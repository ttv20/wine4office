#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
RELEASE=$TMP/release
FAKE_BIN=$TMP/bin
HOME_DIR=$TMP/home
REAL_MANAGER=${WINE4OFFICE_TEST_REAL_MANAGER:-}
mkdir -p "$RELEASE/runner/bin" "$FAKE_BIN" "$HOME_DIR"

cat > "$RELEASE/runner/bin/wine" <<'SH'
#!/bin/sh
exit 0
SH
chmod 0755 "$RELEASE/runner/bin/wine"
printf 'runner payload\n' > "$RELEASE/runner/identity"
tar -C "$RELEASE" -cf - runner | zstd -q -o "$RELEASE/wine.tar.zst"
if [[ -n $REAL_MANAGER ]]; then
    install -m 0755 "$REAL_MANAGER" "$RELEASE/Wine4OfficeManager"
else
cat > "$RELEASE/Wine4OfficeManager" <<'SH'
#!/bin/sh
if [ -n "${WINE4OFFICE_TEST_MANAGER_LOG:-}" ]; then
    if [ "$#" -eq 0 ]; then
        printf '<launch>\n' >> "$WINE4OFFICE_TEST_MANAGER_LOG"
    else
        printf '%s\n' "$*" >> "$WINE4OFFICE_TEST_MANAGER_LOG"
    fi
fi
if [ "${1:-}" = --install-shortcut ]; then
    mkdir -p "$XDG_DATA_HOME"
    printf 'installed\n' > "$XDG_DATA_HOME/shortcut-installed"
fi
exit 0
SH
chmod 0755 "$RELEASE/Wine4OfficeManager"
fi

write_metadata() {
    manager_digest=$1
    manager_version=${2:-0.1.0}
    wine_version=${3:-0.1.0}
    release_channel=${4:-stable}
    RELEASE="$RELEASE" MANAGER_DIGEST="$manager_digest" \
        MANAGER_VERSION="$manager_version" WINE_VERSION="$wine_version" \
        RELEASE_CHANNEL="$release_channel" python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
root = Path(os.environ["RELEASE"])
manager = root / "Wine4OfficeManager"
wine = root / "wine.tar.zst"
payload = {
    "schema_version": 1,
    "channel": os.environ["RELEASE_CHANNEL"],
    "metadata_url": "https://example.invalid/release.json",
    "manager": {
        "version": os.environ["MANAGER_VERSION"],
        "url": "Wine4OfficeManager",
        "sha256": os.environ["MANAGER_DIGEST"],
        "size": manager.stat().st_size,
    },
    "wine": {
        "version": os.environ["WINE_VERSION"],
        "url": "wine.tar.zst",
        "sha256": hashlib.sha256(wine.read_bytes()).hexdigest(),
        "size": wine.stat().st_size,
        "format": "tar.zst",
    },
}
(root / "release.json").write_text(json.dumps(payload))
PY
}
MANAGER_DIGEST=$(sha256sum "$RELEASE/Wine4OfficeManager")
MANAGER_DIGEST=${MANAGER_DIGEST%% *}
write_metadata "$MANAGER_DIGEST"

cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
url=
while (($#)); do
    case $1 in
        https://*) url=$1; shift ;;
        *) shift ;;
    esac
done
[[ -n $url ]]
printf '%s\n' "$url" >> "$FAKE_CURL_LOG"
case $url in
    */release.json) source=$FAKE_RELEASE/release.json ;;
    */Wine4OfficeManager) source=$FAKE_RELEASE/Wine4OfficeManager ;;
    */wine.tar.zst) source=$FAKE_RELEASE/wine.tar.zst ;;
    *) exit 22 ;;
esac
cat "$source"
SH
chmod 0755 "$FAKE_BIN/curl"

export HOME=$HOME_DIR
export XDG_DATA_HOME=$HOME_DIR/data
export WINE4OFFICE_BIN_HOME=$HOME_DIR/bin
export WINE4OFFICE_HOME=$HOME_DIR/data/wine4office
export FAKE_RELEASE=$RELEASE
export FAKE_CURL_LOG=$TMP/curl.log
export WINE4OFFICE_TEST_MANAGER_LOG=$TMP/manager.log

for invalid_args in "--tag" "--tag invalid/tag" "--unknown"; do
    if PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" $invalid_args >/dev/null 2>&1; then
        echo "installer accepted invalid arguments: $invalid_args" >&2
        exit 1
    fi
