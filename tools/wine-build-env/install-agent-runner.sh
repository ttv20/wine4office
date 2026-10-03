#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
source "$script_dir/config.sh"
remote_host=$(wine_build_remote_host)
remote_root=$(wine_build_remote_root)
agent_id=${1:?usage: install-agent-runner.sh AGENT_ID}
[[ "$agent_id" =~ ^[a-z0-9][a-z0-9-]{2,47}$ ]] || { echo "Unsafe AGENT_ID" >&2; exit 2; }
# Optional: WINE4OFFICE_BUNDLE_DXVK=1 also bundles the pinned DXVK release into
# the agent runner (needs HTTPS access from the build server; the verified
# tarball is cached under the build root). Off by default.
bundle_dxvk=${WINE4OFFICE_BUNDLE_DXVK:-0}
[[ "$bundle_dxvk" =~ ^[01]$ ]] || { echo "WINE4OFFICE_BUNDLE_DXVK must be 0 or 1" >&2; exit 2; }
remote_payload=$(printf '%s\0' "$agent_id" "$remote_root" "$bundle_dxvk" | base64 -w0)

ssh -o BatchMode=yes "$remote_host" bash -s -- "$remote_payload" <<'REMOTE'
set -euo pipefail
payload=$1
[[ "$payload" =~ ^[A-Za-z0-9+/]*={0,2}$ ]] || { echo "Invalid remote argument payload" >&2; exit 2; }
mapfile -d '' -t remote_args < <(printf '%s' "$payload" | base64 --decode)
((${#remote_args[@]} == 3)) || { echo "Invalid remote argument count" >&2; exit 2; }
agent_id=${remote_args[0]}
remote_root=${remote_args[1]}
bundle_dxvk=${remote_args[2]}
workspace=$remote_root/agents/$agent_id
grep -qx 'wine4office-build-root-v1' "$remote_root/.wine4office-build-root"
grep -qx "agent_id=$agent_id" "$workspace/OWNER.env"
exec 9>"$remote_root/build.lock"
flock 9
WINE_BUILD_CPUS=18 WINE_BUILD_JOBS=18 WINE_BUILD_MEMORY=10g \
    "$workspace/contract/run-build-container.sh" install \
    "$workspace/source" "$workspace/build" "$workspace/stage"
if [[ $bundle_dxvk == 1 ]]; then
    [[ -x "$workspace/contract/bundle-dxvk.sh" ]] || {
        echo "This build baseline predates bundle-dxvk.sh; refresh the baseline first" >&2
        exit 1
    }
    DXVK_CACHE_DIR="$remote_root/cache/dxvk" \
        "$workspace/contract/bundle-dxvk.sh" "$workspace/stage/opt/wine4office"
fi
printf 'runner=%s\n' "$workspace/stage/opt/wine4office"
REMOTE
