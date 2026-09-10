#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 5 ]] || {
    echo "Usage: $0 MANAGER_BINARY WINE_ARCHIVE RELEASE_JSON INSTALLER OUTPUT_DIR" >&2
    exit 2
}
here=$(cd "$(dirname "$0")" && pwd)
manager=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
wine_archive=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
release_json=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
installer=$(cd "$(dirname "$4")" && pwd)/$(basename "$4")
output_dir=$5
appimagetool=${APPIMAGETOOL:-}
appimage_runtime=${APPIMAGE_RUNTIME:-}

[[ -x $manager ]] || { echo "Manager binary is not executable" >&2; exit 1; }
[[ -f $wine_archive && ! -L $wine_archive ]] || { echo "Wine archive is missing" >&2; exit 1; }
[[ -f $release_json && ! -L $release_json ]] || { echo "release.json is missing" >&2; exit 1; }
[[ -x $installer ]] || { echo "Installer is not executable" >&2; exit 1; }
[[ -n $appimagetool && -x $appimagetool ]] || {
    echo "APPIMAGETOOL must name an executable appimagetool" >&2
    exit 1
}
[[ -n $appimage_runtime && -f $appimage_runtime && ! -L $appimage_runtime ]] || {
    echo "APPIMAGE_RUNTIME must name a pinned AppImage runtime" >&2
    exit 1
}
for command in ldd python3 readlink sha256sum stat; do
    command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done
zstd_binary=$(command -v zstd) || { echo "zstd is required" >&2; exit 1; }

mapfile -t metadata < <(python3 - "$release_json" "$manager" "$wine_archive" <<'PY'
import hashlib
import json
import pathlib
import re
import sys
import urllib.parse

metadata_path, manager_path, wine_path = map(pathlib.Path, sys.argv[1:])
payload = json.loads(metadata_path.read_text(encoding="utf-8"))
if not isinstance(payload, dict) or payload.get("schema_version") != 1:
    raise SystemExit("release.json must use schema version 1")
channel = payload.get("channel")
if not isinstance(channel, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", channel):
    raise SystemExit("release.json has an invalid channel")
metadata_url = payload.get("metadata_url")
parsed = urllib.parse.urlsplit(metadata_url if isinstance(metadata_url, str) else "")
if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
    raise SystemExit("release.json has an invalid canonical metadata URL")

def verify(name, path, expected_format=None):
    component = payload.get(name)
    if not isinstance(component, dict):
        raise SystemExit(f"release.json is missing {name}")
    version = component.get("version")
    if not isinstance(version, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,127}", version):
        raise SystemExit(f"release.json has an invalid {name} version")
    if expected_format and component.get("format") != expected_format:
        raise SystemExit(f"release.json has an invalid {name} format")
    digest_builder = hashlib.sha256()
    with path.open("rb") as artifact:
        for chunk in iter(lambda: artifact.read(1024 * 1024), b""):
            digest_builder.update(chunk)
    digest = digest_builder.hexdigest()
    if component.get("sha256") != digest or component.get("size") != path.stat().st_size:
        raise SystemExit(f"{name} artifact does not match release.json")
    return version

manager_version = verify("manager", manager_path)
wine_version = verify("wine", wine_path, "tar.zst")
if manager_version != wine_version:
    raise SystemExit("Manager and Wine versions must match for the AppImage")
print(manager_version)
print(channel)
print(metadata_url)
PY
)
[[ ${#metadata[@]} -eq 3 ]] || { echo "Invalid release metadata output" >&2; exit 1; }
version=${metadata[0]}
metadata_url=${metadata[2]}
[[ $version =~ ^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$ ]] || exit 1

mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
tmp=$(mktemp -d "$output_dir/.appimage.XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
appdir=$tmp/AppDir
payload_dir=$appdir/payload
mkdir -p "$payload_dir" "$appdir/usr/bin" "$appdir/usr/lib"

install -m 0755 "$here/wine4office.AppRun" "$appdir/AppRun"
install -m 0755 "$installer" "$payload_dir/install.sh"
install -m 0755 "$manager" "$payload_dir/Wine4OfficeManager"
install -m 0644 "$wine_archive" "$payload_dir/wine.tar.zst"
install -m 0644 "$release_json" "$payload_dir/release.json"
printf '%s\n' "$version" > "$payload_dir/VERSION"
printf '%s\n' "$metadata_url" > "$payload_dir/METADATA_URL"

install -m 0644 "$here/wine4office.desktop" "$appdir/wine4office.desktop"
install -m 0644 "$here/../../icons/wine4office-manager.png" \
    "$appdir/wine4office-manager.png"
ln -s wine4office-manager.png "$appdir/.DirIcon"

install -m 0755 "$zstd_binary" "$appdir/usr/bin/zstd.real"
while read -r soname _ path _; do
    [[ -n ${path:-} && -f $path ]] || continue
    case $soname in
        libc.so.*|libdl.so.*|libpthread.so.*|librt.so.*) continue ;;
    esac
    install -m 0644 "$(readlink -f "$path")" "$appdir/usr/lib/$soname"
done < <(ldd "$zstd_binary")
cat > "$appdir/usr/bin/zstd" <<'SH'
#!/bin/sh
exec env LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$APPDIR/usr/bin/zstd.real" "$@"
SH
chmod 0755 "$appdir/usr/bin/zstd"

epoch=${SOURCE_DATE_EPOCH:-0}
[[ $epoch =~ ^[0-9]+$ ]] || { echo "SOURCE_DATE_EPOCH must be an integer" >&2; exit 1; }
find "$appdir" -print0 | xargs -0 touch -h -d "@$epoch"
output=$output_dir/Wine4Office-${version}-x86_64.AppImage
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 SOURCE_DATE_EPOCH=$epoch \
    "$appimagetool" --no-appstream --comp zstd --runtime-file "$appimage_runtime" \
        "$appdir" "$output"
chmod 0755 "$output"
(cd "$output_dir" && sha256sum "$(basename "$output")" > "$(basename "$output").sha256")
printf 'Created Wine4Office AppImage: %s\n' "$output"
