#!/usr/bin/env bash
set -euo pipefail

distribution_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

validate_package_input() {
    [[ $# -eq 6 ]] || {
        echo "Usage: $0 MANAGER_BINARY WINE_ARCHIVE VERSION PROVIDER PACKAGE OUTPUT" >&2
        exit 2
    }
    manager_binary=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
    wine_archive=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
    package_version=$3
    package_provider=$4
    package_name=$5
    # shellcheck disable=SC2034 # Consumed by the sourcing package builder.
    package_output=$6
    [[ -x "$manager_binary" ]] || { echo "Manager binary is not executable" >&2; exit 1; }
    [[ -f "$wine_archive" ]] || { echo "Wine archive is missing" >&2; exit 1; }
    [[ $package_version =~ ^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$ ]] || {
        echo "Invalid package version" >&2; exit 1;
    }
    [[ $package_provider =~ ^(apt|dnf|aur|nix)$ ]] || {
        echo "Invalid package provider" >&2; exit 1;
    }
    [[ $package_name =~ ^[A-Za-z0-9][A-Za-z0-9+_.-]{0,127}$ ]] || {
        echo "Invalid package name" >&2; exit 1;
    }
}

stage_package_payload() {
    local destination=$1 extraction root wine_base_version wine_build_id
    local wine_build_prefix candidate
    extraction=$(mktemp -d)
    tar --zstd -xf "$wine_archive" -C "$extraction"
    mapfile -t roots < <(find "$extraction" -mindepth 1 -maxdepth 1 -type d -print)
    [[ ${#roots[@]} -eq 1 && -x ${roots[0]}/bin/wine ]] || {
        echo "Wine archive must contain one runner with bin/wine" >&2
        exit 1
    }
    root=${roots[0]}
    install -d "$destination/opt/wine4office/bin" \
        "$destination/opt/wine4office/runner" \
        "$destination/usr/bin" \
        "$destination/usr/share/applications" \
        "$destination/usr/share/icons/hicolor/256x256/apps"
    install -m 0755 "$manager_binary" \
        "$destination/opt/wine4office/bin/Wine4OfficeManager"
    cp -a "$root/." "$destination/opt/wine4office/runner/"
    wine_base_version=unknown
    wine_build_id=$("$root/bin/wine" --version 2>/dev/null || true)
    wine_build_prefix="wine4office-${package_version} (Wine "
    if [[ $wine_build_id == "$wine_build_prefix"*')' ]]; then
        candidate=${wine_build_id#"$wine_build_prefix"}
        candidate=${candidate%')'}
        if [[ $candidate =~ ^[0-9]+([.][0-9A-Za-z]+)+([-+][0-9A-Za-z][0-9A-Za-z.-]*)?$ ]]; then
            wine_base_version=$candidate
        fi
    fi
    ln -s /opt/wine4office/bin/Wine4OfficeManager \
        "$destination/usr/bin/Wine4OfficeManager"
    ln -s /opt/wine4office/runner/bin/wine "$destination/usr/bin/wine4office"
    install -m 0644 "$distribution_dir/wine4office.desktop" \
        "$destination/usr/share/applications/wine4office.desktop"
    install -m 0644 "$distribution_dir/../../icons/wine4office-manager.png" \
        "$destination/usr/share/icons/hicolor/256x256/apps/wine4office-manager.png"
    printf 'Wine4OfficeManager\n' > "$destination/opt/wine4office/STANDALONE"
    printf '%s\n' "$package_version" > "$destination/opt/wine4office/VERSION"
    printf '%s\n' "$package_version" > "$destination/opt/wine4office/WINE_VERSION"
    printf '%s\n' "$wine_base_version" > "$destination/opt/wine4office/WINE_BASE_VERSION"
    printf 'stable\n' > "$destination/opt/wine4office/UPDATE_CHANNEL"
    python3 - "$destination/opt/wine4office/PACKAGE-INSTALLATION.json" \
        "$package_provider" "$package_name" <<'PY'
import json
import sys

with open(sys.argv[1], "w", encoding="utf-8") as target:
    json.dump({
        "schema_version": 1,
        "provider": sys.argv[2],
        "package": sys.argv[3],
        "components": ["manager", "wine"],
    }, target, indent=2)
    target.write("\n")
PY
    rm -rf -- "$extraction"
}
