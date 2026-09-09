#!/usr/bin/env bash
set -euo pipefail
[[ $# -ge 3 ]] || {
    echo "Usage: $0 REPOSITORY_DIR CODENAME DEB..." >&2
    exit 2
}
repository=$1
codename=$2
shift 2
[[ $codename =~ ^[a-z0-9][a-z0-9.-]*$ ]] || { echo "Invalid codename" >&2; exit 1; }
command -v apt-ftparchive >/dev/null || { echo "apt-ftparchive is required" >&2; exit 1; }

pool=$repository/pool/main/w/wine4office
binary=$repository/dists/$codename/main/binary-amd64
mkdir -p "$pool" "$binary"
for package in "$@"; do
    [[ -f $package && $package == *.deb ]] || { echo "Invalid DEB: $package" >&2; exit 1; }
    install -m 0644 "$package" "$pool/$(basename "$package")"
done
rm -f -- "$repository/dists/$codename/InRelease" \
    "$repository/dists/$codename/Release.gpg"
(
    cd "$repository"
    apt-ftparchive packages pool/main > "dists/$codename/main/binary-amd64/Packages"
    gzip -n -9 -c "dists/$codename/main/binary-amd64/Packages" \
        > "dists/$codename/main/binary-amd64/Packages.gz"
    apt-ftparchive \
        -o APT::FTPArchive::Release::Origin=Wine4Office \
        -o APT::FTPArchive::Release::Label=Wine4Office \
        -o APT::FTPArchive::Release::Suite="$codename" \
        -o APT::FTPArchive::Release::Codename="$codename" \
        -o APT::FTPArchive::Release::Architectures=amd64 \
        -o APT::FTPArchive::Release::Components=main \
        release "dists/$codename" > "dists/$codename/Release"
)
if [[ -n ${WINE4OFFICE_GPG_KEY_ID:-} ]]; then
    gpg --batch --yes --local-user "$WINE4OFFICE_GPG_KEY_ID" \
        --detach-sign --armor --output "$repository/dists/$codename/Release.gpg" \
        "$repository/dists/$codename/Release"
    gpg --batch --yes --local-user "$WINE4OFFICE_GPG_KEY_ID" \
        --clearsign --output "$repository/dists/$codename/InRelease" \
        "$repository/dists/$codename/Release"
fi
