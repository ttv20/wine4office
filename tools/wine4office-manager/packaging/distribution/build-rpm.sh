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
command -v rpmbuild >/dev/null || { echo "rpmbuild is required" >&2; exit 1; }

rpm_version=${package_version%%-*}
rpm_release=1
if [[ $package_version == *-* ]]; then
    suffix=${package_version#*-}
    suffix=${suffix//-/.}
    [[ $suffix =~ ^[A-Za-z0-9.]+$ ]] || { echo "Invalid RPM prerelease" >&2; exit 1; }
    rpm_release="0.${suffix}.1"
fi
[[ $rpm_version =~ ^[0-9]+([.][0-9A-Za-z]+)*$ ]] || {
    echo "Version cannot be represented as an RPM version" >&2; exit 1;
}

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
payload=$tmp/payload
stage_package_payload "$payload"
mkdir -p "$tmp/rpmbuild"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
tar -C "$payload" --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -czf "$tmp/rpmbuild/SOURCES/payload.tar.gz" .
cat > "$tmp/rpmbuild/SPECS/wine4office.spec" <<EOF
%global debug_package %{nil}
%global __os_install_post %{nil}
%global _binary_payload w10T0.zstdio
Name:           $package_name
Version:        $rpm_version
Release:        $rpm_release%{?dist}
Summary:        Wine and manager tuned for Microsoft Office
License:        LGPL-2.1-or-later
URL:            https://github.com/ttv20/wine4office
Source0:        payload.tar.gz
BuildArch:      x86_64
AutoReqProv:    no
Requires:       glibc, libglvnd-glx, libglvnd-egl, fontconfig, freetype, dbus-libs, gnutls

%description
Wine4Office packages the project release runner and standalone Qt manager.
User Wine prefixes and Microsoft Office files remain outside the package.

%prep

%build

%install
mkdir -p %{buildroot}
tar -xzf %{SOURCE0} -C %{buildroot}

%files
/opt/wine4office
/usr/bin/Wine4OfficeManager
/usr/bin/wine4office
/usr/share/applications/wine4office.desktop
/usr/share/icons/hicolor/256x256/apps/wine4office-manager.png

%changelog
* Wed Sep 09 2026 Wine4Office Project <noreply@wine4office.org> - $rpm_version-$rpm_release
- Package Wine4Office release $package_version
EOF
rpmbuild -bb --define "_topdir $tmp/rpmbuild" \
    --define "_build_id_links none" "$tmp/rpmbuild/SPECS/wine4office.spec"
mkdir -p "$package_output"
find "$tmp/rpmbuild/RPMS" -type f -name '*.rpm' \
    -exec install -m 0644 {} "$package_output/" \;
