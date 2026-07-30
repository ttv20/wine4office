#!/usr/bin/env bash
# Build the standalone native Qt Wine4Office Manager with PyInstaller.
set -euo pipefail

[[ $# -ge 1 && $# -le 3 ]] || { echo "Usage: $0 OUTPUT [VERSION] [CHANNEL]" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUTPUT=$1
VERSION=${2:-${VERSION:-development}}
CHANNEL=${3:-${CHANNEL:-stable}}
PYTHON=${PYTHON:-python3}

"$PYTHON" -c 'import certifi, PyInstaller, PySide6, pefile, zstandard' >/dev/null 2>&1 || {
    echo "certifi, PyInstaller, PySide6, pefile, and zstandard are required; install requirements-build.txt" >&2
    exit 1
}
[[ $VERSION =~ ^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$ ]] || {
    echo "Unsafe Wine4Office Manager version: $VERSION" >&2
    exit 1
}
[[ $CHANNEL =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || {
    echo "Unsafe Wine4Office Manager channel: $CHANNEL" >&2
    exit 1
}
mkdir -p "$(dirname "$OUTPUT")"
OUTPUT=$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT
printf '%s\n' "$VERSION" > "$BUILD/VERSION"
printf '%s\n' "$CHANNEL" > "$BUILD/CHANNEL"

"$PYTHON" -m PyInstaller \
    --clean \
    --noconfirm \
    --noupx \
    --onefile \
    --name Wine4OfficeManager \
    --distpath "$BUILD/dist" \
    --workpath "$BUILD/work" \
    --specpath "$BUILD/spec" \
    --paths "$HERE" \
    --add-data "$HERE/icons:icons" \
    --add-data "$HERE/register-office-cloud-fonts.sh:." \
    --add-data "$BUILD/VERSION:." \
    --add-data "$BUILD/CHANNEL:." \
    --hidden-import wine4office_backend \
    --hidden-import wine4office_post_install \
    --hidden-import wine4office_qt \
    --hidden-import zstandard \
    "$HERE/wine4office_manager.py"
install -m 0755 "$BUILD/dist/Wine4OfficeManager" "$OUTPUT"
printf 'Created standalone Wine4Office Manager: %s (%s bytes)\n' "$OUTPUT" "$(stat -c '%s' "$OUTPUT")"
