#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
contract=$root/tools/wine-build-env
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

assert_absent() {
    local pattern=$1
    local file=$2
    local status
    if grep -F -- "$pattern" "$file" >/dev/null; then
        echo "Forbidden text found in $file: $pattern" >&2
        exit 1
    else
        status=$?
        ((status == 1)) || { echo "Cannot search $file" >&2; exit "$status"; }
    fi
}

for script in "$contract"/*.sh "$contract"/tests/*.sh; do
    bash -n "$script"
done

source_dir=$tmp/source
build_dir=$tmp/build
stage_dir=$tmp/stage
fake_bin=$tmp/bin
mkdir -p "$source_dir/tools/wine4office-manager/packaging/linux-uapi" \
    "$build_dir/include" "$stage_dir" "$fake_bin"
printf '#!/bin/sh\nexit 99\n' > "$source_dir/configure"
chmod 0755 "$source_dir/configure"
printf 'srcdir = %s\n' "$source_dir" > "$build_dir/Makefile"
cat > "$build_dir/config.status" <<'EOF'
ac_cs_config='--prefix=/opt/wine4office --enable-archs=i386,x86_64 --with-gssapi --with-krb5'
EOF
cat > "$build_dir/include/config.h" <<'EOF'
#define HAVE_GSSAPI_GSSAPI_EXT_H 1
#define HAVE_GSSAPI_GSSAPI_H 1
#define HAVE_LINUX_NTSYNC_H 1
#define SONAME_LIBDBUS_1 "libdbus-1.so.3"
#define SONAME_LIBFONTCONFIG "libfontconfig.so.1"
#define SONAME_LIBFREETYPE "libfreetype.so.6"
#define SONAME_LIBGNUTLS "libgnutls.so.30"
#define SONAME_LIBGSSAPI_KRB5 "libgssapi_krb5.so.2"
#define SONAME_LIBKRB5 "libkrb5.so.3"
#define SONAME_LIBVA "libva.so.2"
#define SONAME_LIBVA_DRM "libva-drm.so.2"
EOF

build_env=(
    WINE_SOURCE_DIR="$source_dir"
    WINE_BUILD_DIR="$build_dir"
    WINE_STAGE_DIR="$stage_dir"
    WINE_BUILD_JOBS=12
)
env "${build_env[@]}" "$contract/build.sh" configure
grep -qx 'configure_archs=i386,x86_64' "$build_dir/WINE4OFFICE_BUILD.env"
grep -qx 'jobs=12' "$build_dir/WINE4OFFICE_BUILD.env"
provenance_before=$(sha256sum "$build_dir/WINE4OFFICE_BUILD.env")
if env "${build_env[@]}" "$contract/build.sh" invalid >"$tmp/invalid-mode.log" 2>&1; then
    echo "Build contract accepted an invalid mode" >&2
    exit 1
fi
[[ $(sha256sum "$build_dir/WINE4OFFICE_BUILD.env") == "$provenance_before" ]] || {
    echo "Invalid mode changed build provenance" >&2
    exit 1
}

cp "$build_dir/include/config.h" "$tmp/config.good"
sed -i '/SONAME_LIBGSSAPI_KRB5/d' "$build_dir/include/config.h"
if env "${build_env[@]}" "$contract/build.sh" configure >"$tmp/missing.log" 2>&1; then
    echo "Build contract accepted missing GSSAPI support" >&2
    exit 1
fi
grep -F 'SONAME_LIBGSSAPI_KRB5' "$tmp/missing.log" >/dev/null
cp "$tmp/config.good" "$build_dir/include/config.h"

cat > "$fake_bin/make" <<'EOF'
#!/bin/sh
case " $* " in
    *' -n '*)
        echo 'gcc -c one.c -o one.o'
        echo 'gcc -c two.c -o two.o'
        echo 'gcc one.o two.o -o module.so'
        ;;
    *) echo 'unguarded make executed' >&2; exit 90 ;;
esac
EOF
chmod 0755 "$fake_bin/make"
if env PATH="$fake_bin:$PATH" "${build_env[@]}" WINE_BUILD_MAX_COMPILE_COMMANDS=2 \
    "$contract/build.sh" targets dlls/example/example.so >"$tmp/broad.log" 2>&1; then
    echo "Build contract accepted an unexpectedly broad focused build" >&2
    exit 1
fi
grep -F 'Refusing unexpectedly broad focused build' "$tmp/broad.log" >/dev/null

runner=$stage_dir/opt/wine4office
mkdir -p "$runner/bin" "$runner/lib/wine/x86_64-unix"
printf '#!/bin/sh\nexit 0\n' > "$runner/bin/wine"
printf '#!/bin/sh\n# /dev/ntsync\nexit 0\n' > "$runner/bin/wineserver"
chmod 0755 "$runner/bin/wine" "$runner/bin/wineserver"
printf 'wayland driver\n' > "$runner/lib/wine/x86_64-unix/winewayland.so"
printf 'gss_init_sec_context\n' > "$runner/lib/wine/x86_64-unix/kerberos.so"
env "${build_env[@]}" "$contract/build.sh" verify

printf 'stub kerberos\n' > "$runner/lib/wine/x86_64-unix/kerberos.so"
if env "${build_env[@]}" "$contract/build.sh" verify >"$tmp/stub.log" 2>&1; then
    echo "Build contract accepted a Kerberos stub" >&2
    exit 1
fi
grep -F 'lacks GSSAPI support' "$tmp/stub.log" >/dev/null

printf 'gss_init_sec_context\n' > "$runner/lib/wine/x86_64-unix/kerberos.so"
cat > "$fake_bin/make" <<'EOF'
#!/bin/sh
case " $* " in
    *' -n '*' install '*) echo 'tools/install staged-runner' ;;
    *' install '*)
        runner=$WINE_STAGE_DIR/opt/wine4office
        mkdir -p "$runner/bin" "$runner/lib/wine/x86_64-unix"
        printf '#!/bin/sh\nexit 0\n' > "$runner/bin/wine"
        printf '#!/bin/sh\n# /dev/ntsync\nexit 0\n' > "$runner/bin/wineserver"
        chmod 0755 "$runner/bin/wine" "$runner/bin/wineserver"
        printf 'wayland driver\n' > "$runner/lib/wine/x86_64-unix/winewayland.so"
        printf 'gss_init_sec_context\n' > "$runner/lib/wine/x86_64-unix/kerberos.so"
        ;;
    *) exit 0 ;;
esac
EOF
chmod 0755 "$fake_bin/make"
touch "$stage_dir/unrelated-stage-file"
env PATH="$fake_bin:$PATH" "${build_env[@]}" "$contract/build.sh" full
[[ -f "$stage_dir/unrelated-stage-file" ]] || { echo "Full build removed unrelated stage data" >&2; exit 1; }
grep -Eq '^install_plan_sha256=[0-9a-f]{64}$' "$build_dir/WINE4OFFICE_BUILD.env"
env PATH="$fake_bin:$PATH" "${build_env[@]}" "$contract/build.sh" install

sed -i "s/tools\/install staged-runner/gcc -c unbuilt.c -o unbuilt.o/" "$fake_bin/make"
if env PATH="$fake_bin:$PATH" "${build_env[@]}" "$contract/build.sh" install \
    >"$tmp/dirty-install.log" 2>&1; then
    echo "Build contract installed a runner with unbuilt work" >&2
    exit 1
fi
grep -F 'differs from the canonical clean install plan' "$tmp/dirty-install.log" >/dev/null

grep -F 'libkrb5-dev' "$contract/image/Dockerfile" >/dev/null
grep -F 'libfreetype-dev:i386' "$contract/image/Dockerfile" >/dev/null
grep -F 'libfontconfig-dev:i386' "$contract/image/Dockerfile" >/dev/null
grep -F 'tools/wine-build-env/run-build-container.sh full' \
    "$root/.github/workflows/wine4office-release.yml" >/dev/null
assert_absent 'apt-get install' "$root/.github/workflows/wine4office-release.yml"
grep -F -- '--exclude=/.git' "$contract/sync-agent-source.sh" >/dev/null
assert_absent '--exclude=/.git/' "$contract/sync-agent-source.sh"
grep -F 'docker container inspect "$HOSTNAME"' "$contract/run-build-container.sh" >/dev/null
grep -F 'jobs=${WINE_BUILD_JOBS:-18}' "$contract/build.sh" >/dev/null
grep -F 'cpus=${WINE_BUILD_CPUS:-18}' "$contract/run-build-container.sh" >/dev/null
grep -F 'WINE_BUILD_RECONFIGURE=1' "$contract/refresh-main-build.sh" >/dev/null

ssh_bin=$tmp/ssh-bin
mkdir -p "$ssh_bin"
cat > "$ssh_bin/ssh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "$*" != *';'* ]] || { echo "SSH received an unencoded shell metacharacter" >&2; exit 1; }
payload=${!#}
mapfile -d '' -t decoded < <(printf '%s' "$payload" | base64 --decode)
[[ ${decoded[0]} == agent-safe ]]
[[ ${decoded[1]} == /srv/wine4office/_wine-build ]]
[[ ${decoded[2]} == 'dlls/example;printf injected' ]]
EOF
chmod 0755 "$ssh_bin/ssh"
PATH="$ssh_bin:$PATH" WINE365_REMOTE_HOST=builder@build-host \
    WINE_BUILD_REMOTE_ROOT=/srv/wine4office/_wine-build \
    "$contract/build-agent-targets.sh" agent-safe 'dlls/example;printf injected'

assert_absent 'remote_host=${WINE365_REMOTE_HOST:-' "$contract/build-agent-targets.sh"
assert_absent 'remote_root=${WINE_BUILD_REMOTE_ROOT:-' "$contract/build-agent-targets.sh"
grep -F 'wine_build_repository_variable WINE365_REMOTE_HOST' "$contract/config.sh" >/dev/null
grep -F 'wine_build_repository_variable WINE_BUILD_REMOTE_ROOT' "$contract/config.sh" >/dev/null
grep -F 'wine_build_repository_variable WINE_BUILD_REPO_URL' "$contract/config.sh" >/dev/null

echo "canonical Wine build contract: PASS"
