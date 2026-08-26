#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
source "$script_dir/config.sh"
remote_host=$(wine_build_remote_host)
repo_url=$(wine_build_repo_url)
remote_root=$(wine_build_remote_root)
remote_payload=$(printf '%s\0' "$repo_url" "$remote_root" | base64 -w0)

ssh -o BatchMode=yes "$remote_host" bash -s -- "$remote_payload" <<'REMOTE'
set -euo pipefail
payload=$1
[[ "$payload" =~ ^[A-Za-z0-9+/]*={0,2}$ ]] || { echo "Invalid remote argument payload" >&2; exit 2; }
mapfile -d '' -t remote_args < <(printf '%s' "$payload" | base64 --decode)
((${#remote_args[@]} == 2)) || { echo "Invalid remote argument count" >&2; exit 2; }
repo_url=${remote_args[0]}
remote_root=${remote_args[1]}
baseline_root=$remote_root/baselines

[[ "$remote_root" =~ ^/[A-Za-z0-9._/-]+/_wine-build$ && "$remote_root" != *'/../'* ]] || {
    echo "Unexpected remote build root: $remote_root" >&2
    exit 1
}
mkdir -p "$remote_root"
root_marker=$remote_root/.wine4office-build-root
if [[ ! -e "$root_marker" ]]; then
    printf 'wine4office-build-root-v1\n' > "$root_marker"
fi
grep -qx 'wine4office-build-root-v1' "$root_marker" || {
    echo "Invalid remote build-root marker" >&2
    exit 1
}
[[ $(stat -f -c %T "$remote_root") == btrfs ]] || {
    echo "Canonical incremental builds require Btrfs reflinks" >&2
    exit 1
}
mkdir -p "$baseline_root" "$remote_root/agents"
exec 9>"$remote_root/refresh.lock"
flock 9

origin_commit=$(git ls-remote "$repo_url" refs/heads/main | awk 'NR == 1 {print $1}')
[[ "$origin_commit" =~ ^[0-9a-f]{40}$ ]] || { echo "Cannot resolve origin/main" >&2; exit 1; }

candidate=$(mktemp -d "$baseline_root/.refresh.XXXXXX")
cleanup() {
    [[ -n ${candidate:-} && "$candidate" == "$baseline_root"/.refresh.* ]] && rm -rf -- "$candidate"
}
trap cleanup EXIT INT TERM

if [[ -L "$baseline_root/current" ]]; then
    current=$(readlink -e "$baseline_root/current")
    [[ "$current" == "$baseline_root"/* && -f "$current/BUILD.env" ]] || {
        echo "Invalid canonical baseline pointer" >&2
        exit 1
    }
    cp -a --reflink=always "$current"/. "$candidate"/
    git -C "$candidate/source" status --porcelain | grep -q . && {
        echo "Canonical baseline source is dirty" >&2
        exit 1
    }
    git -C "$candidate/source" fetch --quiet origin main
    git -C "$candidate/source" checkout --quiet --detach "$origin_commit"
else
    git clone --quiet --no-tags "$repo_url" "$candidate/source"
    git -C "$candidate/source" checkout --quiet --detach "$origin_commit"
    mkdir -p "$candidate/build" "$candidate/stage"
fi

contract_dir=$candidate/source/tools/wine-build-env
[[ -x "$contract_dir/run-build-container.sh" && -f "$contract_dir/image/Dockerfile" ]] || {
    echo "origin/main does not contain the canonical Wine build contract" >&2
    exit 1
}
contract_hash=$(sha256sum \
    "$contract_dir/image/Dockerfile" \
    "$contract_dir/build.sh" \
    "$contract_dir/run-build-container.sh" | awk '{print $1}' | sha256sum | cut -c1-16)
baseline_name=${origin_commit:0:12}-$contract_hash
destination=$baseline_root/$baseline_name

if [[ -d "$destination" ]]; then
    grep -qx "source_commit=$origin_commit" "$destination/BUILD.env"
    grep -qx "contract_hash=$contract_hash" "$destination/BUILD.env"
else
    exec 8>"$remote_root/build.lock"
    flock 8
    available_kb=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
    ((available_kb >= 4 * 1024 * 1024)) || {
        echo "Less than 4 GiB is available; refusing canonical build refresh" >&2
        exit 1
    }
    WINE_BUILD_CPUS=18 WINE_BUILD_JOBS=18 WINE_BUILD_MEMORY=10g WINE_BUILD_RECONFIGURE=1 \
        "$contract_dir/run-build-container.sh" full \
        "$candidate/source" "$candidate/build" "$candidate/stage"
    rm -rf -- "$candidate/contract"
    cp -a "$contract_dir" "$candidate/contract"
    builder_image=$(awk -F= '$1 == "builder_image_id" {print $2}' \
        "$candidate/build/WINE4OFFICE_BUILD.env")
    cat > "$candidate/BUILD.env" <<EOF
source_commit=$origin_commit
contract_hash=$contract_hash
builder_image_id=$builder_image
configure_archs=i386,x86_64
created=$(date --iso-8601=seconds)
EOF
    mv "$candidate" "$destination"
    candidate=
fi

link_tmp=$baseline_root/.current.$$
ln -s "$baseline_name" "$link_tmp"
mv -Tf "$link_tmp" "$baseline_root/current"
printf 'baseline=%s\nsource_commit=%s\ncontract_hash=%s\n' \
    "$destination" "$origin_commit" "$contract_hash"
REMOTE
