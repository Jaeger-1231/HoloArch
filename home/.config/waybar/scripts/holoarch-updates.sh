#!/usr/bin/env bash

set -uo pipefail

readonly CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/holoarch-updates"
readonly JSON_FILE="$CACHE_DIR/status.json"
readonly REPO_FILE="$CACHE_DIR/repo.txt"
readonly AUR_FILE="$CACHE_DIR/aur.txt"
readonly FLATPAK_FILE="$CACHE_DIR/flatpak.txt"
readonly CHECK_INTERVAL=3600
readonly ERROR_RETRY_INTERVAL=60

mkdir -p "$CACHE_DIR"

if [[ -n "${XDG_RUNTIME_DIR:-}" && -w "$XDG_RUNTIME_DIR" ]]; then
    readonly LOCK_FILE="$XDG_RUNTIME_DIR/holoarch-updates.lock"
else
    readonly LOCK_FILE="$CACHE_DIR/holoarch-updates.lock"
fi

# 当前桌面会话可能仍继承旧的 10808；使用已经验证可用的 HTTP 代理。
if [[ "${ALL_PROXY:-}" == "socks5://127.0.0.1:10808" \
      && "${HTTPS_PROXY:-}" == "http://127.0.0.1:10809" ]]; then
    export ALL_PROXY="$HTTPS_PROXY"
    export all_proxy="${https_proxy:-$HTTPS_PROXY}"
fi

last_transaction() {
    local line timestamp epoch now age
    line=$(tac /var/log/pacman.log 2>/dev/null | grep -m1 '\[ALPM\] transaction completed' || true)
    [[ -n "$line" ]] || { printf '上次软件事务：未知'; return; }
    timestamp=${line%%]*}
    timestamp=${timestamp#\[}
    epoch=$(date -d "$timestamp" +%s 2>/dev/null || true)
    [[ -n "$epoch" ]] || { printf '上次软件事务：未知'; return; }
    now=$(date +%s)
    age=$((now - epoch))
    if (( age < 60 )); then
        printf '上次软件事务：刚刚'
    elif (( age < 3600 )); then
        printf '上次软件事务：%d 分钟前' $((age / 60))
    elif (( age < 86400 )); then
        printf '上次软件事务：%d 小时前' $((age / 3600))
    else
        printf '上次软件事务：%d 天前' $((age / 86400))
    fi
}

write_lines() {
    local content=$1 target=$2 temp
    temp=$(mktemp "$CACHE_DIR/.tmp.XXXXXX") || return 1
    printf '%s\n' "$content" | sed '/^[[:space:]]*$/d' > "$temp"
    mv "$temp" "$target"
}

publish_json() {
    local json=$1 temp
    temp=$(mktemp "$CACHE_DIR/.status.XXXXXX") || return 1
    printf '%s\n' "$json" > "$temp"
    mv "$temp" "$JSON_FILE"
}

emit_error() {
    local message=$1 json
    json=$(jq -cn --arg tooltip "$message" \
        '{text:"?", alt:"error", tooltip:$tooltip}') || return 1
    publish_json "$json"
}

perform_check() {
    (
        flock -x 9

        local repo="" aur="" flatpak="" status=0
        repo=$(checkupdates 2>/dev/null) || status=$?
        if [[ $status -ne 0 && $status -ne 2 ]]; then
            emit_error "无法检查官方仓库更新；请检查网络和镜像。"
            return 1
        fi

        if command -v paru >/dev/null 2>&1; then
            aur=$(paru -Qua --nodevel 2>/dev/null || true)
        fi
        if command -v flatpak >/dev/null 2>&1; then
            flatpak=$(flatpak remote-ls --updates --columns=application,branch 2>/dev/null || true)
        fi

        if ! write_lines "$repo" "$REPO_FILE" \
            || ! write_lines "$aur" "$AUR_FILE" \
            || ! write_lines "$flatpak" "$FLATPAK_FILE"; then
            emit_error "无法保存更新检查结果。"
            return 1
        fi
        emit_json
    ) 9>"$LOCK_FILE"
}

emit_json() {
    local updates="" line count=0 age_info tooltip
    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        updates+="[Pacman] $line"$'\n'
        ((count++))
    done < "$REPO_FILE"
    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        updates+="[AUR] $line"$'\n'
        ((count++))
    done < "$AUR_FILE"
    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        updates+="[Flatpak] $line"$'\n'
        ((count++))
    done < "$FLATPAK_FILE"

    age_info=$(last_transaction)
    local json
    if (( count == 0 )); then
        json=$(jq -cn --arg tooltip "System is up to date\n----------------\n$age_info" \
            '{text:"", alt:"updated", tooltip:$tooltip}') || return 1
    else
        tooltip="${updates%$'\n'}"$'\n----------------\n'"$age_info"
        json=$(jq -cn --arg text "$count" --arg tooltip "$tooltip" \
            '{text:$text, alt:"has-updates", tooltip:$tooltip}') || return 1
    fi
    publish_json "$json"
}

run_once() {
    local status=0
    perform_check || status=$?
    [[ -s "$JSON_FILE" ]] && cat "$JSON_FILE"
    return "$status"
}

display_status() {
    local age_limit=$CHECK_INTERVAL now modified=0 pacman_modified=0
    if [[ -s "$JSON_FILE" ]]; then
        modified=$(stat -c %Y "$JSON_FILE" 2>/dev/null || printf 0)
        if jq -e '.alt == "error"' "$JSON_FILE" >/dev/null 2>&1; then
            age_limit=$ERROR_RETRY_INTERVAL
        fi
    fi
    now=$(date +%s)
    pacman_modified=$(stat -c %Y /var/log/pacman.log 2>/dev/null || printf 0)
    if [[ ! -s "$JSON_FILE" ]] \
        || (( now - modified >= age_limit || pacman_modified > modified )); then
        run_once >/dev/null || true
    fi
    if [[ -s "$JSON_FILE" ]]; then
        cat "$JSON_FILE"
    else
        jq -cn '{text:"?", alt:"error", tooltip:"无法读取更新状态。"}'
    fi
}

case "${1:---display}" in
    --once) run_once ;;
    --display) display_status ;;
    *) printf '用法：%s [--once|--display]\n' "$0" >&2; exit 2 ;;
esac
