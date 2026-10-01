#!/usr/bin/env bash
set -Eeuo pipefail
REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
KEY_VERSION="$(cat "$REPO_DIR/key-cli/src/key_cli/VERSION")"
readonly REPO_DIR KEY_VERSION
RUNTIME_DIR="$HOME/.local/share/holoarch/key-cli-$KEY_VERSION-$(date +%Y%m%d-%H%M%S)"
readonly RUNTIME_DIR
export PATH="$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"
unset PYTHONPATH PYTHONHOME CONDA_PREFIX CONDA_DEFAULT_ENV QT_PLUGIN_PATH
cmake -S "$REPO_DIR/key-cli/native" -B "$REPO_DIR/key-cli/native/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$REPO_DIR/key-cli/native/build" --parallel "${CLAVIS_BUILD_JOBS:-6}"
ctest --test-dir "$REPO_DIR/key-cli/native/build" --output-on-failure --no-tests=error
/usr/bin/python3 -m venv --system-site-packages "$RUNTIME_DIR/venv"
"$RUNTIME_DIR/venv/bin/python" -m pip install --no-build-isolation --no-deps \
    --force-reinstall "$REPO_DIR/key-cli"
install -m755 "$REPO_DIR/key-cli/native/build/bin/key-sysmon" "$RUNTIME_DIR/venv/bin/key-sysmon"
install -m755 "$REPO_DIR/key-cli/native/build/bin/key-cpu-power" "$RUNTIME_DIR/venv/bin/key-cpu-power"
ln -sfn "$RUNTIME_DIR" "$HOME/.local/share/holoarch/key-current"
printf '\nkey-cli 已部署：%s\n' "$RUNTIME_DIR"