done
[[ ! -e $FAKE_CURL_LOG ]]

PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" --tag wine4office-v0.1.10 >/dev/null
[[ $(head -n 1 "$FAKE_CURL_LOG") == \
   https://github.com/ttv20/wine4office/releases/download/wine4office-v0.1.10/release.json ]]
export WINE4OFFICE_METADATA_URL=https://example.invalid/release.json

[[ -x $WINE4OFFICE_HOME/bin/Wine4OfficeManager ]]
[[ -x $WINE4OFFICE_HOME/bin/wine4office-uninstall ]]
[[ -x $WINE4OFFICE_HOME/runner/bin/wine ]]
[[ $(cat "$WINE4OFFICE_HOME/runner/identity") == "runner payload" ]]
[[ $(cat "$WINE4OFFICE_HOME/WINE_VERSION") == 0.1.0 ]]
[[ -L $WINE4OFFICE_BIN_HOME/Wine4OfficeManager ]]
if [[ -n $REAL_MANAGER ]]; then
    [[ -f $XDG_DATA_HOME/applications/wine4office-manager.desktop ]]
else
    [[ -f $XDG_DATA_HOME/shortcut-installed ]]
fi
[[ $(cat "$WINE4OFFICE_HOME/VERSION") == 0.1.0 ]]
[[ $(cat "$WINE4OFFICE_HOME/UPDATE_URL") == https://example.invalid/release.json ]]
[[ $(cat "$WINE4OFFICE_HOME/UPDATE_CHANNEL") == stable ]]
[[ $(cat "$WINE4OFFICE_HOME/STANDALONE") == Wine4OfficeManager ]]

cp /bin/dash "$WINE4OFFICE_HOME/runner/bin/wine-active-test"
"$WINE4OFFICE_HOME/runner/bin/wine-active-test" -c 'while :; do :; done' &
ACTIVE_WINE=$!
for _ in {1..50}; do
    [[ $(readlink -f "/proc/$ACTIVE_WINE/exe" 2>/dev/null || true) == \
       "$WINE4OFFICE_HOME/runner/bin/wine-active-test" ]] && break
    sleep 0.02
done
write_metadata "$(printf '0%.0s' {1..64})"
if PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" --force >/dev/null 2>&1; then
    echo "installer accepted invalid staging metadata" >&2
    exit 1
fi
kill -0 "$ACTIVE_WINE"
write_metadata "$MANAGER_DIGEST"
if PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null 2>"$TMP/active.err"; then
    echo "installer updated while Wine4Office was active" >&2
    exit 1
fi
grep -q -- 'rerun with --force' "$TMP/active.err"
kill -0 "$ACTIVE_WINE"
PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" --force >/dev/null
if kill -0 "$ACTIVE_WINE" 2>/dev/null; then
    echo "installer --force left Wine4Office running" >&2
    exit 1
fi
write_metadata "$MANAGER_DIGEST" 0.2.0-beta.1 0.2.0-beta.1 prerelease
PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" --tag 0.2.0-beta.1 >/dev/null
[[ $(cat "$WINE4OFFICE_HOME/VERSION") == 0.2.0-beta.1 ]]
[[ $(cat "$WINE4OFFICE_HOME/UPDATE_CHANNEL") == stable ]]
write_metadata "$MANAGER_DIGEST"
if [[ -z $REAL_MANAGER ]]; then
    [[ $(grep -c '^<launch>$' "$WINE4OFFICE_TEST_MANAGER_LOG" || true) == 0 ]]

    WINE4OFFICE_LAUNCH_MANAGER=no PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null
    [[ $(grep -c '^<launch>$' "$WINE4OFFICE_TEST_MANAGER_LOG" || true) == 0 ]]
    WINE4OFFICE_LAUNCH_MANAGER= PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null
    for _ in {1..20}; do
        [[ $(grep -c '^<launch>$' "$WINE4OFFICE_TEST_MANAGER_LOG" || true) == 1 ]] && break
        sleep 0.05
    done
    [[ $(grep -c '^<launch>$' "$WINE4OFFICE_TEST_MANAGER_LOG" || true) == 1 ]]
fi

PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null &
FIRST_INSTALL=$!
PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null &
SECOND_INSTALL=$!
wait "$FIRST_INSTALL"
wait "$SECOND_INSTALL"

printf '# updated manager\n' >> "$RELEASE/Wine4OfficeManager"
printf 'runner payload v2\n' > "$RELEASE/runner/identity"
tar -C "$RELEASE" -cf - runner | zstd -q -f -o "$RELEASE/wine.tar.zst"
MANAGER_DIGEST=$(sha256sum "$RELEASE/Wine4OfficeManager")
MANAGER_DIGEST=${MANAGER_DIGEST%% *}
write_metadata "$MANAGER_DIGEST" 0.2.0 0.2.0
PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null
[[ $(cat "$WINE4OFFICE_HOME/VERSION") == 0.2.0 ]]
[[ $(cat "$WINE4OFFICE_HOME/WINE_VERSION") == 0.2.0 ]]
[[ $(cat "$WINE4OFFICE_HOME/runner/identity") == "runner payload v2" ]]

BEFORE_MANAGER=$(sha256sum "$WINE4OFFICE_HOME/bin/Wine4OfficeManager")
BEFORE_RUNNER=$(sha256sum "$WINE4OFFICE_HOME/runner/identity")

write_metadata "$(printf '0%.0s' {1..64})"
if PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null 2>&1; then
    echo "installer accepted a bad manager digest" >&2
    exit 1
fi
[[ $(sha256sum "$WINE4OFFICE_HOME/bin/Wine4OfficeManager") == "$BEFORE_MANAGER" ]]
[[ $(sha256sum "$WINE4OFFICE_HOME/runner/identity") == "$BEFORE_RUNNER" ]]

mv "$RELEASE/wine.tar.zst" "$RELEASE/wine.safe.tar.zst"
RELEASE="$RELEASE" python3 - <<'PY'
import io
import os
import tarfile
from pathlib import Path

archive = Path(os.environ["RELEASE"]) / "wine.malicious.tar"
with tarfile.open(archive, "w") as bundle:
    root = tarfile.TarInfo("runner")
    root.type = tarfile.DIRTYPE
    bundle.addfile(root)
    escape = tarfile.TarInfo("runner/escape")
    escape.type = tarfile.SYMTYPE
    escape.linkname = "../../outside"
    bundle.addfile(escape)
    wine = tarfile.TarInfo("runner/bin/wine")
    wine.mode = 0o755
    wine.size = 1
    bundle.addfile(wine, io.BytesIO(b"x"))
PY
zstd -q "$RELEASE/wine.malicious.tar" -o "$RELEASE/wine.tar.zst"
write_metadata "$MANAGER_DIGEST"
if PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null 2>&1; then
    echo "installer accepted a path-escaping Wine archive" >&2
    exit 1
fi
[[ ! -e $TMP/outside ]]
[[ $(sha256sum "$WINE4OFFICE_HOME/bin/Wine4OfficeManager") == "$BEFORE_MANAGER" ]]
[[ $(sha256sum "$WINE4OFFICE_HOME/runner/identity") == "$BEFORE_RUNNER" ]]

mkdir -p "$WINE4OFFICE_HOME/lib" "$WINE4OFFICE_HOME/icons"
printf 'legacy\n' > "$WINE4OFFICE_HOME/lib/legacy-manager"
printf 'legacy\n' > "$WINE4OFFICE_HOME/icons/legacy-icon"
printf 'legacy\n' > "$WINE4OFFICE_HOME/bin/wine4office-preload-worker"
ln -sfn "$WINE4OFFICE_HOME/bin/wine4office-preload-worker" \
    "$WINE4OFFICE_BIN_HOME/wine4office-launcher"
PATH="$FAKE_BIN:$PATH" "$WINE4OFFICE_HOME/bin/wine4office-uninstall" --purge-runner >/dev/null
[[ ! -e $WINE4OFFICE_HOME ]]
[[ ! -e $WINE4OFFICE_BIN_HOME/Wine4OfficeManager &&
   ! -L $WINE4OFFICE_BIN_HOME/Wine4OfficeManager ]]
[[ ! -e $WINE4OFFICE_BIN_HOME/wine4office-launcher &&
   ! -L $WINE4OFFICE_BIN_HOME/wine4office-launcher ]]

echo "curl installer verified install, uninstall, locking, rollback, and archive safety: PASS"
