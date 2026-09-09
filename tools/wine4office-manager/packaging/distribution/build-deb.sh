#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
# Populated by validate_package_input from common.sh.
package_version=''
package_name=''
package_output=''
# shellcheck disable=SC1091
source "$here/common.sh"
validate_package_input "$@"
command -v dpkg-deb >/dev/null || { echo "dpkg-deb is required" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
root=$tmp/root
stage_package_payload "$root"
install -d "$root/DEBIAN"
installed_size=$(du -sk "$root" | awk '{print $1}')
deb_version=$package_version
if [[ $package_version == *-* ]]; then
    deb_version="${package_version%%-*}~${package_version#*-}"
fi
cat > "$root/DEBIAN/control" <<EOF
Package: $package_name
Version: $deb_version
Architecture: amd64
Maintainer: Wine4Office Project <noreply@wine4office.org>
Installed-Size: $installed_size
Depends: libc6 (>= 2.35), libgcc-s1, libgl1, libegl1, libfontconfig1, libfreetype6, libdbus-1-3, libgnutls30t64
Suggests: policykit-1
Section: otherosfs
Priority: optional
Homepage: https://github.com/ttv20/wine4office
Description: Wine and manager tuned for Microsoft Office
 Wine4Office packages the project release runner and standalone Qt manager.
 User Wine prefixes and Microsoft Office files remain outside the package.
EOF
cat > "$root/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
EOF
chmod 0755 "$root/DEBIAN/postinst"
mkdir -p "$package_output"
package_output=$(cd "$package_output" && pwd)
dpkg-deb --root-owner-group -Zzstd -z10 --threads-max=0 --build "$root" \
    "$package_output/${package_name}_${deb_version}_amd64.deb"
