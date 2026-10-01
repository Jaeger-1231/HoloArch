#!/usr/bin/env bash
set -Eeuo pipefail

REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
RELEASE_ID="$(git -C "$REPO_DIR" rev-parse --short=12 HEAD)-$(date +%Y%m%d-%H%M%S)"
readonly REPO_DIR RELEASE_ID
readonly RELEASE_DIR="$HOME/.local/share/holoarch/releases/$RELEASE_ID"
export PATH="$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"
unset PYTHONPATH PYTHONHOME CONDA_PREFIX CONDA_DEFAULT_ENV QT_PLUGIN_PATH QML2_IMPORT_PATH

/usr/bin/python3 "$REPO_DIR/clavis/scripts/release.py" bundle-resources \
    --cache "$HOME/.cache/holoarch-build/resources"
cmake -S "$REPO_DIR/clavis" -B "$REPO_DIR/clavis/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/usr/bin/python3 \
    -DCMAKE_INSTALL_PREFIX="$RELEASE_DIR" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCLAVIS_CONFIG_INSTALL_DIR:STRING=share/quickshell/clavis \
    -DCLAVIS_QML_INSTALL_DIR:STRING=lib/qt6/qml
cmake --build "$REPO_DIR/clavis/build" --parallel "${CLAVIS_BUILD_JOBS:-6}"
test_runtime=$(mktemp -d /tmp/holoarch-clavis-tests.XXXXXX)
trap 'rm -rf -- "$test_runtime"' EXIT
XDG_RUNTIME_DIR="$test_runtime" ctest --test-dir "$REPO_DIR/clavis/build" --output-on-failure --no-tests=error
cmake --install "$REPO_DIR/clavis/build"
git -C "$REPO_DIR" rev-parse HEAD > "$RELEASE_DIR/SOURCE_COMMIT"
git -C "$REPO_DIR" diff --binary HEAD > "$RELEASE_DIR/LOCAL_CHANGES.patch"
mkdir -p "$HOME/.local/state/holoarch"
printf '%s\n' "$RELEASE_DIR" > "$HOME/.local/state/holoarch/clavis-last-build"
printf '\n构建和测试通过：%s\n用 holoarch-shell promote 切换到此版本。\n' "$RELEASE_DIR"
