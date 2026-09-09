#!/usr/bin/env python3
"""Generate fixed-hash AUR and Nix sources from a Wine4Office release manifest."""

import argparse
import base64
import hashlib
import json
import re
import shutil
import urllib.parse
from pathlib import Path


def shell_single_quote(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def nix_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace(
        "${", "\\${"
    ) + '"'


def validated_release(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("release.json schema_version must be 1")
    version = data.get("manager", {}).get("version")
    if (not isinstance(version, str)
            or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,127}", version)
            or data.get("wine", {}).get("version") != version):
        raise ValueError("manager and Wine must use the same valid product version")
    metadata_url = data.get("metadata_url")
    if not isinstance(metadata_url, str):
        raise ValueError("metadata_url must use HTTPS")
    parsed = urllib.parse.urlsplit(metadata_url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.fragment):
        raise ValueError("metadata_url must use HTTPS")
    for name in ("manager", "wine"):
        component = data.get(name)
        if not isinstance(component, dict) or not isinstance(component.get("url"), str):
            raise ValueError(f"{name} release entry is invalid")
        url = urllib.parse.urljoin(metadata_url, component["url"])
        parsed = urllib.parse.urlsplit(url)
        if (parsed.scheme != "https" or not parsed.hostname or parsed.username is not None
                or parsed.password is not None or parsed.fragment
                or any(ord(character) < 0x21 or ord(character) == 0x7f for character in url)):
            raise ValueError(f"{name} URL must use HTTPS")
        if not re.fullmatch(r"[0-9a-f]{64}", component["sha256"]):
            raise ValueError(f"{name} SHA-256 is invalid")
        component["resolved_url"] = url
    base_version = data["wine"].get("base_version", "unknown")
    if (base_version != "unknown" and (not isinstance(base_version, str)
            or not re.fullmatch(r"[0-9]+(?:[.][0-9A-Za-z]+)+", base_version))):
        raise ValueError("Wine base version is invalid")
    return data


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate_aur(release: dict, output: Path, distribution: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    desktop = distribution / "wine4office.desktop"
    icon = distribution.parent.parent / "icons/wine4office-manager.png"
    shutil.copyfile(desktop, output / desktop.name)
    shutil.copyfile(icon, output / "wine4office-manager.png")
    version = release["manager"]["version"]
    base_version = release["wine"].get("base_version", "unknown")
    pkgver = version.replace("-", "_")
    manager_url = release["manager"]["resolved_url"]
    wine_url = release["wine"]["resolved_url"]
    manager_source = shell_single_quote(f"Wine4OfficeManager::{manager_url}")
    wine_source = shell_single_quote(f"wine4office-runner::{wine_url}")
    pkgbuild = f'''# Generated from release.json. Do not edit hashes by hand.
pkgname=wine4office-bin
pkgver={pkgver}
_release_version={version}
pkgrel=1
pkgdesc="Wine and manager tuned for Microsoft Office"
arch=('x86_64')
url="https://github.com/ttv20/wine4office"
license=('LGPL-2.1-or-later')
depends=('glibc' 'gcc-libs' 'glib2' 'libxkbcommon' 'libxkbcommon-x11'
         'libxcb' 'xcb-util' 'xcb-util-cursor' 'xcb-util-image'
         'xcb-util-keysyms' 'xcb-util-renderutil' 'xcb-util-wm'
         'libglvnd' 'fontconfig' 'freetype2' 'dbus' 'gnutls' 'krb5')
optdepends=('polkit: install updates from the Manager'
            'libx11: X11 display driver'
            'wayland: native Wayland display driver'
            'vulkan-icd-loader: Vulkan support')
provides=('wine4office')
conflicts=('wine4office')
options=('!strip')
source=('wine4office.desktop' 'wine4office-manager.png')
source_x86_64=({manager_source}
               {wine_source})
sha256sums=('{file_sha256(desktop)}'
            '{file_sha256(icon)}')
sha256sums_x86_64=('{release["manager"]["sha256"]}'
                   '{release["wine"]["sha256"]}')

package() {{
    install -d "$pkgdir/opt/wine4office/bin" "$pkgdir/opt/wine4office/runner" \
        "$pkgdir/usr/bin" "$pkgdir/usr/share/applications" \
        "$pkgdir/usr/share/icons/hicolor/256x256/apps"
    install -m755 "$srcdir/Wine4OfficeManager" \
        "$pkgdir/opt/wine4office/bin/Wine4OfficeManager"
    local runner_root
    runner_root=$(find "$srcdir" -maxdepth 1 -type d -name 'wine4office-*-x86_64' -print -quit)
    [[ -n $runner_root && -x $runner_root/bin/wine ]]
    cp -a "$runner_root/." "$pkgdir/opt/wine4office/runner/"
    ln -s /opt/wine4office/bin/Wine4OfficeManager "$pkgdir/usr/bin/Wine4OfficeManager"
    ln -s /opt/wine4office/runner/bin/wine "$pkgdir/usr/bin/wine4office"
    install -m644 "$srcdir/wine4office.desktop" \
        "$pkgdir/usr/share/applications/wine4office.desktop"
    install -m644 "$srcdir/wine4office-manager.png" \
        "$pkgdir/usr/share/icons/hicolor/256x256/apps/wine4office-manager.png"
    printf 'Wine4OfficeManager\\n' > "$pkgdir/opt/wine4office/STANDALONE"
    printf '%s\\n' "$_release_version" > "$pkgdir/opt/wine4office/VERSION"
    printf '%s\\n' "$_release_version" > "$pkgdir/opt/wine4office/WINE_VERSION"
    printf '%s\\n' "{base_version}" > "$pkgdir/opt/wine4office/WINE_BASE_VERSION"
    printf 'stable\\n' > "$pkgdir/opt/wine4office/UPDATE_CHANNEL"
    cat > "$pkgdir/opt/wine4office/PACKAGE-INSTALLATION.json" <<'EOF'
{{
  "schema_version": 1,
  "provider": "aur",
  "package": "wine4office-bin",
  "components": ["manager", "wine"]
}}
EOF
}}
'''
    (output / "PKGBUILD").write_text(pkgbuild, encoding="utf-8")
    srcinfo = f'''pkgbase = wine4office-bin
\tpkgdesc = Wine and manager tuned for Microsoft Office
\tpkgver = {pkgver}
\tpkgrel = 1
\turl = https://github.com/ttv20/wine4office
\tarch = x86_64
\tlicense = LGPL-2.1-or-later
\tdepends = glibc
\tdepends = gcc-libs
\tdepends = glib2
\tdepends = libxkbcommon
\tdepends = libxkbcommon-x11
\tdepends = libxcb
\tdepends = xcb-util
\tdepends = xcb-util-cursor
\tdepends = xcb-util-image
\tdepends = xcb-util-keysyms
\tdepends = xcb-util-renderutil
\tdepends = xcb-util-wm
\tdepends = libglvnd
\tdepends = fontconfig
\tdepends = freetype2
\tdepends = dbus
\tdepends = gnutls
\tdepends = krb5
\toptdepends = polkit: install updates from the Manager
\toptdepends = libx11: X11 display driver
\toptdepends = wayland: native Wayland display driver
\toptdepends = vulkan-icd-loader: Vulkan support
\tprovides = wine4office
\tconflicts = wine4office
\toptions = !strip
\tsource = wine4office.desktop
\tsource = wine4office-manager.png
\tsha256sums = {file_sha256(desktop)}
\tsha256sums = {file_sha256(icon)}
\tsource_x86_64 = Wine4OfficeManager::{manager_url}
\tsource_x86_64 = wine4office-runner::{wine_url}
\tsha256sums_x86_64 = {release["manager"]["sha256"]}
\tsha256sums_x86_64 = {release["wine"]["sha256"]}

pkgname = wine4office-bin
'''
    (output / ".SRCINFO").write_text(srcinfo, encoding="utf-8")


def sri(hex_digest: str) -> str:
    return "sha256-" + base64.b64encode(bytes.fromhex(hex_digest)).decode("ascii")


def generate_nix(release: dict, output: Path, distribution: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(distribution / "nix/flake.nix", output / "flake.nix")
    shutil.copyfile(distribution / "nix/package.nix", output / "package.nix")
    data = {
        "version": release["manager"]["version"],
        "managerUrl": release["manager"]["resolved_url"],
        "managerHash": sri(release["manager"]["sha256"]),
        "wineUrl": release["wine"]["resolved_url"],
        "wineHash": sri(release["wine"]["sha256"]),
        "wineBaseVersion": release["wine"].get("base_version", "unknown"),
    }
    lines = ["{"]
    for key, value in data.items():
        lines.append(f"  {key} = {nix_string(value)};")
    lines.append("}")
    (output / "release.nix").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("release_json", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    release = validated_release(args.release_json)
    distribution = Path(__file__).resolve().parent
    generate_aur(release, args.output / "aur", distribution)
    generate_nix(release, args.output / "nix", distribution)


if __name__ == "__main__":
    main()
