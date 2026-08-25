#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
source "$script_dir/config.sh"
remote_host=$(wine_build_remote_host)
remote_root=$(wine_build_remote_root)
agent_id=${1:?usage: create-agent-build.sh AGENT_ID}

[[ "$agent_id" =~ ^[a-z0-9][a-z0-9-]{2,47}$ ]] || {
    echo "AGENT_ID must be 3-48 lowercase letters, digits, or hyphens" >&2
    exit 2
}

export WINE365_REMOTE_HOST="$remote_host" WINE_BUILD_REMOTE_ROOT="$remote_root"
"$script_dir/refresh-main-build.sh"
remote_payload=$(printf '%s\0' "$agent_id" "$remote_root" | base64 -w0)

ssh -o BatchMode=yes "$remote_host" bash -s -- "$remote_payload" <<'REMOTE'
set -euo pipefail
payload=$1
[[ "$payload" =~ ^[A-Za-z0-9+/]*={0,2}$ ]] || { echo "Invalid remote argument payload" >&2; exit 2; }
mapfile -d '' -t remote_args < <(printf '%s' "$payload" | base64 --decode)
((${#remote_args[@]} == 2)) || { echo "Invalid remote argument count" >&2; exit 2; }
agent_id=${remote_args[0]}
remote_root=${remote_args[1]}
baseline_root=$remote_root/baselines
workspace=$remote_root/agents/$agent_id

grep -qx 'wine4office-build-root-v1' "$remote_root/.wine4office-build-root"
[[ -L "$baseline_root/current" ]] || { echo "No canonical baseline is available" >&2; exit 1; }
baseline=$(readlink -e "$baseline_root/current")
[[ "$baseline" == "$baseline_root"/* && -f "$baseline/BUILD.env" ]]
[[ ! -e "$workspace" ]] || { echo "Agent build already exists: $agent_id" >&2; exit 1; }

cp -a --reflink=always "$baseline" "$workspace"
source_commit=$(awk -F= '$1 == "source_commit" {print $2}' "$baseline/BUILD.env")
contract_hash=$(awk -F= '$1 == "contract_hash" {print $2}' "$baseline/BUILD.env")
cat > "$workspace/OWNER.env" <<EOF
agent_id=$agent_id
source_commit=$source_commit
contract_hash=$contract_hash
baseline=$baseline
created=$(date --iso-8601=seconds)
EOF
printf 'agent_id=%s\nworkspace=%s\nsource_commit=%s\ncontract_hash=%s\n' \
    "$agent_id" "$workspace" "$source_commit" "$contract_hash"
REMOTE
