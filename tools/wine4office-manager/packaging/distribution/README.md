# Distribution packages

Wine4Office publishes one combined `wine4office` package. It installs the exact
standalone Manager and Wine runner produced by the release workflow. The package
adds only system integration and `PACKAGE-INSTALLATION.json`; it does not rebuild
or modify either release payload.

The package layout is `/opt/wine4office`, with command links in `/usr/bin`.
User prefixes, Office installations, configuration, and documents stay in the
user's home directory and package removal does not delete them.

`build-deb.sh` and `build-rpm.sh` consume the two release artifacts. The release
workflow runs each builder in its matching distribution container. AUR and Nix
sources are generated from `release.json`, so their URLs and SHA-256 hashes are
fixed to one release.

APT and RPM repositories are static HTTPS directory trees. Run
`build-apt-repository.sh` or `build-rpm-repository.sh` with
`WINE4OFFICE_GPG_KEY_ID` set to sign production metadata. Publish the resulting
tree atomically under `https://packages.wine4office.org/`. Until a package server
and signing key exist, CI uploads the packages and unsigned repository trees as
release artifacts for testing only.

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
PyInstaller one-file Manager extracts embedded ELF libraries at runtime.

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
