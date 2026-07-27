#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
RELEASE=$TMP/release
FAKE_BIN=$TMP/bin
HOME_DIR=$TMP/home
mkdir -p "$RELEASE/runner/bin" "$FAKE_BIN" "$HOME_DIR"

cat > "$RELEASE/runner/bin/wine" <<'SH'
#!/bin/sh
exit 0
SH
chmod 0755 "$RELEASE/runner/bin/wine"
printf 'runner payload\n' > "$RELEASE/runner/identity"
tar -C "$RELEASE" -cf - runner | zstd -q -o "$RELEASE/wine.tar.zst"
cat > "$RELEASE/Wine4OfficeManager" <<'SH'
#!/bin/sh
if [ "${1:-}" = --install-shortcut ]; then
    mkdir -p "$XDG_DATA_HOME"
    printf 'installed\n' > "$XDG_DATA_HOME/shortcut-installed"
fi
exit 0
SH
chmod 0755 "$RELEASE/Wine4OfficeManager"

write_metadata() {
    manager_digest=$1
    RELEASE="$RELEASE" MANAGER_DIGEST="$manager_digest" python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
root = Path(os.environ["RELEASE"])
manager = root / "Wine4OfficeManager"
wine = root / "wine.tar.zst"
payload = {
    "schema_version": 1,
    "channel": "stable",
    "metadata_url": "https://example.invalid/release.json",
    "manager": {
        "version": "0.1.0",
        "url": "Wine4OfficeManager",
        "sha256": os.environ["MANAGER_DIGEST"],
        "size": manager.stat().st_size,
    },
    "wine": {
        "version": "0.1.0",
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
export WINE4OFFICE_METADATA_URL=https://example.invalid/release.json
export FAKE_RELEASE=$RELEASE
PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null

[[ -x $WINE4OFFICE_HOME/bin/Wine4OfficeManager ]]
[[ -x $WINE4OFFICE_HOME/runner/bin/wine ]]
[[ $(cat "$WINE4OFFICE_HOME/runner/identity") == "runner payload" ]]
[[ $(cat "$WINE4OFFICE_HOME/WINE_VERSION") == 0.1.0 ]]
[[ -L $WINE4OFFICE_BIN_HOME/Wine4OfficeManager ]]
[[ -f $XDG_DATA_HOME/shortcut-installed ]]
[[ $(cat "$WINE4OFFICE_HOME/VERSION") == 0.1.0 ]]
[[ $(cat "$WINE4OFFICE_HOME/UPDATE_URL") == https://example.invalid/release.json ]]
[[ $(cat "$WINE4OFFICE_HOME/UPDATE_CHANNEL") == stable ]]
[[ $(cat "$WINE4OFFICE_HOME/STANDALONE") == Wine4OfficeManager ]]
PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null &
FIRST_INSTALL=$!
PATH="$FAKE_BIN:$PATH" "$HERE/install.sh" >/dev/null &
SECOND_INSTALL=$!
wait "$FIRST_INSTALL"
wait "$SECOND_INSTALL"
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

echo "curl installer verified install, locking, rollback, and archive safety: PASS"
