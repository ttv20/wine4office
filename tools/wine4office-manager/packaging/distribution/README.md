# Distribution packages

Wine4Office publishes one combined `wine4office` package. It installs the exact
standalone Manager and Wine runner produced by the release workflow. The package
adds only system integration and `PACKAGE-INSTALLATION.json`; it does not rebuild
or modify either release payload.

The package layout is `/opt/wine4office`, with command links in `/usr/bin`.
User prefixes, Office installations, configuration, and documents stay in the
user's home directory and package removal does not delete them.

`build-deb.sh`, `build-rpm.sh`, and `build-appimage.sh` consume the same release
artifacts. The release workflow runs the DEB and RPM builders in matching
distribution containers. AUR and Nix sources are generated from `release.json`,
so their URLs and SHA-256 hashes are fixed to one release.

The AppImage is a single-file installer. Its first launch verifies and installs
the bundled Manager and Wine runner under `~/.local/share/wine4office`, then runs
the installed Manager. Later launches reuse that durable installation. Office
shortcuts, Wine services, and updates therefore keep working if the downloaded
AppImage is moved or removed. The installed copy follows the existing standalone
release feed and does not claim package-manager ownership.

Build an AppImage with the pinned `appimagetool-uruntime` used by the release
workflow:

```bash
APPIMAGETOOL=/path/to/appimagetool-x86_64.AppImage \
APPIMAGE_RUNTIME=/path/to/uruntime-appimage-squashfs-lite-x86_64 \
tools/wine4office-manager/packaging/distribution/build-appimage.sh \
    release/Wine4OfficeManager-VERSION-x86_64 \
    release/wine4office-VERSION-x86_64.tar.zst \
    release/release.json release/install.sh release
```

APT and RPM repositories are static HTTPS directory trees. Run
`build-apt-repository.sh` or `build-rpm-repository.sh` with
`WINE4OFFICE_GPG_KEY_ID` set to sign production metadata. Publish the resulting
tree atomically under `https://packages.wine4office.org/`. Until a package server
and signing key exist, CI uploads the packages and unsigned repository trees as
release artifacts for testing only.

Published package filenames are immutable. Rebuilds with different bytes must
use a new DEB version or RPM release so existing repository metadata never
points to replaced content. Enabling RPM package signing for a previously
unsigned filename also requires a new RPM release.

Packaged installations use the distribution package source as their update
authority. The Manager hides the standalone metadata URL, prerelease selector,
and background release-feed controls. APT and DNF updates run as one combined
Manager-and-Wine transaction; the Manager compares the installed versions before
and after the transaction and runs Wine migration only when both components
actually advanced. AUR and Nix show copyable update instructions.

Suggested production paths:

```text
packages.wine4office.org/apt/dists/stable/...
packages.wine4office.org/apt/pool/...
packages.wine4office.org/rpm/x86_64/...
packages.wine4office.org/keys/wine4office-packages.asc
```

Ubuntu users add `deb [signed-by=/usr/share/keyrings/wine4office.asc]
https://packages.wine4office.org/apt stable main`. Fedora users install
`wine4office.repo`. AUR remains hosted by the AUR and downloads immutable GitHub
release assets. The generated Nix flake uses an FHS environment because the
PyInstaller one-file Manager extracts embedded ELF libraries at runtime. Its
wrapper is also embedded into generated Office shortcuts and user services so
later launches re-enter that environment. Install the flake into a Nix profile
or NixOS configuration before creating persistent shortcuts; a transient
`nix run` store path is not a durable installation location.

The repository trees need only static HTTPS hosting; no package-specific web
application or database is required. They can be served by the same Nginx,
object-storage bucket, or CDN as the project website. A production APT or DNF
update requires the published tree, a stable URL, and a signing key. AUR and Nix
packages fetch immutable GitHub release assets and do not require this package
server.

The release runner is built on Ubuntu 24.04 and reused byte-for-byte in every
package. This keeps the package set small and makes the Manager and core Wine
runner portable, but some optional Unix-side Wine modules link to exact library
ABIs. For example, a runner linked to `libavcodec.so.60` cannot load that media
module on a Fedora release that provides only a newer FFmpeg soname. The RPM
therefore declares only the core runtime dependencies and disables automatic ELF
dependency generation. Full optional-module parity across distributions would
require bundling those ABI-sensitive libraries or rebuilding the Unix modules
for each distribution.
