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
packages=$repository/rpm/x86_64/Packages
mkdir -p "$packages"
install -m 0644 "$here/wine4office.repo" "$repository/wine4office.repo"
for package in "$@"; do
    [[ -f $package && $package == *.rpm ]] || { echo "Invalid RPM: $package" >&2; exit 1; }
    if [[ -n ${WINE4OFFICE_GPG_KEY_ID:-} ]]; then
        rpmsign --addsign --define "_gpg_name $WINE4OFFICE_GPG_KEY_ID" "$package"
    fi
    install -m 0644 "$package" "$packages/$(basename "$package")"
done
createrepo_c --update "$repository/rpm/x86_64"
if [[ -n ${WINE4OFFICE_GPG_KEY_ID:-} ]]; then
    gpg --batch --yes --local-user "$WINE4OFFICE_GPG_KEY_ID" \
        --detach-sign --armor \
        "$repository/rpm/x86_64/repodata/repomd.xml"
fi
