#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 MODE SOURCE_DIR BUILD_DIR STAGE_DIR [MAKE_TARGET...]" >&2
    exit 2
}

[[ $# -ge 4 ]] || usage
mode=$1
source_dir=$2
build_dir=$3
stage_dir=$4
shift 4

case "$mode" in configure|full|targets|install|verify) ;; *) usage ;; esac
[[ -d "$source_dir" && -x "$source_dir/configure" ]] || { echo "Invalid source directory: $source_dir" >&2; exit 1; }
mkdir -p "$build_dir" "$stage_dir"
source_dir=$(cd "$source_dir" && pwd)
build_dir=$(cd "$build_dir" && pwd)
stage_dir=$(cd "$stage_dir" && pwd)
script_dir=$(cd "$(dirname "$0")" && pwd)
dockerfile=$script_dir/image/Dockerfile
[[ -f "$dockerfile" && -x "$script_dir/build.sh" ]] || { echo "Incomplete build contract: $script_dir" >&2; exit 1; }

contract_hash=$(sha256sum "$dockerfile" "$script_dir/build.sh" "$script_dir/run-build-container.sh" \
    | awk '{print $1}' | sha256sum | cut -c1-16)
image=${WINE_BUILD_IMAGE:-wine4office-builder:$contract_hash}
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --pull --tag "$image" "$script_dir/image"
fi
image_id=$(docker image inspect "$image" --format '{{.Id}}')

cpus=${WINE_BUILD_CPUS:-18}
memory=${WINE_BUILD_MEMORY:-10g}
jobs=${WINE_BUILD_JOBS:-18}
[[ "$cpus" =~ ^[1-9][0-9]*([.][0-9]+)?$ && "$memory" =~ ^[1-9][0-9]*[mg]$ && "$jobs" =~ ^[1-9][0-9]*$ ]] || {
    echo "Invalid WINE_BUILD_CPUS, WINE_BUILD_MEMORY, or WINE_BUILD_JOBS" >&2
    exit 2
}

volume_args=()
container_source=/work/source
container_build=/work/build
container_stage=/work/stage
container_contract=/work/contract
if [[ -n ${HOSTNAME:-} ]] && docker container inspect "$HOSTNAME" >/dev/null 2>&1; then
    volume_args=(--volumes-from "$HOSTNAME")
    container_source=$source_dir
    container_build=$build_dir
    container_stage=$stage_dir
    container_contract=$script_dir
else
    volume_args=(
        --mount "type=bind,src=$source_dir,dst=$container_source,readonly"
        --mount "type=bind,src=$build_dir,dst=$container_build"
        --mount "type=bind,src=$stage_dir,dst=$container_stage"
        --mount "type=bind,src=$script_dir,dst=$container_contract,readonly"
    )
fi

exec docker run --rm --init \
    --label wine4office.role=wine-builder \
    --cpus "$cpus" --memory "$memory" --memory-swap "$memory" \
    --network none --security-opt no-new-privileges \
    --user "$(id -u):$(id -g)" \
    "${volume_args[@]}" \
    --workdir "$container_build" \
    --env WINE_SOURCE_DIR="$container_source" \
    --env WINE_BUILD_DIR="$container_build" \
    --env WINE_STAGE_DIR="$container_stage" \
    --env WINE_BUILD_JOBS="$jobs" \
    --env WINE_BUILD_RECONFIGURE="${WINE_BUILD_RECONFIGURE:-0}" \
    --env WINE_BUILD_MAX_COMPILE_COMMANDS="${WINE_BUILD_MAX_COMPILE_COMMANDS:-80}" \
    --env WINE_BUILD_IMAGE_ID="$image_id" \
    "$image" \
    "$container_contract/build.sh" "$mode" "$@"
