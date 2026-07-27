#!/usr/bin/env bash
# Install Wine4Office Manager for the current user. Never invokes sudo.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
ROOT=${WINE4OFFICE_MANAGER_HOME:-$DATA_HOME/wine4office}
BIN_HOME=${WINE4OFFICE_BIN_HOME:-$HOME/.local/bin}
LIB=$ROOT/lib

command -v python3 >/dev/null 2>&1 || { echo "python3 is required" >&2; exit 1; }
mkdir -p "$LIB" "$ROOT/bin" "$ROOT/icons" "$BIN_HOME" "$DATA_HOME/applications"
install -m 0644 "$HERE/wine4office_backend.py" "$LIB/wine4office_backend.py"
install -m 0755 "$HERE/wine4office_manager.py" "$LIB/wine4office_manager.py"
install -m 0644 "$HERE/wine4office_qt.py" "$LIB/wine4office_qt.py"
install -m 0755 "$HERE/register-office-cloud-fonts.sh" "$LIB/register-office-cloud-fonts.sh"
rm -f "$LIB/ui.html"
if [[ -x "$HERE/wine4office-manager-qt" ]]; then
    install -m 0755 "$HERE/wine4office-manager-qt" "$LIB/wine4office-manager-qt"
else
    rm -f "$LIB/wine4office-manager-qt"
fi
install -m 0755 "$HERE/wine4office-launcher" "$ROOT/bin/wine4office-launcher"
install -m 0755 "$HERE/uninstall-user.sh" "$ROOT/bin/wine4office-uninstall"
install -m 0644 "$HERE"/icons/*.svg "$ROOT/icons/"

cat >"$ROOT/bin/wine4office-manager" <<'EOF'
#!/bin/sh
SELF=$(readlink -f "$0")
ROOT=$(CDPATH= cd -- "$(dirname -- "$SELF")/.." && pwd)
export WINE4OFFICE_MANAGER_ROOT="$ROOT"
if [ -x "$ROOT/lib/wine4office-manager-qt" ]; then
    exec "$ROOT/lib/wine4office-manager-qt" "$@"
fi
if ! python3 -c 'import PySide6, pefile' >/dev/null 2>&1; then
    echo "wine4office-manager: PySide6 and pefile are required for the native Qt interface." >&2
    echo "Install them with: python3 -m pip install --user PySide6 pefile" >&2
    exit 1
fi
exec python3 "$ROOT/lib/wine4office_manager.py" "$@"
EOF
chmod 0755 "$ROOT/bin/wine4office-manager"
ln -sfn "$ROOT/bin/wine4office-manager" "$BIN_HOME/wine4office-manager"
ln -sfn "$ROOT/bin/wine4office-launcher" "$BIN_HOME/wine4office-launcher"

if [[ -n ${WINE4OFFICE_VERSION:-} ]]; then printf '%s\n' "$WINE4OFFICE_VERSION" > "$ROOT/VERSION"; fi
if [[ -n ${WINE4OFFICE_UPDATE_URL+x} ]]; then printf '%s\n' "$WINE4OFFICE_UPDATE_URL" > "$ROOT/UPDATE_URL"; fi
WINE4OFFICE_MANAGER_ROOT="$ROOT" PYTHONPATH="$LIB" python3 - <<'PY'
import wine4office_backend as backend
if not backend.config_path().exists():
    backend.save_config(backend.default_config())
PY
WINE4OFFICE_MANAGER_ROOT="$ROOT" python3 "$LIB/wine4office_manager.py" --install-shortcut >/dev/null
printf 'Wine4Office Manager installed for %s.\n' "${USER:-$(id -un)}"
printf 'Application shortcut: %s/applications/wine4office-manager.desktop\n' "$DATA_HOME"
printf 'Command: %s/wine4office-manager\n' "$BIN_HOME"
