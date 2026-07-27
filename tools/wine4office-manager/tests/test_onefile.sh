#!/usr/bin/env bash
# End-to-end smoke test for the generated self-extracting installer.
set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
RUNNER1="$TMP/runner-one"
RUNNER2="$TMP/runner-two"
mkdir -p "$RUNNER1/bin" "$RUNNER2/bin"
for pair in "$RUNNER1:one" "$RUNNER2:two"; do
    runner=${pair%%:*}; marker=${pair##*:}
    cat > "$runner/bin/wine" <<EOF
#!/bin/sh
echo $marker
EOF
    chmod +x "$runner/bin/wine"
done

export HOME="$TMP/home with spaces"
export USER=wine4office-test
export XDG_DATA_HOME="$HOME/share"
export XDG_CONFIG_HOME="$HOME/config"
export XDG_CACHE_HOME="$HOME/cache"
export XDG_DESKTOP_DIR="$HOME/Desktop Folder"
export WINE4OFFICE_BIN_HOME="$HOME/bin"
mkdir -p "$HOME/.wine4office"
echo keep > "$HOME/.wine4office/user-data"

BUNDLE1="$TMP/wine4office-1.run"
BUNDLE2="$TMP/wine4office-2.run"
"$HERE/packaging/build-onefile.sh" "$RUNNER1" "$BUNDLE1" 1.0.0 \
    https://updates.example/manifest.json >/dev/null
"$HERE/packaging/build-onefile.sh" "$RUNNER2" "$BUNDLE2" 2.0.0 \
    https://updates.example/manifest.json >/dev/null
BUNDLE="$BUNDLE1" python3 - <<'PY'
import hashlib, json, os
from pathlib import Path
bundle = Path(os.environ["BUNDLE"])
manifest = json.loads(bundle.with_suffix(".manifest.json").read_text())
assert manifest["installer_url"] == ""
assert manifest["sha256"] == hashlib.sha256(bundle.read_bytes()).hexdigest()
PY
[[ $("$BUNDLE1" --version) == 1.0.0 ]]
"$BUNDLE1" --install >/dev/null
ROOT="$XDG_DATA_HOME/wine4office"
[[ $("$ROOT/runner/bin/wine") == one ]]
[[ $(<"$ROOT/VERSION") == 1.0.0 ]]
[[ -x "$ROOT/bin/wine4office-manager" ]]
[[ -f "$ROOT/lib/wine4office_qt.py" ]]
[[ ! -e "$ROOT/lib/ui.html" ]]
[[ -x "$ROOT/bin/wine4office-uninstall" ]]
[[ -f "$XDG_DATA_HOME/applications/wine4office-manager.desktop" ]]
PYTHONPATH="$ROOT/lib" python3 - <<'PY'
import wine4office_backend as backend
assert backend.load_config()["update_url"] == "https://updates.example/manifest.json"
PY

"$BUNDLE2" --update >/dev/null
[[ $("$ROOT/runner/bin/wine") == two ]]
[[ $(<"$ROOT/VERSION") == 2.0.0 ]]
[[ -f "$ROOT/install.json" ]]

EXTRACT="$TMP/extracted"
"$BUNDLE2" --extract "$EXTRACT" >/dev/null
[[ -x "$EXTRACT/payload/runner/bin/wine" ]]

"$BUNDLE2" --uninstall >/dev/null
[[ ! -e "$ROOT/runner" ]]
[[ ! -e "$XDG_DATA_HOME/applications/wine4office-manager.desktop" ]]
[[ -f "$HOME/.wine4office/user-data" ]]
[[ ! -e "$XDG_CONFIG_HOME/wine4office" ]]

# Prefix deletion is separate, explicit, and safety-validated.
"$BUNDLE2" --install >/dev/null
"$ROOT/bin/wine4office-uninstall" --purge-runner --remove-prefix "$HOME/.wine4office" >/dev/null
[[ ! -e "$HOME/.wine4office" ]]
echo "one-file install/update/extract/uninstall: PASS"
