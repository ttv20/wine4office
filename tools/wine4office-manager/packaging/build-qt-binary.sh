#!/usr/bin/env bash
# Build a standalone native Qt Wine4Office Manager with PyInstaller.
set -euo pipefail

[[ $# -eq 1 ]] || { echo "Usage: $0 OUTPUT" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUTPUT=$1
PYTHON=${PYTHON:-python3}

"$PYTHON" -c 'import PyInstaller, PySide6, pefile' >/dev/null 2>&1 || {
    echo "PyInstaller, PySide6, and pefile are required; install requirements-build.txt" >&2
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
    --name wine4office-manager-qt \
    --distpath "$BUILD/dist" \
    --workpath "$BUILD/work" \
    --specpath "$BUILD/spec" \
    --paths "$HERE" \
    --add-data "$HERE/icons:icons" \
    --add-data "$HERE/register-office-cloud-fonts.sh:." \
    --hidden-import wine4office_backend \
    --hidden-import wine4office_qt \
    "$HERE/wine4office_manager.py"
install -m 0755 "$BUILD/dist/wine4office-manager-qt" "$OUTPUT"
printf 'Created native Qt manager: %s (%s bytes)\n' "$OUTPUT" "$(stat -c '%s' "$OUTPUT")"
