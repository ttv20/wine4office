#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
[[ $# -ge 2 ]] || {
    echo "Usage: $0 REPOSITORY_DIR RPM..." >&2
    exit 2
}
repository=$1
shift
command -v createrepo_c >/dev/null || { echo "createrepo_c is required" >&2; exit 1; }
for package in "$@"; do
    [[ -f $package && $package == *.rpm ]] || { echo "Invalid RPM: $package" >&2; exit 1; }
done

mkdir -p "$repository"
repository=$(cd "$repository" && pwd)
repository_root=$repository/rpm/x86_64
packages=$repository_root/Packages
source_digests=$repository/rpm/.wine4office-source-sha256
mkdir -p "$packages" "$source_digests"
install -m 0644 "$here/wine4office.repo" "$repository/wine4office.repo"
staged_repository=$(mktemp -d "$repository_root/.repository.XXXXXX")
staged_packages=$staged_repository/Packages
backup_metadata=
package_commit_temp=
committed_new_packages=()
declare -A package_source_digests=()
cleanup() {
    local status=$?
    [[ -z ${package_commit_temp:-} || ! -e $package_commit_temp ]] || \
        rm -f -- "$package_commit_temp"
    [[ -z ${staged_repository:-} || ! -e $staged_repository ]] || \
        rm -rf -- "$staged_repository"
    if ((status != 0)) && [[ -n ${backup_metadata:-} && -e $backup_metadata \
            && ! -e $repository_root/repodata ]]; then
        mv -- "$backup_metadata" "$repository_root/repodata"
    fi
    if ((status != 0)); then
        rm -f -- "${committed_new_packages[@]}"
    fi
}
trap cleanup EXIT
mkdir -p "$staged_packages"
if [[ -d $packages ]]; then
    cp -al "$packages/." "$staged_packages/"
fi
if [[ -d $repository_root/repodata ]]; then
    cp -a "$repository_root/repodata" "$staged_repository/repodata"
fi
for package in "$@"; do
    package_name=$(basename "$package")
    staged_package=$staged_packages/$package_name
    live_package=$packages/$package_name
    source_digest=$(sha256sum "$package" | cut -d ' ' -f 1)
    package_source_digests["$package_name"]=$source_digest
    rm -f -- "$staged_package"
    source_digest_file=$source_digests/$package_name.sha256
    if [[ -e $live_package && -f $source_digest_file \
            && $(cat "$source_digest_file") == "$source_digest" ]]; then
        install -m 0644 "$live_package" "$staged_package"
    else
        install -m 0644 "$package" "$staged_package"
        if [[ -n ${WINE4OFFICE_GPG_KEY_ID:-} ]]; then
            rpmsign --addsign --define "_gpg_name $WINE4OFFICE_GPG_KEY_ID" \
                "$staged_package"
        fi
    fi
    if [[ -e $live_package ]] && ! cmp -s "$staged_package" "$live_package"; then
        echo "Refusing to replace published RPM; use a new package release: $live_package" >&2
        exit 1
    fi
done
createrepo_c --update "$staged_repository"
if [[ -n ${WINE4OFFICE_GPG_KEY_ID:-} ]]; then
    gpg --batch --yes --local-user "$WINE4OFFICE_GPG_KEY_ID" \
        --detach-sign --armor \
        --output "$staged_repository/repodata/repomd.xml.asc" \
        "$staged_repository/repodata/repomd.xml"
else
    rm -f -- "$staged_repository/repodata/repomd.xml.asc"
fi

for package in "$@"; do
    package_name=$(basename "$package")
    if [[ ! -e $packages/$package_name ]]; then
        package_commit_temp=$(mktemp "$packages/.${package_name}.XXXXXX")
        install -m 0644 "$staged_packages/$package_name" "$package_commit_temp"
        mv -- "$package_commit_temp" "$packages/$package_name"
        package_commit_temp=
        committed_new_packages+=("$packages/$package_name")
    fi
    source_digest_file=$source_digests/$package_name.sha256
    if [[ ! -e $source_digest_file ]]; then
        package_commit_temp=$(mktemp "$source_digests/.${package_name}.XXXXXX")
        printf '%s\n' "${package_source_digests[$package_name]}" > "$package_commit_temp"
        mv -- "$package_commit_temp" "$source_digest_file"
        package_commit_temp=
        committed_new_packages+=("$source_digest_file")
    fi
done
if [[ -e $repository_root/repodata ]]; then
    backup_metadata=$(mktemp -d "$repository_root/.repodata.backup.XXXXXX")
    rmdir "$backup_metadata"
    mv -- "$repository_root/repodata" "$backup_metadata"
fi
if ! mv -- "$staged_repository/repodata" "$repository_root/repodata"; then
    [[ -z $backup_metadata || ! -e $backup_metadata ]] || \
        mv -- "$backup_metadata" "$repository_root/repodata"
    exit 1
fi
rm -rf -- "$staged_repository"
staged_repository=
if [[ -n $backup_metadata ]]; then
    rm -rf -- "$backup_metadata"
    backup_metadata=
fi
committed_new_packages=()
