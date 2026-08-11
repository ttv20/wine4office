#!/usr/bin/env bash
# Package Wine4Office Manager and an unmodified Wine runner as separate release artifacts.
set -euo pipefail

usage() {
    echo "Usage: $0 RUNNER_DIR MANAGER_BINARY OUTPUT_DIR VERSION METADATA_URL [RELEASE_BASE_URL] [CHANNEL]" >&2
    exit 2
}
[[ $# -ge 5 && $# -le 7 ]] || usage

RUNNER=$(cd "$1" && pwd)
MANAGER=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
OUTPUT_DIR=$3
VERSION=$4
METADATA_URL=$5
RELEASE_BASE_URL=${6:-}
CHANNEL=${7:-stable}

[[ -x "$RUNNER/bin/wine" ]] || { echo "Runner has no executable bin/wine: $RUNNER" >&2; exit 1; }
[[ -x "$MANAGER" ]] || { echo "Wine4Office Manager binary is missing or not executable: $MANAGER" >&2; exit 1; }
GECKO_VERSION=2.47.4
for gecko_arch in x86 x86_64; do
    gecko="$RUNNER/share/wine/gecko/wine-gecko-${GECKO_VERSION}-${gecko_arch}.msi"
    [[ -f "$gecko" ]] || { echo "Runner is missing bundled Wine Gecko: $gecko" >&2; exit 1; }
done
MONO_VERSION=11.2.0
mono="$RUNNER/share/wine/mono/wine-mono-${MONO_VERSION}-x86.msi"
[[ -f "$mono" ]] || { echo "Runner is missing bundled Wine Mono: $mono" >&2; exit 1; }
[[ $VERSION =~ ^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$ ]] || { echo "Unsafe version: $VERSION" >&2; exit 1; }
[[ $CHANNEL =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || { echo "Unsafe channel: $CHANNEL" >&2; exit 1; }
command -v zstd >/dev/null || { echo "zstd is required" >&2; exit 1; }

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
case "$OUTPUT_DIR/" in
    "$RUNNER/"*) echo "Output directory must not be inside the staged runner." >&2; exit 1 ;;
esac

MANAGER_NAME="Wine4OfficeManager-${VERSION}-x86_64"
WINE_ROOT="wine4office-${VERSION}-x86_64"
WINE_NAME="${WINE_ROOT}.tar.zst"
TMP=$(mktemp -d "$OUTPUT_DIR/.release.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

install -m 0755 "$MANAGER" "$TMP/$MANAGER_NAME"

# Transform archive member and hard-link names. Keep symlink targets and every staged
# runner file byte-for-byte and metadata-for-metadata as installed by Wine.
tar -C "$RUNNER" \
    --format=gnu --sort=name \
    --transform="flags=rhS;s|^\\.$|$WINE_ROOT|;s|^\\./|$WINE_ROOT/|" \
    -cf - . | zstd -T0 -19 --long=27 --no-progress -o "$TMP/$WINE_NAME"

mapfile -t roots < <(tar --zstd -tf "$TMP/$WINE_NAME" | sed 's|/.*||' | sort -u)
[[ ${#roots[@]} -eq 1 && ${roots[0]} == "$WINE_ROOT" ]] || {
    echo "Wine archive must contain exactly the versioned root $WINE_ROOT" >&2
    exit 1
}
tar --zstd -tf "$TMP/$WINE_NAME" | grep -Fx "$WINE_ROOT/bin/wine" >/dev/null || {
    echo "Wine archive is missing $WINE_ROOT/bin/wine" >&2
    exit 1
}
for gecko_arch in x86 x86_64; do
    tar --zstd -tf "$TMP/$WINE_NAME" \
        | grep -Fx "$WINE_ROOT/share/wine/gecko/wine-gecko-${GECKO_VERSION}-${gecko_arch}.msi" >/dev/null || {
        echo "Wine archive is missing Wine Gecko for $gecko_arch" >&2
        exit 1
    }
done
tar --zstd -tf "$TMP/$WINE_NAME" \
    | grep -Fx "$WINE_ROOT/share/wine/mono/wine-mono-${MONO_VERSION}-x86.msi" >/dev/null || {
    echo "Wine archive is missing Wine Mono" >&2
    exit 1
}

(
    cd "$TMP"
    for artifact in "$MANAGER_NAME" "$WINE_NAME"; do
        sha256sum "$artifact" > "$artifact.sha256"
    done
)

VERSION="$VERSION" CHANNEL="$CHANNEL" METADATA_URL="$METADATA_URL" \
RELEASE_BASE_URL="$RELEASE_BASE_URL" MANAGER_NAME="$MANAGER_NAME" WINE_NAME="$WINE_NAME" \
MANAGER_SHA256="$(sha256sum "$TMP/$MANAGER_NAME" | cut -d ' ' -f 1)" \
MANAGER_SIZE="$(stat -c '%s' "$TMP/$MANAGER_NAME")" \
WINE_SHA256="$(sha256sum "$TMP/$WINE_NAME" | cut -d ' ' -f 1)" \
WINE_SIZE="$(stat -c '%s' "$TMP/$WINE_NAME")" \
python3 - "$TMP/release.json" <<'PY'
import json
import os
import sys
import urllib.parse

metadata_url = os.environ["METADATA_URL"]
metadata = urllib.parse.urlsplit(metadata_url)
if metadata.scheme != "https" or not metadata.hostname or metadata.username or metadata.password or metadata.fragment:
    raise SystemExit("METADATA_URL must be a canonical HTTPS URL without credentials or a fragment.")

base = os.environ["RELEASE_BASE_URL"].strip()
if base:
    parsed_base = urllib.parse.urlsplit(base)
    if parsed_base.scheme:
        if (parsed_base.scheme != "https" or not parsed_base.hostname or parsed_base.username
                or parsed_base.password or parsed_base.query or parsed_base.fragment):
            raise SystemExit("RELEASE_BASE_URL must be HTTPS and cannot contain credentials, a query, or a fragment.")
    elif parsed_base.netloc or parsed_base.query or parsed_base.fragment:
        raise SystemExit("Relative RELEASE_BASE_URL cannot contain an authority, query, or fragment.")

def artifact_url(name: str) -> str:
    encoded_name = urllib.parse.quote(name, safe="-._~")
    return f"{base.rstrip('/')}/{encoded_name}" if base else encoded_name

release = {
    "schema_version": 1,
    "channel": os.environ["CHANNEL"],
    "metadata_url": metadata_url,
    "manager": {
        "version": os.environ["VERSION"],
        "url": artifact_url(os.environ["MANAGER_NAME"]),
        "sha256": os.environ["MANAGER_SHA256"],
        "size": int(os.environ["MANAGER_SIZE"]),
    },
    "wine": {
        "version": os.environ["VERSION"],
        "url": artifact_url(os.environ["WINE_NAME"]),
        "sha256": os.environ["WINE_SHA256"],
        "size": int(os.environ["WINE_SIZE"]),
        "format": "tar.zst",
    },
}
with open(sys.argv[1], "w", encoding="utf-8") as destination:
    json.dump(release, destination, indent=2)
    destination.write("\n")
PY

for artifact in "$MANAGER_NAME" "$MANAGER_NAME.sha256" "$WINE_NAME" "$WINE_NAME.sha256" release.json; do
    mv "$TMP/$artifact" "$OUTPUT_DIR/$artifact"
done
printf 'Created Wine4Office Manager release artifacts in %s\n' "$OUTPUT_DIR"
