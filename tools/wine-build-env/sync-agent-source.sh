#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
source "$script_dir/config.sh"
remote_host=$(wine_build_remote_host)
remote_root=$(wine_build_remote_root)
agent_id=${1:?usage: sync-agent-source.sh AGENT_ID LOCAL_WORKTREE}
local_source=${2:?usage: sync-agent-source.sh AGENT_ID LOCAL_WORKTREE}

[[ "$agent_id" =~ ^[a-z0-9][a-z0-9-]{2,47}$ ]] || { echo "Unsafe AGENT_ID" >&2; exit 2; }
local_source=$(cd "$local_source" && pwd)
git -C "$local_source" rev-parse --is-inside-work-tree >/dev/null
remote_payload=$(printf '%s\0' "$agent_id" "$remote_root" | base64 -w0)

metadata=$(ssh -o BatchMode=yes "$remote_host" bash -s -- "$remote_payload" <<'REMOTE'
set -euo pipefail
payload=$1
[[ "$payload" =~ ^[A-Za-z0-9+/]*={0,2}$ ]] || { echo "Invalid remote argument payload" >&2; exit 2; }
mapfile -d '' -t remote_args < <(printf '%s' "$payload" | base64 --decode)
((${#remote_args[@]} == 2)) || { echo "Invalid remote argument count" >&2; exit 2; }
agent_id=${remote_args[0]}
remote_root=${remote_args[1]}
workspace=$remote_root/agents/$agent_id
grep -qx 'wine4office-build-root-v1' "$remote_root/.wine4office-build-root"
grep -qx "agent_id=$agent_id" "$workspace/OWNER.env"
awk -F= '$1 == "source_commit" {print $2}' "$workspace/OWNER.env"
REMOTE
)
base_commit=$metadata
[[ "$base_commit" =~ ^[0-9a-f]{40}$ ]]
git -C "$local_source" merge-base --is-ancestor "$base_commit" HEAD || {
    echo "Local worktree is not based on the agent baseline $base_commit" >&2
    exit 1
}

rsync -rlp --checksum --delete --no-times --omit-dir-times --safe-links \
    --exclude=/.git \
    "$local_source"/ "$remote_host:$remote_root/agents/$agent_id/source/"

ssh -o BatchMode=yes "$remote_host" bash -s -- "$remote_payload" <<'REMOTE'
set -euo pipefail
payload=$1
[[ "$payload" =~ ^[A-Za-z0-9+/]*={0,2}$ ]] || { echo "Invalid remote argument payload" >&2; exit 2; }
mapfile -d '' -t remote_args < <(printf '%s' "$payload" | base64 --decode)
((${#remote_args[@]} == 2)) || { echo "Invalid remote argument count" >&2; exit 2; }
agent_id=${remote_args[0]}
remote_root=${remote_args[1]}
grep -qx 'wine4office-build-root-v1' "$remote_root/.wine4office-build-root"
git -C "$remote_root/agents/$agent_id/source" status --short --branch
REMOTE
