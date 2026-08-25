#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: build.sh configure|full|targets|install|verify [MAKE_TARGET...]

The caller must mount a Wine source tree, build tree, and stage tree and set
WINE_SOURCE_DIR, WINE_BUILD_DIR, and WINE_STAGE_DIR when they differ from the
canonical /work paths.
EOF
    exit 2
}

mode=${1:-}
[[ -n "$mode" ]] || usage
shift

source_dir=${WINE_SOURCE_DIR:-/work/source}
build_dir=${WINE_BUILD_DIR:-/work/build}
stage_dir=${WINE_STAGE_DIR:-/work/stage}
prefix=${WINE_BUILD_PREFIX:-/opt/wine4office}
jobs=${WINE_BUILD_JOBS:-18}
max_compile_commands=${WINE_BUILD_MAX_COMPILE_COMMANDS:-80}
image_id=${WINE_BUILD_IMAGE_ID:-unknown}
reconfigure=${WINE_BUILD_RECONFIGURE:-0}

[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "WINE_BUILD_JOBS must be a positive integer" >&2; exit 2; }
[[ "$prefix" == /opt/wine4office ]] || { echo "WINE_BUILD_PREFIX must be /opt/wine4office" >&2; exit 2; }
[[ "$reconfigure" == 0 || "$reconfigure" == 1 ]] || {
    echo "WINE_BUILD_RECONFIGURE must be 0 or 1" >&2
    exit 2
}
[[ "$max_compile_commands" =~ ^[0-9]+$ ]] || {
    echo "WINE_BUILD_MAX_COMPILE_COMMANDS must be a non-negative integer" >&2
    exit 2
}
[[ -x "$source_dir/configure" ]] || { echo "Invalid Wine source tree: $source_dir" >&2; exit 1; }
mkdir -p "$build_dir" "$stage_dir"
source_dir=$(cd "$source_dir" && pwd)
build_dir=$(cd "$build_dir" && pwd)
stage_dir=$(cd "$stage_dir" && pwd)
[[ "$source_dir" != "$build_dir" && "$source_dir" != "$stage_dir" && "$build_dir" != "$stage_dir" ]] || {
    echo "Source, build, and stage must be separate directories" >&2
    exit 1
}

configure_build() {
    if [[ "$reconfigure" == 1 || ! -f "$build_dir/Makefile" ]]; then
        echo "Configuring the canonical Office build in $build_dir"
        (
            cd "$build_dir"
            CPPFLAGS="-I$source_dir/tools/wine4office-manager/packaging/linux-uapi${CPPFLAGS:+ $CPPFLAGS}" \
                "$source_dir/configure" \
                --prefix="$prefix" \
                --enable-archs=i386,x86_64 \
                --with-gssapi \
                --with-krb5
        )
    fi

    [[ -f "$build_dir/config.status" ]] || { echo "Build tree has no config.status" >&2; exit 1; }
    grep -F -- "--prefix=$prefix" "$build_dir/config.status" >/dev/null || {
        echo "Build tree was configured with a different prefix" >&2
        exit 1
    }
    for argument in --enable-archs=i386,x86_64 --with-gssapi --with-krb5; do
        grep -F -- "$argument" "$build_dir/config.status" >/dev/null || {
            echo "Build tree is missing required configure argument: $argument" >&2
            exit 1
        }
    done
    grep -Fx "srcdir = $source_dir" "$build_dir/Makefile" >/dev/null || {
        echo "Build tree source provenance mismatch: expected $source_dir" >&2
        exit 1
    }
}

check_configured_capabilities() {
    local config=$build_dir/include/config.h
    [[ -f "$config" ]] || { echo "Wine configure did not produce $config" >&2; exit 1; }
    local definitions=(
        HAVE_GSSAPI_GSSAPI_EXT_H
        HAVE_GSSAPI_GSSAPI_H
        HAVE_LINUX_NTSYNC_H
        SONAME_LIBDBUS_1
        SONAME_LIBFONTCONFIG
        SONAME_LIBFREETYPE
        SONAME_LIBGNUTLS
        SONAME_LIBGSSAPI_KRB5
        SONAME_LIBKRB5
        SONAME_LIBVA
        SONAME_LIBVA_DRM
    )
    local definition
    for definition in "${definitions[@]}"; do
        grep -Eq "^#define $definition( |$)" "$config" || {
            echo "Wine configure did not enable required Office capability: $definition" >&2
            exit 1
        }
    done
}

record_provenance() {
    local source_commit=unknown
    local install_plan_sha256=
    if git -C "$source_dir" rev-parse --verify HEAD >/dev/null 2>&1; then
        source_commit=$(git -C "$source_dir" rev-parse HEAD)
    fi
    if [[ -f "$build_dir/WINE4OFFICE_BUILD.env" ]]; then
        install_plan_sha256=$(sed -n 's/^install_plan_sha256=//p' "$build_dir/WINE4OFFICE_BUILD.env")
    fi
    cat > "$build_dir/WINE4OFFICE_BUILD.env" <<EOF
source_commit=$source_commit
builder_image_id=$image_id
configure_prefix=$prefix
configure_archs=i386,x86_64
configure_gssapi=required
configure_krb5=required
jobs=$jobs
EOF
    if [[ -n "$install_plan_sha256" ]]; then
        printf 'install_plan_sha256=%s\n' "$install_plan_sha256" >> "$build_dir/WINE4OFFICE_BUILD.env"
    fi
}

