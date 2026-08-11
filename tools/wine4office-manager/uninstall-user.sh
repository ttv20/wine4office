#!/usr/bin/env bash
# Remove Wine4Office from the current user. Prefix deletion requires an explicit option.
set -euo pipefail

DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
CONFIG_HOME=${XDG_CONFIG_HOME:-$HOME/.config}
ROOT=${WINE4OFFICE_MANAGER_HOME:-$DATA_HOME/wine4office}
BIN_HOME=${WINE4OFFICE_BIN_HOME:-$HOME/.local/bin}
PURGE_RUNNER=false
REMOVE_PREFIX=

usage() {
    echo "Usage: $0 [--purge-runner] [--remove-prefix PATH]" >&2
}
while [[ $# -gt 0 ]]; do
    case $1 in
        --purge-runner) PURGE_RUNNER=true; shift ;;
        --remove-prefix)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            REMOVE_PREFIX=$2; shift 2 ;;
        *) usage; exit 2 ;;
    esac
done


# Validate ownership, confirm Wine shutdown, and remove the selected prefix
# through the manager's anchored no-follow deletion helper.
if [[ -n $REMOVE_PREFIX ]]; then
    [[ -f "$ROOT/lib/wine4office_backend.py" &&
       -f "$ROOT/lib/wine4office_manager.py" ]] || {
        echo "Cannot safely remove the prefix because the installed manager backend is missing." >&2
        exit 1
    }
    python3 -I - "$ROOT/lib" "$REMOVE_PREFIX" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import wine4office_backend as backend
import wine4office_manager as manager

config = backend.load_config()
removed = manager.stop_and_remove_owned_prefix(
    sys.argv[2], config["wine"], use_x11=config.get("use_x11", True)
)
print(f"Removed Wine environment: {removed}")
PY
fi

if [[ -f "$ROOT/lib/wine4office_backend.py" ]]; then
    python3 -I - "$ROOT/lib" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import wine4office_backend as backend
backend.uninstall_preload_service()
if hasattr(backend, "uninstall_automatic_update_schedule"):
    backend.uninstall_automatic_update_schedule()
PY
elif [[ -e "$CONFIG_HOME/systemd/user/wine4office-preload.service" ||
        -e "$CONFIG_HOME/wine4office/preload-service.json" ||
        -e "$CONFIG_HOME/systemd/user/wine4office-update-check.service" ||
        -e "$CONFIG_HOME/systemd/user/wine4office-update-check.timer" ]]; then
    echo "Cannot safely remove Wine4Office user services because the installed backend is missing." >&2
    exit 1
fi

if [[ -f "$ROOT/lib/wine4office_backend.py" ]]; then
    python3 -I - "$ROOT/lib" <<'PY' || true
import sys
sys.path.insert(0, sys.argv[1])
import wine4office_backend as backend
backend.remove_app_shortcuts(backend.APP_META)
PY
fi
for file in "$DATA_HOME/applications/wine4office-manager.desktop" "${XDG_DESKTOP_DIR:-$HOME/Desktop}/wine4office-manager.desktop"; do
    if [[ -f "$file" ]] && grep -q '^X-Wine4Office-Managed=true$' "$file"; then rm -f "$file"; fi
done
rm -f "$DATA_HOME/icons/wine4office"/wine4office-manager*.png
for link in "$BIN_HOME/wine4office-manager" "$BIN_HOME/wine4office-launcher"; do
    if [[ -L "$link" ]] && [[ $(readlink "$link") == "$ROOT"/bin/* ]]; then rm -f "$link"; fi
done

rm -rf "$ROOT/lib" "$ROOT/icons"
rm -f "$ROOT/bin/wine4office-manager" "$ROOT/bin/wine4office-launcher"
if $PURGE_RUNNER; then
    rm -rf "$ROOT/runner"
    rm -f "$ROOT/VERSION" "$ROOT/UPDATE_URL" "$ROOT/UPDATE_CHANNEL" "$ROOT/install.json"
    rm -rf "$CONFIG_HOME/wine4office"
fi
rm -f "$ROOT/bin/wine4office-uninstall"
rmdir "$ROOT/bin" "$ROOT" 2>/dev/null || true

if $PURGE_RUNNER; then
    echo "Wine4Office runner, manager, shortcuts, and configuration removed."
else
    printf 'Wine4Office Manager removed. Prefixes, configuration, and any runner in %s/runner were preserved.\n' "$ROOT"
fi
