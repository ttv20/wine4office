#!/usr/bin/env bash
# Install Wine 365 Manager for the current user. Never invokes sudo.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
ROOT=${WINE365_MANAGER_HOME:-$DATA_HOME/wine365}
BIN_HOME=${WINE365_BIN_HOME:-$HOME/.local/bin}
LIB=$ROOT/lib

command -v python3 >/dev/null 2>&1 || { echo "python3 is required" >&2; exit 1; }
mkdir -p "$LIB" "$ROOT/bin" "$ROOT/icons" "$BIN_HOME" "$DATA_HOME/applications"
install -m 0644 "$HERE/wine365_backend.py" "$LIB/wine365_backend.py"
install -m 0755 "$HERE/wine365_manager.py" "$LIB/wine365_manager.py"
install -m 0644 "$HERE/wine365_qt.py" "$LIB/wine365_qt.py"
install -m 0755 "$HERE/register-office-cloud-fonts.sh" "$LIB/register-office-cloud-fonts.sh"
rm -f "$LIB/ui.html"
if [[ -x "$HERE/wine365-manager-qt" ]]; then
    install -m 0755 "$HERE/wine365-manager-qt" "$LIB/wine365-manager-qt"
else
    rm -f "$LIB/wine365-manager-qt"
fi
install -m 0755 "$HERE/wine365-launcher" "$ROOT/bin/wine365-launcher"
install -m 0755 "$HERE/uninstall-user.sh" "$ROOT/bin/wine365-uninstall"
install -m 0644 "$HERE"/icons/*.svg "$ROOT/icons/"

cat >"$ROOT/bin/wine365-manager" <<'EOF'
#!/bin/sh
SELF=$(readlink -f "$0")
ROOT=$(CDPATH= cd -- "$(dirname -- "$SELF")/.." && pwd)
export WINE365_MANAGER_ROOT="$ROOT"
if [ -x "$ROOT/lib/wine365-manager-qt" ]; then
    exec "$ROOT/lib/wine365-manager-qt" "$@"
fi
if ! python3 -c 'import PySide6, pefile' >/dev/null 2>&1; then
    echo "wine365-manager: PySide6 and pefile are required for the native Qt interface." >&2
    echo "Install them with: python3 -m pip install --user PySide6 pefile" >&2
    exit 1
fi
exec python3 "$ROOT/lib/wine365_manager.py" "$@"
EOF
chmod 0755 "$ROOT/bin/wine365-manager"
ln -sfn "$ROOT/bin/wine365-manager" "$BIN_HOME/wine365-manager"
ln -sfn "$ROOT/bin/wine365-launcher" "$BIN_HOME/wine365-launcher"

if [[ -n ${WINE365_VERSION:-} ]]; then printf '%s\n' "$WINE365_VERSION" > "$ROOT/VERSION"; fi
if [[ -n ${WINE365_UPDATE_URL+x} ]]; then printf '%s\n' "$WINE365_UPDATE_URL" > "$ROOT/UPDATE_URL"; fi
PYTHONPATH="$LIB" python3 - <<'PY'
import wine365_backend as backend
if not backend.config_path().exists():
    backend.save_config(backend.default_config())
PY
python3 "$LIB/wine365_manager.py" --install-shortcut >/dev/null
printf 'Wine 365 Manager installed for %s.\n' "${USER:-$(id -un)}"
printf 'Application shortcut: %s/applications/wine365-manager.desktop\n' "$DATA_HOME"
printf 'Command: %s/wine365-manager\n' "$BIN_HOME"
