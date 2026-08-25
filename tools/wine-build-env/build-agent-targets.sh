#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
source "$script_dir/config.sh"
remote_host=$(wine_build_remote_host)
remote_root=$(wine_build_remote_root)
agent_id=${1:?usage: build-agent-targets.sh AGENT_ID TARGET...}
shift
(($# > 0)) || { echo "At least one explicit make target is required" >&2; exit 2; }
[[ "$agent_id" =~ ^[a-z0-9][a-z0-9-]{2,47}$ ]] || { echo "Unsafe AGENT_ID" >&2; exit 2; }
for target in "$@"; do
    [[ -n "$target" && "$target" != -* && "$target" != *$'\n'* ]] || {
        echo "Unsafe make target: $target" >&2
        exit 2
    }
done
remote_payload=$(printf '%s\0' "$agent_id" "$remote_root" "$@" | base64 -w0)

ssh -o BatchMode=yes "$remote_host" bash -s -- "$remote_payload" <<'REMOTE'
set -euo pipefail
payload=$1
[[ "$payload" =~ ^[A-Za-z0-9+/]*={0,2}$ ]] || { echo "Invalid remote argument payload" >&2; exit 2; }
mapfile -d '' -t remote_args < <(printf '%s' "$payload" | base64 --decode)
((${#remote_args[@]} >= 3)) || { echo "Invalid remote argument count" >&2; exit 2; }
agent_id=${remote_args[0]}
remote_root=${remote_args[1]}
set -- "${remote_args[@]:2}"
workspace=$remote_root/agents/$agent_id
grep -qx 'wine4office-build-root-v1' "$remote_root/.wine4office-build-root"
grep -qx "agent_id=$agent_id" "$workspace/OWNER.env"
[[ -x "$workspace/contract/run-build-container.sh" ]]
available_kb=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
((available_kb >= 4 * 1024 * 1024)) || { echo "Less than 4 GiB available" >&2; exit 1; }
exec 9>"$remote_root/build.lock"
flock 9
WINE_BUILD_CPUS=18 WINE_BUILD_JOBS=18 WINE_BUILD_MEMORY=10g \
    "$workspace/contract/run-build-container.sh" targets \
    "$workspace/source" "$workspace/build" "$workspace/stage" "$@"
REMOTE
