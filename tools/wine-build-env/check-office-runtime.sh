#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
[[ $# == 2 || ( $# == 3 && $3 == --soda ) ]] || {
    echo "Usage: $0 RUNNER NEW_RESULT_DIR [--soda]" >&2
    exit 2
}
runner=$1
results=$2
profile=${3:-}
# Never run against an existing prefix or overwrite earlier evidence.
mkdir -- "$results"
results=$(cd "$results" && pwd)
preflight() {
    runner=$(cd "$runner" && pwd) || return 1
    [[ -x "$runner/bin/wine" && -x "$runner/bin/wineserver" ]] || {
        echo "Installed runner is missing Wine executables: $runner" >&2
        return 1
    }
    for arch in i386 x86_64; do
        for module in windows.applicationmodel.dll twinapi.appcore.dll windows.security.enterprisedata.dll \
                windows.security.authentication.onlineid.dll windows.ui.dll dcomp.dll wine4officeauth.exe; do
            [[ -f "$runner/lib/wine/$arch-windows/$module" ]] || {
                echo "Installed runner is missing $arch-windows/$module" >&2
                return 1
            }
        done
    done
    for tool in x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc timeout; do
        command -v "$tool" >/dev/null || { echo "Required runtime-check tool is missing: $tool" >&2; return 1; }
    done
}
if ! preflight >"$results/preflight.log" 2>&1; then
    cat "$results/preflight.log" >&2
    exit 1
fi
printf 'runner=%s\nprofile=%s\nresults=%s\n' "$runner" "${profile:---supported}" "$results"
failures=0
for bits in 64 32; do
    compiler=x86_64-w64-mingw32-gcc
    [[ $bits == 64 ]] || compiler=i686-w64-mingw32-gcc
    executable=$results/office-runtime-$bits.exe
    if ! "$compiler" -O2 -Wall -Wextra -Werror "$script_dir/probes/office-runtime.c" \
        -lole32 -lruntimeobject -o "$executable" >"$results/compile-$bits.log" 2>&1; then
        sed -n '1,70p' "$results/compile-$bits.log" >&2
        echo "Runtime probe compilation failed ($bits-bit)" >&2
        failures=$((failures + 1))
        continue
    fi
    (
        unset WINELOADER WINESERVER WINEDLLPATH WINE_D3D_CONFIG DISPLAY WAYLAND_DISPLAY
        export WINEPREFIX="$results/prefix-$bits" WINEARCH=win64 WINEDEBUG=-all
        export WINEDLLOVERRIDES='winemenubuilder.exe,mscoree,mshtml=d'
        if ! timeout 90 "$runner/bin/wine" wineboot -u >"$results/wineboot-$bits.log" 2>&1; then
            echo "Runtime-check environment failed to initialize ($bits-bit); see $results/wineboot-$bits.log" >&2
            exit 1
        fi
        args=()
        [[ -z "$profile" ]] || args+=("$profile")
        status=0
        timeout 90 "$runner/bin/wine" "$executable" "${args[@]}" >"$results/runtime-$bits.log" 2>&1 || status=$?
        # Bound diagnostics but retain complete stdout/stderr next to the prefix.
        sed -n '1,70p' "$results/runtime-$bits.log"
        ((status == 0)) || { echo "Runtime probe failed ($bits-bit, exit $status)" >&2; exit 1; }
        expected_profile=supported
        [[ -z "$profile" ]] || expected_profile=soda
        grep -Fx "office_runtime:$expected_profile:$bits:passed" <(tr -d '\r' <"$results/runtime-$bits.log") >/dev/null || {
            echo "Runtime probe did not report a complete $bits-bit pass" >&2
            exit 1
        }
        timeout 30 "$runner/bin/wineserver" -w || {
            echo "Runtime-check processes did not exit; prefix retained at $WINEPREFIX" >&2
            exit 1
        }
    ) || failures=$((failures + 1))
done
((failures == 0)) || { echo "Office runtime validation failed; evidence retained at $results" >&2; exit 1; }
echo "Office runtime validation: PASS (64-bit and 32-bit)"
