#!/usr/bin/env bash
# Bundle the pinned DXVK release into an installed Wine4Office runner root.
#
# Usage: bundle-dxvk.sh STAGE_RUNNER_ROOT
#   STAGE_RUNNER_ROOT is the installed runner root, for example
#   "$GITHUB_WORKSPACE/stage/opt/wine4office".
#
# Only dxgi, d3d11 and d3d10core are installed (x64 and x32) into
# share/wine4office/dxvk/<version>/{x64,x32}/, together with manifest.json and
# the DXVK zlib license. Wine4Office Manager copies them into managed prefixes.
#
# Optional environment:
#   DXVK_TARBALL    use this local tarball instead of downloading it
#   DXVK_CACHE_DIR  keep the verified tarball here and reuse it next time
# Every tarball is verified against the pinned SHA-256 before it is used.
set -euo pipefail

DXVK_VERSION=3.1.1
DXVK_URL="https://github.com/doitsujin/dxvk/releases/download/v${DXVK_VERSION}/dxvk-${DXVK_VERSION}.tar.gz"
DXVK_SHA256=40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608
DXVK_DLLS=(dxgi d3d11 d3d10core)
DXVK_ARCHES=(x64 x32)

script_dir=$(cd "$(dirname "$0")" && pwd)
license=$script_dir/dxvk/LICENSE

[[ $# -eq 1 ]] || { echo "Usage: $0 STAGE_RUNNER_ROOT" >&2; exit 2; }
runner=$1
[[ -d $runner && -x $runner/bin/wine ]] || {
    echo "Not an installed Wine4Office runner root (bin/wine is missing): $runner" >&2
    exit 1
}
runner=$(cd "$runner" && pwd)
[[ -f $license ]] && grep -q 'zlib/libpng license' "$license" || {
    echo "The vendored DXVK license is missing or is not the zlib license: $license" >&2
    exit 1
}

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
tarball=$tmp/dxvk-${DXVK_VERSION}.tar.gz

verify_tarball() {
    printf '%s  %s\n' "$DXVK_SHA256" "$1" | sha256sum --check --quiet --strict - >/dev/null 2>&1
}

cached=
if [[ -n ${DXVK_CACHE_DIR:-} ]]; then
    cached=$DXVK_CACHE_DIR/dxvk-${DXVK_VERSION}.tar.gz
fi
if [[ -n ${DXVK_TARBALL:-} ]]; then
    cp -- "$DXVK_TARBALL" "$tarball"
elif [[ -n $cached && -f $cached ]] && verify_tarball "$cached"; then
    cp -- "$cached" "$tarball"
else
    curl --fail --location --retry 3 --retry-delay 2 --show-error --silent \
        --proto '=https' --output "$tarball" "$DXVK_URL"
fi
verify_tarball "$tarball" || {
    echo "DXVK ${DXVK_VERSION} tarball does not match the pinned SHA-256 ${DXVK_SHA256}" >&2
    exit 1
}
if [[ -n $cached && ! -f $cached ]]; then
    mkdir -p -- "$DXVK_CACHE_DIR"
    cp -- "$tarball" "$cached.tmp.$$"
    mv -f -- "$cached.tmp.$$" "$cached"
fi

members=()
for arch in "${DXVK_ARCHES[@]}"; do
    for dll in "${DXVK_DLLS[@]}"; do
        members+=("dxvk-${DXVK_VERSION}/${arch}/${dll}.dll")
    done
done
mkdir -p "$tmp/extract"
# Extract only the explicitly named members; nothing else in the archive is used.
tar -xzf "$tarball" -C "$tmp/extract" --no-same-owner --no-same-permissions -- "${members[@]}"

parent=$runner/share/wine4office/dxvk
destination=$parent/$DXVK_VERSION
mkdir -p -- "$parent"
staging=$(mktemp -d "$parent/.${DXVK_VERSION}.XXXXXX")
trap 'rm -rf -- "$tmp" "$staging"' EXIT

file_entries=()
for arch in "${DXVK_ARCHES[@]}"; do
    mkdir -p -- "$staging/$arch"
    for dll in "${DXVK_DLLS[@]}"; do
        source=$tmp/extract/dxvk-${DXVK_VERSION}/${arch}/${dll}.dll
        [[ -f $source && ! -L $source ]] || { echo "DXVK archive is missing $arch/$dll.dll" >&2; exit 1; }
        [[ $(head -c 2 -- "$source") == MZ ]] || { echo "DXVK $arch/$dll.dll is not a PE file" >&2; exit 1; }
        install -m 0644 -- "$source" "$staging/$arch/$dll.dll"
        digest=$(sha256sum -- "$staging/$arch/$dll.dll" | cut -d ' ' -f 1)
        file_entries+=("    \"$arch/$dll.dll\": \"$digest\"")
    done
done
install -m 0644 -- "$license" "$staging/LICENSE"
license_digest=$(sha256sum -- "$staging/LICENSE" | cut -d ' ' -f 1)

{
    printf '{\n'
    printf '  "schema": 1,\n'
    printf '  "name": "DXVK",\n'
    printf '  "version": "%s",\n' "$DXVK_VERSION"
    printf '  "source_url": "%s",\n' "$DXVK_URL"
    printf '  "source_sha256": "%s",\n' "$DXVK_SHA256"
    printf '  "license": "LICENSE",\n'
    printf '  "license_sha256": "%s",\n' "$license_digest"
    printf '  "files": {\n'
    last=$((${#file_entries[@]} - 1))
    for index in "${!file_entries[@]}"; do
        if ((index < last)); then
            printf '%s,\n' "${file_entries[$index]}"
        else
            printf '%s\n' "${file_entries[$index]}"
        fi
    done
    printf '  }\n'
    printf '}\n'
} > "$staging/manifest.json"
chmod 0644 "$staging/manifest.json"
chmod 0755 "$staging" "$staging/x64" "$staging/x32"

rm -rf -- "$destination"
mv -- "$staging" "$destination"
printf 'Bundled DXVK %s (dxgi, d3d11, d3d10core; x64 and x32) into %s\n' \
    "$DXVK_VERSION" "$destination"
