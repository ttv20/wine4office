#!/bin/sh
# Register Office-downloaded cloud fonts inside the user's own Wine prefix.
# The project does not redistribute these font files.
set -eu

prefix=${WINEPREFIX:-$HOME/.wine}
marker="$prefix/.wine4office-managed-prefix"
marker_info=$(stat -c '%F:%u:%a' -- "$marker" 2>/dev/null || true)
[ "$marker_info" = "regular file:$(id -u):600" ] || {
    echo "wine4office: refusing cloud-font registration in an unowned prefix" >&2
    exit 0
}
marker_text=$(cat "$marker" 2>/dev/null || true)
marker_size=$(wc -c < "$marker" 2>/dev/null | tr -d ' ' || true)
[ "$marker_size" = 29 ] &&
    [ "$marker_text" = "Wine4OfficeManager prefix v1" ] || {
    echo "wine4office: refusing cloud-font registration in an unowned prefix" >&2
    exit 0
}
fonts_dir="$prefix/drive_c/windows/Fonts"
tmp=${TMPDIR:-/tmp}
font_list="$tmp/wine4office-office-cloud-fonts.$$"
reg_file="$tmp/wine4office-office-cloud-fonts.$$.reg"
state_file="$prefix/.wine4office-cloud-fonts.sha256"
state_tmp="$prefix/.wine4office-cloud-fonts.sha256.$$"
source_state_file="$prefix/.wine4office-cloud-fonts.sources.sha256"
source_state_tmp="$prefix/.wine4office-cloud-fonts.sources.sha256.$$"
trap 'rm -f "$font_list" "$reg_file" "$state_tmp" "$source_state_tmp"' EXIT INT TERM

find "$prefix/drive_c/users" -type f \( \
    -ipath '*/AppData/Local/Microsoft/FontCache/*/CloudFonts/*.ttf' -o \
    -ipath '*/AppData/Local/Microsoft/FontCache/*/CloudFonts/*.ttc' -o \
    -ipath '*/AppData/Local/Microsoft/FontCache/*/CloudFonts/*.otf' \
    \) -print 2>/dev/null | LC_ALL=C sort -u > "$font_list" || true

[ -s "$font_list" ] || {
    rm -f "$source_state_file"
    echo "wine4office: no Office cloud fonts found; leaving Wine fonts unchanged"
    exit 0
}

# A full pass hashes and inspects every font with fc-scan. Avoid putting that
# work on every Office launch when the source set is unchanged. Size, mtime and
# ctime catch normal downloads, replacements and in-place edits without reading
# all font contents on the fast path.
source_hash=$(
    while IFS= read -r source; do
        stat -Lc '%n	%s	%y	%z' "$source"
    done < "$font_list" |
    sha256sum | awk '{print $1}'
)
if [ -f "$state_file" ] && [ -f "$source_state_file" ] &&
   [ "$(cat "$source_state_file")" = "$source_hash" ]; then
    echo "wine4office: Office cloud font sources are unchanged"
    exit 0
fi

command -v fc-scan >/dev/null 2>&1 || {
    echo "wine4office: fc-scan is required to register Office cloud fonts" >&2
    exit 1
}

mkdir -p "$fonts_dir"
{
    printf 'Windows Registry Editor Version 5.00\r\n\r\n'
    printf '[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts]\r\n'

    while IFS= read -r source; do
        hash=$(sha256sum "$source" | awk '{print substr($1, 1, 20)}')
        extension=${source##*.}
        filename=$(printf 'OFFICECLOUD_%s.%s' "$hash" "$extension" | tr '[:lower:]' '[:upper:]')
        destination="$fonts_dir/$filename"
        if [ ! -f "$destination" ] || ! cmp -s "$source" "$destination"; then
            cp "$source" "$destination"
        fi

        fc-scan --format='%{fullname[0]}\n' "$source" 2>/dev/null |
        while IFS= read -r full_name; do
            [ -n "$full_name" ] || continue
            escaped_name=$(printf '%s' "$full_name (TrueType)" | sed 's/\\/\\\\/g; s/"/\\"/g')
            printf '"%s"="%s"\r\n' "$escaped_name" "$filename"
        done
    done < "$font_list"
} > "$reg_file"

reg_hash=$(sha256sum "$reg_file" | awk '{print $1}')
if [ -f "$state_file" ] && [ "$(cat "$state_file")" = "$reg_hash" ]; then
    printf '%s\n' "$source_hash" > "$source_state_tmp"
    mv -f "$source_state_tmp" "$source_state_file"
    echo "wine4office: Office cloud font registration is already current"
    exit 0
fi

wine regedit /S "$(winepath -w "$reg_file")"
printf '%s\n' "$reg_hash" > "$state_tmp"
mv -f "$state_tmp" "$state_file"
printf '%s\n' "$source_hash" > "$source_state_tmp"
mv -f "$source_state_tmp" "$source_state_file"
count=$(wc -l < "$font_list" | tr -d ' ')
echo "wine4office: registered $count Office cloud font files in C:\\windows\\Fonts"
