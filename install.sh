#!/usr/bin/env bash
# Install the latest verified Wine4Office Manager and Wine runner for one user.
set -euo pipefail
umask 077

METADATA_URL=${WINE4OFFICE_METADATA_URL:-https://github.com/ttv20/wine4office/releases/latest/download/release.json}
DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
BIN_HOME=${WINE4OFFICE_BIN_HOME:-$HOME/.local/bin}
ROOT=${WINE4OFFICE_HOME:-$DATA_HOME/wine4office}

fail() {
    printf 'wine4office install: %s\n' "$*" >&2
    exit 1
}
warn() {
    printf 'wine4office install warning: %s\n' "$*" >&2
}
prompt_and_launch_manager() {
    local manager=$1 answer
    if [[ -v WINE4OFFICE_LAUNCH_MANAGER ]]; then
        answer=$WINE4OFFICE_LAUNCH_MANAGER
    elif [[ -t 0 ]]; then
        read -r -p 'Launch Wine4Office Manager now? [Y/n] ' answer || answer=n
    elif [[ -t 1 ]]; then
        if ! read -r -p 'Launch Wine4Office Manager now? [Y/n] ' answer </dev/tty; then
            printf '\nRun: %s\n' "$manager"
            return
        fi
    else
        printf 'Run: %s\n' "$manager"
        return
    fi
    case ${answer,,} in
        ""|y|yes)
            printf 'Launching Wine4Office Manager…\n'
            nohup "$manager" </dev/null >/dev/null 2>&1 9>&- &
            ;;
        n|no)
            printf 'Run later: %s\n' "$manager"
            ;;
        *)
            warn "unknown launch choice '$answer'; not launching"
            printf 'Run later: %s\n' "$manager"
            ;;
    esac
}

for command in curl flock install python3 sha256sum stat zstd; do
    command -v "$command" >/dev/null 2>&1 || fail "$command is required"
