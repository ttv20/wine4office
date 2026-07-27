#!/usr/bin/env bash
# Package a compiled Wine runner and Wine 365 Manager as one executable .run file.
set -euo pipefail

usage() {
    echo "Usage: $0 RUNNER_DIR OUTPUT.run VERSION [UPDATE_MANIFEST_URL] [INSTALLER_URL] [QT_MANAGER_BINARY]" >&2
    exit 2
}
[[ $# -ge 3 && $# -le 6 ]] || usage
RUNNER=$(cd "$1" && pwd)
OUTPUT=$2
VERSION=$3
UPDATE_URL=${4:-}
INSTALLER_URL=${5:-}
QT_MANAGER=${6:-}
HERE=$(cd "$(dirname "$0")/.." && pwd)

[[ -x "$RUNNER/bin/wine" ]] || { echo "Runner has no executable bin/wine: $RUNNER" >&2; exit 1; }
command -v zstd >/dev/null || { echo "zstd is required" >&2; exit 1; }
[[ $VERSION =~ ^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$ ]] || { echo "Unsafe version: $VERSION" >&2; exit 1; }
if [[ -n $UPDATE_URL && $UPDATE_URL != https://* ]]; then
    echo "Update manifest address must use HTTPS." >&2; exit 1
fi
if [[ -n $INSTALLER_URL && $INSTALLER_URL != https://* ]]; then
    echo "Installer address must use HTTPS." >&2; exit 1
fi
if [[ -n $QT_MANAGER && ! -x $QT_MANAGER ]]; then
    echo "Qt manager binary is missing or not executable: $QT_MANAGER" >&2; exit 1
fi
mkdir -p "$(dirname "$OUTPUT")"
OUTPUT=$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/payload/runner" "$STAGE/payload/manager"
cp -a "$RUNNER/." "$STAGE/payload/runner/"
tar -C "$HERE" --exclude='tests' --exclude='packaging' --exclude='__pycache__' --exclude='*.pyc' \
    -cf - . | tar -C "$STAGE/payload/manager" -xf -
if [[ -n $QT_MANAGER ]]; then
    install -m 0755 "$QT_MANAGER" "$STAGE/payload/manager/wine365-manager-qt"
fi
printf '%s\n' "$VERSION" > "$STAGE/payload/VERSION"
printf '%s\n' "$UPDATE_URL" > "$STAGE/payload/UPDATE_URL"
tar -C "$STAGE" -cf - payload | \
    zstd -T0 -19 --long=27 --no-progress -o "$STAGE/payload.tar.zst"

cat > "$OUTPUT" <<EOF
#!/bin/sh
# Wine 365 self-extracting user installer. Generated; do not edit.
set -eu
BUNDLE_VERSION='$VERSION'

fail() { echo "wine365-installer: \$*" >&2; exit 1; }
usage() {
    echo "Usage: \$0 [--install|--update|--uninstall|--version|--extract DIRECTORY]" >&2
    exit 2
}
MODE=--install
EXTRACT_DIR=
case \${1:-} in
    '') ;;
    --install|--update|--uninstall|--version) MODE=\$1; shift ;;
    --extract) MODE=\$1; shift; [ \$# -eq 1 ] || usage; EXTRACT_DIR=\$1; shift ;;
    *) usage ;;
esac
[ \$# -eq 0 ] || usage
[ "\$MODE" != --version ] || { echo "\$BUNDLE_VERSION"; exit 0; }
DATA_HOME=\${XDG_DATA_HOME:-\$HOME/.local/share}
ROOT=\${WINE365_MANAGER_HOME:-\$DATA_HOME/wine365}
if [ "\$MODE" = --uninstall ]; then
    [ -x "\$ROOT/bin/wine365-uninstall" ] || fail "Wine 365 is not installed at \$ROOT"
    exec "\$ROOT/bin/wine365-uninstall" --purge-runner
fi
[ "\$(id -u)" -ne 0 ] || fail "do not run this installer as root"
command -v python3 >/dev/null 2>&1 || fail "python3 is required"
command -v zstd >/dev/null 2>&1 || fail "zstd is required"
case \$ROOT in "\$HOME"/*) ;; *) fail "install root must be inside your home directory: \$ROOT";; esac
PAYLOAD_LINE=\$(awk '/^__WINE365_PAYLOAD_BELOW__\$/ { print NR + 1; exit }' "\$0")
[ -n "\$PAYLOAD_LINE" ] || fail "embedded payload marker is missing"
if [ "\$MODE" = --extract ]; then
    mkdir -p "\$EXTRACT_DIR"
    tail -n +"\$PAYLOAD_LINE" "\$0" | zstd -d -q -c | tar -xf - -C "\$EXTRACT_DIR"
    echo "Extracted Wine 365 \$BUNDLE_VERSION to \$EXTRACT_DIR"
    exit 0
fi
TMP=\$(mktemp -d)
LOCK_CREATED=false
NEW=
OLD=
cleanup() {
    rm -rf "\$TMP"
    [ -z "\$NEW" ] || rm -rf "\$NEW"
    if \$LOCK_CREATED; then rmdir "\$ROOT/.install-lock" 2>/dev/null || true; fi
}
trap cleanup EXIT INT TERM
mkdir -p "\$ROOT"
mkdir "\$ROOT/.install-lock" 2>/dev/null || fail "another Wine 365 install or update is running"
LOCK_CREATED=true
tail -n +"\$PAYLOAD_LINE" "\$0" | zstd -d -q -c | tar -xf - -C "\$TMP"
[ -x "\$TMP/payload/runner/bin/wine" ] || fail "payload runner is invalid"
VERSION=\$(cat "\$TMP/payload/VERSION")
UPDATE_URL=\$(cat "\$TMP/payload/UPDATE_URL")
NEW="\$ROOT/.runner.new.\$\$"
OLD="\$ROOT/.runner.old.\$\$"
cp -a "\$TMP/payload/runner" "\$NEW"
if [ -e "\$ROOT/runner" ]; then mv "\$ROOT/runner" "\$OLD"; fi
mv "\$NEW" "\$ROOT/runner"; NEW=
if ! WINE365_MANAGER_HOME="\$ROOT" WINE365_WINE="\$ROOT/runner/bin/wine" \
     WINE365_VERSION="\$VERSION" WINE365_UPDATE_URL="\$UPDATE_URL" \
     "\$TMP/payload/manager/install-user.sh"; then
    rm -rf "\$ROOT/runner"
    [ ! -e "\$OLD" ] || mv "\$OLD" "\$ROOT/runner"
    fail "manager installation failed; previous runner restored"
fi
rm -rf "\$OLD"; OLD=
WINE365_ROOT="\$ROOT" WINE365_VERSION="\$VERSION" WINE365_UPDATE_URL="\$UPDATE_URL" python3 - <<'PY'
import json, os
from pathlib import Path
root = Path(os.environ["WINE365_ROOT"])
data = {
    "version": os.environ["WINE365_VERSION"],
    "update_manifest_url": os.environ["WINE365_UPDATE_URL"],
    "install_scope": "user",
}
(root / "install.json").write_text(json.dumps(data, indent=2) + "\\n")
PY
echo "Wine 365 \$VERSION installed for the current user."
echo "Open Wine 365 Manager from the application menu."
exit 0
__WINE365_PAYLOAD_BELOW__
EOF
cat "$STAGE/payload.tar.zst" >> "$OUTPUT"
chmod 0755 "$OUTPUT"
SHA256=$(sha256sum "$OUTPUT" | awk '{print $1}')
SIZE=$(stat -c '%s' "$OUTPUT")
MANIFEST=${OUTPUT%.run}.manifest.json
VERSION="$VERSION" INSTALLER_URL="$INSTALLER_URL" SHA256="$SHA256" SIZE="$SIZE" \
python3 - "$MANIFEST" <<'PY'
import json, os, sys
manifest = {
    "version": os.environ["VERSION"],
    "installer_url": os.environ["INSTALLER_URL"],
    "sha256": os.environ["SHA256"],
    "size": int(os.environ["SIZE"]),
}
with open(sys.argv[1], "w", encoding="utf-8") as destination:
    json.dump(manifest, destination, indent=2)
    destination.write("\n")
PY
printf '%s  %s\n' "$SHA256" "$(basename "$OUTPUT")" > "$OUTPUT.sha256"
printf 'Created %s (%s bytes)\nManifest: %s\n' "$OUTPUT" "$SIZE" "$MANIFEST"
