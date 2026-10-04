#!/usr/bin/env bash
# Run a built Wine4OfficeManager binary with the hidden DXVK probe flag.
# The frozen binary re-executes itself for this check; it must print one valid
# probe result as JSON and exit 0 whether or not a usable GPU is present
# (llvmpipe-only and loader-less CI runners report supported=false). A probe
# that crashed inside the binary reports supported=null with code "error";
# that fails this check, because users would silently stay on WineD3D.
set -euo pipefail

[[ $# -eq 1 ]] || { echo "Usage: $0 WINE4OFFICE_MANAGER_BINARY" >&2; exit 2; }
binary=$1
[[ -x $binary ]] || { echo "Not an executable: $binary" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/config" XDG_DATA_HOME="$tmp/data" \
    timeout 120 "$binary" --probe-dxvk-support > "$tmp/probe.json" 2> "$tmp/probe.err" || {
    status=$?
    echo "DXVK probe exited with status $status" >&2
    tail -n 20 "$tmp/probe.err" >&2
    exit 1
}
python3 - "$tmp/probe.json" <<'PY'
import json
import sys

lines = [line for line in open(sys.argv[1], encoding="utf-8") if line.strip()]
assert lines, "the DXVK probe printed nothing"
result = json.loads(lines[-1])
assert result["schema"] == 1, result
assert isinstance(result["code"], str) and isinstance(result["reason"], str), result
assert isinstance(result["devices"], list), result
# The child probe answers True or False for every host it can inspect;
# None only comes from an exception (code "error") inside the binary.
assert result["supported"] in (True, False), f"DXVK probe failed: {result}"
assert result["code"] not in ("error", "timeout"), f"DXVK probe failed: {result}"
print(f"frozen DXVK probe: PASS (supported={result['supported']}, code={result['code']})")
PY
