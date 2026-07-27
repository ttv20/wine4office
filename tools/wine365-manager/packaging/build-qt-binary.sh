#!/usr/bin/env bash
# Build a standalone native Qt Wine 365 Manager with PyInstaller.
set -euo pipefail

[[ $# -eq 1 ]] || { echo "Usage: $0 OUTPUT" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUTPUT=$1
PYTHON=${PYTHON:-python3}

"$PYTHON" -c 'import PyInstaller, PySide6' >/dev/null 2>&1 || {
    echo "PyInstaller and PySide6 are required; install requirements-build.txt" >&2
    exit 1
}
mkdir -p "$(dirname "$OUTPUT")"
OUTPUT=$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

"$PYTHON" -m PyInstaller \
    --clean \
    --noconfirm \
    --noupx \
    --onefile \
    --name wine365-manager-qt \
    --distpath "$BUILD/dist" \
    --workpath "$BUILD/work" \
    --specpath "$BUILD/spec" \
    --paths "$HERE" \
    --add-data "$HERE/icons:icons" \
    --add-data "$HERE/register-office-cloud-fonts.sh:." \
    --hidden-import wine365_backend \
    --hidden-import wine365_qt \
    "$HERE/wine365_manager.py"
install -m 0755 "$BUILD/dist/wine365-manager-qt" "$OUTPUT"
printf 'Created native Qt manager: %s (%s bytes)\n' "$OUTPUT" "$(stat -c '%s' "$OUTPUT")"