install_plan_sha256() {
    local dry_run=$1
    sha256sum "$dry_run" | awk '{print $1}'
}

record_clean_install_plan() {
    local dry_run plan_sha
    dry_run=$(mktemp)
    make -C "$build_dir" -n DESTDIR="$stage_dir" install > "$dry_run"
    plan_sha=$(install_plan_sha256 "$dry_run")
    rm -f -- "$dry_run"
    sed -i '/^install_plan_sha256=/d' "$build_dir/WINE4OFFICE_BUILD.env"
    printf 'install_plan_sha256=%s\n' "$plan_sha" >> "$build_dir/WINE4OFFICE_BUILD.env"
}

count_compile_commands() {
    local dry_run=$1
    grep -Ec '^[[:space:]]*([^[:space:]=]+=[^[:space:]]+[[:space:]]+)*([^[:space:]]*/)?(ccache[[:space:]]+)?((i686|x86_64)-w64-mingw32-)?(gcc|g[+][+]|cc|c[+][+]|clang|clang[+][+]|winegcc|widl|bison|flex)([[:space:]]|$)' \
        "$dry_run" || true
}

guard_targets() {
    (($# > 0)) || { echo "Focused builds require at least one explicit make target" >&2; exit 2; }
    local target
    for target in "$@"; do
        [[ -n "$target" && "$target" != -* && "$target" != *$'\n'* ]] || {
            echo "Unsafe make target: $target" >&2
            exit 2
        }
    done
    local dry_run
    dry_run=$(mktemp)
    if ! make -C "$build_dir" -n -- "$@" > "$dry_run"; then
        rm -f -- "$dry_run"
        echo "Focused build dry run failed" >&2
        exit 1
    fi
    local compile_commands
    compile_commands=$(count_compile_commands "$dry_run")
    echo "Dry run proposes $compile_commands compile/link commands for $# explicit targets"
    if ((compile_commands > max_compile_commands)); then
        echo "Refusing unexpectedly broad focused build ($compile_commands > $max_compile_commands commands)" >&2
        sed -n '1,120p' "$dry_run" >&2
        rm -f -- "$dry_run"
        exit 1
    fi
    rm -f -- "$dry_run"
}

verify_staged_runner() {
    local runner=$stage_dir$prefix
    [[ -x "$runner/bin/wine" && -x "$runner/bin/wineserver" ]] || {
        echo "Staged runner is missing Wine executables: $runner" >&2
        exit 1
    }
    grep -aq '/dev/ntsync' "$runner/bin/wineserver" || {
        echo "Staged wineserver does not contain NTSYNC support" >&2
        exit 1
    }
    [[ -f "$runner/lib/wine/x86_64-unix/winewayland.so" ]] || {
        echo "Staged runner is missing the Wine Wayland driver" >&2
        exit 1
    }
    mapfile -t kerberos_modules < <(find "$runner/lib/wine" -type f -name kerberos.so -print)
    ((${#kerberos_modules[@]} > 0)) || {
        echo "Staged runner has no Kerberos Unix module" >&2
        exit 1
    }
    local module
    for module in "${kerberos_modules[@]}"; do
        grep -aq 'gss_init_sec_context' "$module" || {
            echo "Staged Kerberos module lacks GSSAPI support: $module" >&2
            exit 1
        }
    done
}

case "$mode" in
    configure|full|install|verify)
        (($# == 0)) || usage
        ;;
    targets)
        (($# > 0)) || usage
        ;;
    *) usage ;;
esac

configure_build
check_configured_capabilities
record_provenance

case "$mode" in
    configure)
        ;;
    full)
        echo "Building canonical Wine4Office tree with $jobs parallel jobs"
        make -C "$build_dir" -j"$jobs"
        rm -rf -- "$stage_dir$prefix"
        make -C "$build_dir" -j"$jobs" DESTDIR="$stage_dir" install
        verify_staged_runner
        record_clean_install_plan
        ;;
    targets)
        guard_targets "$@"
        make -C "$build_dir" -j"$jobs" -- "$@"
        ;;
    install)
        install_dry_run=$(mktemp)
        trap 'rm -f -- "$install_dry_run"' EXIT
        make -C "$build_dir" -n DESTDIR="$stage_dir" install > "$install_dry_run"
        expected_install_plan=$(sed -n 's/^install_plan_sha256=//p' "$build_dir/WINE4OFFICE_BUILD.env")
        current_install_plan=$(install_plan_sha256 "$install_dry_run")
        if [[ -z "$expected_install_plan" || "$current_install_plan" != "$expected_install_plan" ]]; then
            echo "Refusing runner installation: build state differs from the canonical clean install plan" >&2
            sed -n '1,120p' "$install_dry_run" >&2
            exit 1
        fi
        make -C "$build_dir" -j"$jobs" DESTDIR="$stage_dir" install
        verify_staged_runner
        ;;
    verify)
        verify_staged_runner
        ;;
esac
