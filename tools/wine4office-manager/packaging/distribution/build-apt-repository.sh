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
for package in "$@"; do
    [[ -f $package && $package == *.deb ]] || { echo "Invalid DEB: $package" >&2; exit 1; }
done

mkdir -p "$repository"
repository=$(cd "$repository" && pwd)
pool=$repository/pool/main/w/wine4office
distributions=$repository/dists
live_distribution=$distributions/$codename
mkdir -p "$pool" "$distributions"
staged_repository=$(mktemp -d "$repository/.apt-repository.XXXXXX")
staged_pool=$staged_repository/pool/main/w/wine4office
staged_distribution=$staged_repository/dists/$codename
backup_distribution=
package_commit_temp=
committed_new_packages=()
cleanup() {
    local status=$?
    [[ -z ${package_commit_temp:-} || ! -e $package_commit_temp ]] || \
        rm -f -- "$package_commit_temp"
    [[ -z ${staged_repository:-} || ! -e $staged_repository ]] || \
        rm -rf -- "$staged_repository"
    if ((status != 0)) && [[ -n ${backup_distribution:-} \
            && -e $backup_distribution && ! -e $live_distribution ]]; then
        mv -- "$backup_distribution" "$live_distribution"
    fi
    if ((status != 0)); then
        rm -f -- "${committed_new_packages[@]}"
    fi
}
trap cleanup EXIT
mkdir -p "$staged_pool" "$staged_repository/dists"
if [[ -d $pool ]]; then
    cp -al "$pool/." "$staged_pool/"
fi
for package in "$@"; do
    staged_package=$staged_pool/$(basename "$package")
    rm -f -- "$staged_package"
    install -m 0644 "$package" "$staged_package"
    live_package=$pool/$(basename "$package")
    if [[ -e $live_package ]] && ! cmp -s "$staged_package" "$live_package"; then
        echo "Refusing to replace published DEB; use a new package version: $live_package" >&2
        exit 1
    fi
done
if [[ -e $live_distribution ]]; then
    mkdir -p "$staged_distribution"
    cp -a "$live_distribution/." "$staged_distribution/"
fi
binary=$staged_distribution/main/binary-amd64
mkdir -p "$binary"
(
    cd "$staged_repository"
    apt-ftparchive packages pool/main > "$binary/Packages"
    gzip -n -9 -c "$binary/Packages" > "$binary/Packages.gz"
    apt-ftparchive \
        -o APT::FTPArchive::Release::Origin=Wine4Office \
        -o APT::FTPArchive::Release::Label=Wine4Office \
        -o APT::FTPArchive::Release::Suite="$codename" \
        -o APT::FTPArchive::Release::Codename="$codename" \
        -o APT::FTPArchive::Release::Architectures=amd64 \
        -o APT::FTPArchive::Release::Components=main \
        release "$staged_distribution" > "$staged_distribution/Release"
)
if [[ -n ${WINE4OFFICE_GPG_KEY_ID:-} ]]; then
    gpg --batch --yes --local-user "$WINE4OFFICE_GPG_KEY_ID" \
        --detach-sign --armor --output "$staged_distribution/Release.gpg" \
        "$staged_distribution/Release"
    gpg --batch --yes --local-user "$WINE4OFFICE_GPG_KEY_ID" \
        --clearsign --output "$staged_distribution/InRelease" \
        "$staged_distribution/Release"
else
    rm -f -- "$staged_distribution/InRelease" "$staged_distribution/Release.gpg"
fi

for package in "$@"; do
    package_name=$(basename "$package")
    if [[ ! -e $pool/$package_name ]]; then
        package_commit_temp=$(mktemp "$pool/.${package_name}.XXXXXX")
        install -m 0644 "$staged_pool/$package_name" "$package_commit_temp"
        mv -- "$package_commit_temp" "$pool/$package_name"
        package_commit_temp=
        committed_new_packages+=("$pool/$package_name")
    fi
done
if [[ -e $live_distribution ]]; then
    backup_distribution=$(mktemp -d "$distributions/.${codename}.backup.XXXXXX")
    rmdir "$backup_distribution"
    mv -- "$live_distribution" "$backup_distribution"
fi
if ! mv -- "$staged_distribution" "$live_distribution"; then
    [[ -z $backup_distribution || ! -e $backup_distribution ]] || \
        mv -- "$backup_distribution" "$live_distribution"
    exit 1
fi
committed_new_packages=()
rm -rf -- "$staged_repository"
staged_repository=
if [[ -n $backup_distribution ]]; then
    rm -rf -- "$backup_distribution"
    backup_distribution=
fi
