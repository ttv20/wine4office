#!/usr/bin/env bash
# Focused smoke test for separate Wine4OfficeManager release artifacts.
set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
RUNNER="$TMP/staged-runner"
MANAGER="$TMP/Wine4OfficeManager"
RELEASE="$TMP/release"
ORIGINAL="$TMP/original-runner"
VERSION=2.3.4
ROOT="wine4office-${VERSION}-x86_64"

mkdir -p "$RUNNER/bin" "$RUNNER/lib/wine/x86_64-windows" \
    "$RUNNER/share/wine4office" "$RUNNER/share/wine/gecko"
cat > "$RUNNER/bin/wine" <<'SH'
#!/bin/sh
exit 0
SH
chmod 0755 "$RUNNER/bin/wine"
printf 'runner payload\n' > "$RUNNER/lib/wine/x86_64-windows/kernel32.dll"
printf 'hidden payload\n' > "$RUNNER/.runner-metadata"
printf 'gecko x86 fixture\n' > "$RUNNER/share/wine/gecko/wine-gecko-2.47.4-x86.msi"
printf 'gecko x86_64 fixture\n' > "$RUNNER/share/wine/gecko/wine-gecko-2.47.4-x86_64.msi"
ln -s ../lib/wine/x86_64-windows/kernel32.dll "$RUNNER/bin/kernel32-link"
cat > "$MANAGER" <<'SH'
#!/bin/sh
exit 0
SH
chmod 0755 "$MANAGER"
cp -a "$RUNNER" "$ORIGINAL"

MISSING_GECKO_RUNNER="$TMP/missing-gecko-runner"
cp -a "$RUNNER" "$MISSING_GECKO_RUNNER"
rm "$MISSING_GECKO_RUNNER/share/wine/gecko/wine-gecko-2.47.4-x86_64.msi"
if "$HERE/packaging/build-release-artifacts.sh" \
    "$MISSING_GECKO_RUNNER" "$MANAGER" "$TMP/release-missing-gecko" "$VERSION" \
    "https://updates.example/releases/stable/release.json" \
    >"$TMP/missing-gecko.log" 2>&1; then
    echo "release packaging accepted a runner without x86_64 Wine Gecko" >&2
    exit 1
fi
grep -F "Runner is missing bundled Wine Gecko:" "$TMP/missing-gecko.log" >/dev/null

"$HERE/packaging/build-release-artifacts.sh" \
    "$RUNNER" "$MANAGER" "$RELEASE" "$VERSION" \
    "https://updates.example/releases/stable/release.json" "downloads" stable >/dev/null

MANAGER_NAME="Wine4OfficeManager-${VERSION}-x86_64"
WINE_NAME="${ROOT}.tar.zst"
expected_artifacts=(
    "$MANAGER_NAME"
    "$MANAGER_NAME.sha256"
    "$WINE_NAME"
    "$WINE_NAME.sha256"
    release.json
)
mapfile -t release_files < <(find "$RELEASE" -maxdepth 1 -type f -printf '%f\n' | sort)
mapfile -t expected_artifacts < <(printf '%s\n' "${expected_artifacts[@]}" | sort)
[[ ${#release_files[@]} -eq ${#expected_artifacts[@]} ]]
[[ "${release_files[*]}" == "${expected_artifacts[*]}" ]]
[[ -x "$RELEASE/$MANAGER_NAME" ]]
(
    cd "$RELEASE"
    sha256sum -c "$MANAGER_NAME.sha256" "$WINE_NAME.sha256" >/dev/null
)

mapfile -t roots < <(tar --zstd -tf "$RELEASE/$WINE_NAME" | sed 's|/.*||' | sort -u)
[[ ${#roots[@]} -eq 1 && ${roots[0]} == "$ROOT" ]]
! tar --zstd -tf "$RELEASE/$WINE_NAME" | grep -Fx "$ROOT/bin/wine64" >/dev/null
tar --zstd -tf "$RELEASE/$WINE_NAME" \
    | grep -Fx "$ROOT/share/wine/gecko/wine-gecko-2.47.4-x86.msi" >/dev/null
tar --zstd -tf "$RELEASE/$WINE_NAME" \
    | grep -Fx "$ROOT/share/wine/gecko/wine-gecko-2.47.4-x86_64.msi" >/dev/null
EXTRACTED="$TMP/extracted"
mkdir -p "$EXTRACTED"
tar --zstd -xf "$RELEASE/$WINE_NAME" -C "$EXTRACTED"

RUNNER="$RUNNER" ORIGINAL="$ORIGINAL" EXTRACTED="$EXTRACTED/$ROOT" RELEASE="$RELEASE" \
VERSION="$VERSION" MANAGER_NAME="$MANAGER_NAME" WINE_NAME="$WINE_NAME" python3 - <<'PY'
import hashlib
import json
import os
import stat
from pathlib import Path

runner = Path(os.environ["RUNNER"])
extracted = Path(os.environ["EXTRACTED"])
original = Path(os.environ["ORIGINAL"])
release_dir = Path(os.environ["RELEASE"])
version = os.environ["VERSION"]
manager_name = os.environ["MANAGER_NAME"]
wine_name = os.environ["WINE_NAME"]

def inventory(root: Path):
    result = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            result[relative] = ("symlink", mode, os.readlink(path))
        elif path.is_dir():
            result[relative] = ("directory", mode)
        else:
            result[relative] = ("file", mode, hashlib.sha256(path.read_bytes()).hexdigest())
    return result

assert inventory(runner) == inventory(original), "packaging modified the staged runner"
assert inventory(extracted) == inventory(original), "archive changed the staged runner"
metadata = json.loads((release_dir / "release.json").read_text())
assert set(metadata) == {"schema_version", "channel", "metadata_url", "manager", "wine"}
assert metadata["schema_version"] == 1
assert metadata["channel"] == "stable"
assert metadata["metadata_url"] == "https://updates.example/releases/stable/release.json"
assert metadata["manager"] == {
    "version": version,
    "url": f"downloads/{manager_name}",
    "sha256": hashlib.sha256((release_dir / manager_name).read_bytes()).hexdigest(),
    "size": (release_dir / manager_name).stat().st_size,
}
assert metadata["wine"] == {
    "version": version,
    "url": f"downloads/{wine_name}",
    "sha256": hashlib.sha256((release_dir / wine_name).read_bytes()).hexdigest(),
    "size": (release_dir / wine_name).stat().st_size,
    "format": "tar.zst",
}
PY

echo "separate manager and Wine release artifacts: PASS"