done
[[ $(uname -m) == x86_64 ]] || fail "only x86_64 Linux is currently supported"
[[ $METADATA_URL == https://* ]] || fail "metadata URL must use HTTPS"
ROOT=$(python3 - "$ROOT" "$HOME" <<'PY'
import pathlib
import sys
root = pathlib.Path(sys.argv[1]).expanduser().resolve(strict=False)
home = pathlib.Path(sys.argv[2]).expanduser().resolve(strict=False)
unsafe = {pathlib.Path("/"), home}
unsafe.update(pathlib.Path("/") / name for name in (
    "bin", "boot", "dev", "etc", "home", "lib", "lib64", "media", "mnt", "opt",
    "proc", "root", "run", "sbin", "srv", "sys", "tmp", "usr", "var",
))
if root in unsafe:
    raise SystemExit(f"unsafe installation root: {root}")
print(root)
PY
)
MANAGER_TARGET=$ROOT/bin/Wine4OfficeManager
UNINSTALLER_TARGET=$ROOT/bin/wine4office-uninstall
RUNNER_TARGET=$ROOT/runner

TMP=$(mktemp -d)
NEW_RUNNER=$ROOT/.runner.new.$$
NEW_MANAGER=$ROOT/bin/.Wine4OfficeManager.new.$$
NEW_UNINSTALLER=$ROOT/bin/.wine4office-uninstall.new.$$
RUNNER_BACKUP=$ROOT/.runner.old.$$
MANAGER_BACKUP=$ROOT/bin/.Wine4OfficeManager.old.$$
UNINSTALLER_BACKUP=$ROOT/bin/.wine4office-uninstall.old.$$
RUNNER_CHANGED=false
MANAGER_CHANGED=false
UNINSTALLER_CHANGED=false
COMMITTED=false
METADATA_CHANGED=()

cleanup() {
    status=$?
    if ! $COMMITTED; then
        for ((index=${#METADATA_CHANGED[@]} - 1; index >= 0; index--)); do
            name=${METADATA_CHANGED[index]}
            rm -f -- "$ROOT/$name"
            [[ ! -e $ROOT/.$name.old.$$ && ! -L $ROOT/.$name.old.$$ ]] || \
                mv -- "$ROOT/.$name.old.$$" "$ROOT/$name"
        done
        if $MANAGER_CHANGED; then
            rm -f -- "$MANAGER_TARGET"
            [[ ! -e $MANAGER_BACKUP && ! -L $MANAGER_BACKUP ]] || \
                mv -- "$MANAGER_BACKUP" "$MANAGER_TARGET"
        fi
        if $UNINSTALLER_CHANGED; then
            rm -f -- "$UNINSTALLER_TARGET"
            [[ ! -e $UNINSTALLER_BACKUP && ! -L $UNINSTALLER_BACKUP ]] || \
                mv -- "$UNINSTALLER_BACKUP" "$UNINSTALLER_TARGET"
        fi
        if $RUNNER_CHANGED; then
            rm -rf -- "$RUNNER_TARGET"
            [[ ! -e $RUNNER_BACKUP && ! -L $RUNNER_BACKUP ]] || \
                mv -- "$RUNNER_BACKUP" "$RUNNER_TARGET"
        fi
    fi
    rm -rf -- "$TMP" "$NEW_RUNNER"
    rm -f -- "$NEW_MANAGER" "$NEW_UNINSTALLER"
    if $COMMITTED; then
        rm -rf -- "$RUNNER_BACKUP"
        rm -f -- "$MANAGER_BACKUP"
        rm -f -- "$UNINSTALLER_BACKUP"
        for name in VERSION WINE_VERSION UPDATE_URL UPDATE_CHANNEL STANDALONE; do
            rm -f -- "$ROOT/.$name.old.$$"
        done
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cat > "$TMP/bounded_download.py" <<'PY'
import os
import pathlib
import sys

destination = pathlib.Path(sys.argv[1])
limit = int(sys.argv[2])
temporary = destination.with_suffix(destination.suffix + ".part")
written = 0
try:
    with temporary.open("wb") as output:
        while True:
            chunk = sys.stdin.buffer.read(1024 * 1024)
            if not chunk:
                break
            written += len(chunk)
            if written > limit:
                raise SystemExit(f"download exceeded {limit} bytes")
            output.write(chunk)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, destination)
finally:
    temporary.unlink(missing_ok=True)
PY

fetch_limited() {
    url=$1
    destination=$2
    maximum=$3
    curl --proto '=https' --tlsv1.2 --fail --silent --show-error --location \
        --retry 3 --retry-delay 2 "$url" | \
        python3 "$TMP/bounded_download.py" "$destination" "$maximum"
}

printf 'Fetching release metadata…\n'
fetch_limited "$METADATA_URL" "$TMP/release.json" 1048576
mapfile -t RELEASE < <(python3 - "$METADATA_URL" "$TMP/release.json" <<'PY'
import json
import re
import sys
import urllib.parse

source, path = sys.argv[1:]
with open(path, "rb") as release_file:
    payload = json.load(release_file)
if not isinstance(payload, dict) or payload.get("schema_version") != 1:
    raise SystemExit("release.json must use schema version 1")
channel = payload.get("channel")
if channel != "stable":
    raise SystemExit("release.json must use the stable channel")

def https_url(value, base, label):
    if not isinstance(value, str) or not value or any(ord(character) < 32 for character in value):
        raise SystemExit(f"invalid {label} URL")
    url = urllib.parse.urljoin(base, value)
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
        raise SystemExit(f"invalid {label} HTTPS URL")
    return url

def artifact(name, maximum, expected_format=None):
    component = payload.get(name)
    if not isinstance(component, dict):
        raise SystemExit(f"release.json is missing {name}")
    version = component.get("version")
    digest = component.get("sha256")
    size = component.get("size")
    if not isinstance(version, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,127}", version):
        raise SystemExit(f"invalid {name} version")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
        raise SystemExit(f"invalid {name} digest")
    if type(size) is not int or size <= 0 or size > maximum:
        raise SystemExit(f"invalid {name} size")
    if expected_format and component.get("format") != expected_format:
        raise SystemExit(f"invalid {name} archive format")
    return version, https_url(component.get("url"), source, name), digest.lower(), str(size)

manager = artifact("manager", 1024**3)
wine = artifact("wine", 8 * 1024**3, "tar.zst")
canonical = https_url(payload.get("metadata_url"), source, "metadata")
for field in (*manager, *wine, canonical):
    print(field)
PY
)
[[ ${#RELEASE[@]} -eq 9 ]] || fail "release metadata did not produce nine validated fields"
MANAGER_VERSION=${RELEASE[0]}
MANAGER_URL=${RELEASE[1]}
MANAGER_SHA256=${RELEASE[2]}
MANAGER_SIZE=${RELEASE[3]}
WINE_VERSION=${RELEASE[4]}
WINE_URL=${RELEASE[5]}
WINE_SHA256=${RELEASE[6]}
WINE_SIZE=${RELEASE[7]}
CANONICAL_METADATA_URL=${RELEASE[8]}

printf 'Downloading Wine4Office Manager %s…\n' "$MANAGER_VERSION"
fetch_limited "$MANAGER_URL" "$TMP/Wine4OfficeManager" "$MANAGER_SIZE"
printf 'Downloading Wine runner %s…\n' "$WINE_VERSION"
fetch_limited "$WINE_URL" "$TMP/wine.tar.zst" "$WINE_SIZE"

verify() {
    path=$1
    expected_size=$2
    expected_digest=$3
    label=$4
    actual_size=$(stat -c '%s' "$path")
    [[ $actual_size == "$expected_size" ]] || fail "$label size mismatch"
    actual_digest=$(sha256sum "$path")
    actual_digest=${actual_digest%% *}
    [[ $actual_digest == "$expected_digest" ]] || fail "$label SHA-256 mismatch"
}
verify "$TMP/Wine4OfficeManager" "$MANAGER_SIZE" "$MANAGER_SHA256" Wine4OfficeManager
verify "$TMP/wine.tar.zst" "$WINE_SIZE" "$WINE_SHA256" "Wine runner"
chmod 0755 "$TMP/Wine4OfficeManager"

cat > "$TMP/wine4office-uninstall" <<'SH'
#!/usr/bin/env bash
# Remove a standalone Wine4Office installation for the current user.
set -euo pipefail

SELF=$(readlink -f "$0")
ROOT=$(CDPATH= cd -- "$(dirname -- "$SELF")/.." && pwd)
CONFIG_HOME=${XDG_CONFIG_HOME:-$HOME/.config}
BIN_HOME=${WINE4OFFICE_BIN_HOME:-$HOME/.local/bin}
MANAGER=$ROOT/bin/Wine4OfficeManager
PURGE_RUNNER=false
REMOVE_PREFIX=

usage() {
    echo "Usage: $0 [--purge-runner] [--remove-prefix PATH]" >&2
}
while [[ $# -gt 0 ]]; do
    case $1 in
        --purge-runner) PURGE_RUNNER=true; shift ;;
        --remove-prefix)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            REMOVE_PREFIX=$2; shift 2 ;;
        *) usage; exit 2 ;;
    esac
done

[[ -f $ROOT/STANDALONE && $(<"$ROOT/STANDALONE") == Wine4OfficeManager ]] || {
    echo "Refusing to remove an installation without a valid Wine4Office standalone marker: $ROOT" >&2
    exit 1
}
[[ -x $MANAGER ]] || {
    echo "Cannot safely remove Wine4Office because its manager is missing: $MANAGER" >&2
    exit 1
}

cleanup_args=(--prepare-uninstall)
if [[ -n $REMOVE_PREFIX ]]; then cleanup_args+=(--remove-prefix "$REMOVE_PREFIX"); fi
"$MANAGER" "${cleanup_args[@]}"

for link in "$BIN_HOME/Wine4OfficeManager" "$BIN_HOME/wine4office-manager"; do
    if [[ -L $link && $(readlink -f "$link") == "$MANAGER" ]]; then rm -f -- "$link"; fi
done
if $PURGE_RUNNER; then
    rm -rf -- "$ROOT/runner"
    rm -f -- "$ROOT/VERSION" "$ROOT/WINE_VERSION" "$ROOT/UPDATE_URL" \
        "$ROOT/UPDATE_CHANNEL" "$ROOT/STANDALONE" "$ROOT/install.json" \
        "$ROOT/.wine4office-update.lock"
    rm -rf -- "$CONFIG_HOME/wine4office"
fi
rm -f -- "$MANAGER" "$MANAGER.version" "$SELF"
rmdir -- "$ROOT/bin" "$ROOT" 2>/dev/null || true

if $PURGE_RUNNER; then
    echo "Wine4Office runner, manager, shortcuts, and configuration removed."
else
    printf 'Wine4Office Manager removed. Configuration and the runner in %s/runner were preserved.\n' "$ROOT"
fi
SH
chmod 0755 "$TMP/wine4office-uninstall"

cat > "$TMP/safe_extract.py" <<'PY'
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile

ARCHIVE = pathlib.Path(sys.argv[1])
DESTINATION = pathlib.Path(sys.argv[2])
MAX_MEMBERS = 100_000
MAX_FILE_SIZE = 4 * 1024**3
MAX_TOTAL_SIZE = 16 * 1024**3

def safe_parts(value, base=()):
    path = pathlib.PurePosixPath(value)
    if path.is_absolute():
        raise ValueError(f"absolute archive path: {value!r}")
    parts = list(base)
    for part in path.parts:
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                raise ValueError(f"archive path escapes root: {value!r}")
            parts.pop()
        else:
            parts.append(part)
    if not parts:
        raise ValueError(f"empty archive path: {value!r}")
    return tuple(parts)

def checked_member(member):
    path = safe_parts(member.name)
    if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
        raise ValueError(f"unsafe special archive member: {member.name!r}")
    if member.size < 0 or (member.isfile() and member.size > MAX_FILE_SIZE):
        raise ValueError(f"oversized archive member: {member.name!r}")
    if member.issym():
        safe_parts(member.linkname, path[:-1])
    elif member.islnk():
        safe_parts(member.linkname)
    return path

def archive_stream():
    process = subprocess.Popen(
        ["zstd", "-dc", "--", str(ARCHIVE)],
        stdout=subprocess.PIPE,
    )
    if process.stdout is None:
        raise RuntimeError("zstd did not provide an output stream")
    bundle = tarfile.open(fileobj=process.stdout, mode="r|")
    return process, bundle

def close_stream(process, bundle):
    bundle.close()
    if process.stdout is not None:
        process.stdout.close()
    if process.wait() != 0:
        raise ValueError("Wine artifact is not a valid tar.zst archive")

def validated_members(bundle):
    count = 0
    total = 0
    paths = set()
    roots = set()
    for member in bundle:
        count += 1
        if count > MAX_MEMBERS:
            raise ValueError("Wine archive exceeds the member-count limit")
        path = checked_member(member)
        if path in paths:
            raise ValueError(f"duplicate archive path: {member.name!r}")
        paths.add(path)
        roots.add(path[0])
        if member.isfile():
            total += member.size
            if total > MAX_TOTAL_SIZE:
                raise ValueError("Wine archive exceeds the total declared-size limit")
        yield member, path
    if len(roots) != 1:
        raise ValueError("Wine archive must contain exactly one runner root")

def target_path(path):
    target = DESTINATION.joinpath(*path)
    try:
        target.parent.resolve().relative_to(DESTINATION.resolve())
    except ValueError as error:
        raise ValueError(f"archive extraction escapes destination: {target}") from error
    return target

process, bundle = archive_stream()
try:
    for _member, _path in validated_members(bundle):
        pass
finally:
    close_stream(process, bundle)

DESTINATION.mkdir(parents=True, exist_ok=False)
try:
    process, bundle = archive_stream()
    directory_modes = []
    hard_links = []
    extracted_total = 0
    try:
        for member, path in validated_members(bundle):
            target = target_path(path)
            target.parent.mkdir(parents=True, exist_ok=True)
            if member.isdir():
                target.mkdir(exist_ok=True)
                directory_modes.append((target, member.mode & 0o777))
                continue
            if member.issym():
                os.symlink(member.linkname, target)
                continue
            if member.islnk():
                hard_links.append((target, safe_parts(member.linkname)))
                continue
            source = bundle.extractfile(member)
            if source is None:
                raise ValueError(f"archive file has no data: {member.name!r}")
            written = 0
            try:
                with target.open("xb") as output:
                    while True:
                        chunk = source.read(1024 * 1024)
                        if not chunk:
                            break
                        written += len(chunk)
                        extracted_total += len(chunk)
                        if written > member.size or written > MAX_FILE_SIZE:
                            raise ValueError(f"archive file exceeds declared size: {member.name!r}")
                        if extracted_total > MAX_TOTAL_SIZE:
                            raise ValueError("Wine archive exceeds the extracted-size limit")
                        output.write(chunk)
            finally:
                source.close()
            if written != member.size:
                raise ValueError(f"archive file size changed: {member.name!r}")
            target.chmod(member.mode & 0o777)
        for target, source_parts in hard_links:
            source = target_path(source_parts)
            try:
                source.resolve().relative_to(DESTINATION.resolve())
            except ValueError as error:
                raise ValueError(f"hard-link target escapes destination: {source}") from error
            if not source.is_file():
                raise ValueError(f"hard-link target is missing: {source}")
            os.link(source, target)
        for directory, mode in reversed(directory_modes):
            directory.chmod(mode)
    finally:
        close_stream(process, bundle)
    roots = list(DESTINATION.iterdir())
    if len(roots) != 1 or not roots[0].is_dir() or not (roots[0] / "bin/wine").is_file():
        raise ValueError("Wine archive does not contain one runner with bin/wine")
    if not os.access(roots[0] / "bin/wine", os.X_OK):
        raise ValueError("Wine archive bin/wine is not executable")
    print(roots[0])
except Exception:
    shutil.rmtree(DESTINATION, ignore_errors=True)
    raise
PY
EXTRACTED_ROOT=$(python3 "$TMP/safe_extract.py" "$TMP/wine.tar.zst" "$TMP/extracted")

mkdir -p "$ROOT/bin" "$BIN_HOME" "$TMP/metadata"
install -m 0755 "$TMP/Wine4OfficeManager" "$NEW_MANAGER"
install -m 0755 "$TMP/wine4office-uninstall" "$NEW_UNINSTALLER"
mv -- "$EXTRACTED_ROOT" "$NEW_RUNNER"
printf '%s\n' "$MANAGER_VERSION" > "$TMP/metadata/VERSION"
printf '%s\n' "$WINE_VERSION" > "$TMP/metadata/WINE_VERSION"
printf '%s\n' "$CANONICAL_METADATA_URL" > "$TMP/metadata/UPDATE_URL"
printf 'stable\n' > "$TMP/metadata/UPDATE_CHANNEL"
printf 'Wine4OfficeManager\n' > "$TMP/metadata/STANDALONE"
chmod 0644 "$TMP/metadata"/*

exec 9> "$ROOT/.wine4office-update.lock"
flock 9
trap '' INT TERM
for path in "$RUNNER_BACKUP" "$MANAGER_BACKUP" "$UNINSTALLER_BACKUP"; do
    [[ ! -e $path && ! -L $path ]] || fail "stale installation backup exists: $path"
done
if [[ -e $RUNNER_TARGET || -L $RUNNER_TARGET ]]; then mv -- "$RUNNER_TARGET" "$RUNNER_BACKUP"; fi
RUNNER_CHANGED=true
mv -- "$NEW_RUNNER" "$RUNNER_TARGET"
if [[ -e $MANAGER_TARGET || -L $MANAGER_TARGET ]]; then mv -- "$MANAGER_TARGET" "$MANAGER_BACKUP"; fi
MANAGER_CHANGED=true
mv -- "$NEW_MANAGER" "$MANAGER_TARGET"
if [[ -e $UNINSTALLER_TARGET || -L $UNINSTALLER_TARGET ]]; then
    mv -- "$UNINSTALLER_TARGET" "$UNINSTALLER_BACKUP"
fi
UNINSTALLER_CHANGED=true
mv -- "$NEW_UNINSTALLER" "$UNINSTALLER_TARGET"
for name in VERSION WINE_VERSION UPDATE_URL UPDATE_CHANNEL STANDALONE; do
    backup=$ROOT/.$name.old.$$
    [[ ! -e $backup && ! -L $backup ]] || fail "stale metadata backup exists: $backup"
    if [[ -e $ROOT/$name || -L $ROOT/$name ]]; then mv -- "$ROOT/$name" "$backup"; fi
    METADATA_CHANGED+=("$name")
    mv -- "$TMP/metadata/$name" "$ROOT/$name"
done
COMMITTED=true
trap 'exit 130' INT
trap 'exit 143' TERM

ln -sfn "$MANAGER_TARGET" "$BIN_HOME/Wine4OfficeManager" || \
    warn "could not create $BIN_HOME/Wine4OfficeManager"
XDG_DATA_HOME="$DATA_HOME" "$MANAGER_TARGET" --install-shortcut >/dev/null || \
    warn "manager installed, but the application-menu shortcut could not be created"

printf 'Installed Wine4Office Manager %s and Wine runner %s.\n' "$MANAGER_VERSION" "$WINE_VERSION"
prompt_and_launch_manager "$BIN_HOME/Wine4OfficeManager"
