#!/usr/bin/env bash
set -euo pipefail
mode=${1:?usage: test-package-builders.sh deb|rpm|community}
root=$(cd "$(dirname "$0")/../../../../.." && pwd)
distribution=$root/tools/wine4office-manager/packaging/distribution
release_builder=$root/tools/wine4office-manager/packaging/build-release-artifacts.sh
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
chmod 0755 "$tmp"
version=2.3.4
runner=$tmp/runner
manager=$tmp/Wine4OfficeManager
release=$tmp/release
mkdir -p "$runner/bin" "$runner/share/wine/gecko" "$runner/share/wine/mono"
cat > "$runner/bin/wine" <<'EOF'
#!/bin/sh
echo 'wine4office-2.3.4 (Wine 11.17)'
EOF
cat > "$manager" <<'EOF'
#!/bin/sh
test "${1:-}" = --smoke-test || true
exit 0
EOF
chmod 0755 "$runner/bin/wine" "$manager"
printf x > "$runner/share/wine/gecko/wine-gecko-2.47.4-x86.msi"
printf x > "$runner/share/wine/gecko/wine-gecko-2.47.4-x86_64.msi"
printf x > "$runner/share/wine/mono/wine-mono-11.3.0-x86.msi"
"$release_builder" "$runner" "$manager" "$release" "$version" \
    https://updates.example/releases/release.json \
    https://updates.example/releases  stable 11.17 >/dev/null

verify_installation() {
    [[ $(/opt/wine4office/runner/bin/wine --version) == \
        'wine4office-2.3.4 (Wine 11.17)' ]]
    /opt/wine4office/bin/Wine4OfficeManager --smoke-test
    cmp "$manager" /opt/wine4office/bin/Wine4OfficeManager
    cmp "$runner/bin/wine" /opt/wine4office/runner/bin/wine
    [[ $(cat /opt/wine4office/WINE_BASE_VERSION) == 11.17 ]]
    python3 - <<'PY'
import json
from pathlib import Path
data = json.loads(Path("/opt/wine4office/PACKAGE-INSTALLATION.json").read_text())
assert data["components"] == ["manager", "wine"]
assert data["package"] == "wine4office"
PY
}

case $mode in
    deb)
        "$distribution/build-deb.sh" "$release/Wine4OfficeManager-${version}-x86_64" \
            "$release/wine4office-${version}-x86_64.tar.zst" "$version" \
            apt wine4office "$tmp/packages"
        deb=$tmp/packages/wine4office_2.3.4_amd64.deb
        dpkg-deb -f "$deb" Depends | grep -F 'libgssapi-krb5-2' >/dev/null
        dpkg-deb -f "$deb" Depends | grep -F 'libxcb-cursor0' >/dev/null
        "$distribution/build-apt-repository.sh" "$tmp/repository" stable "$deb"
        printf 'deb [trusted=yes] file:%s stable main\n' "$tmp/repository" \
            > /etc/apt/sources.list.d/wine4office-test.list
        apt-get update >/dev/null
        apt-get install -y --no-install-recommends wine4office >/dev/null
        verify_installation
        [[ $(python3 -c 'import json; print(json.load(open("/opt/wine4office/PACKAGE-INSTALLATION.json"))["provider"])') == apt ]]
        ;;
    rpm)
        "$distribution/build-rpm.sh" "$release/Wine4OfficeManager-${version}-x86_64" \
            "$release/wine4office-${version}-x86_64.tar.zst" "$version" \
            dnf wine4office "$tmp/packages"
        rpm=$(find "$tmp/packages" -name '*.rpm' -print -quit)
        rpm -qp --requires "$rpm" | grep -Fx 'krb5-libs' >/dev/null
        rpm -qp --requires "$rpm" | grep -Fx 'xcb-util-cursor' >/dev/null
        "$distribution/build-rpm.sh" "$release/Wine4OfficeManager-${version}-x86_64" \
            "$release/wine4office-${version}-x86_64.tar.zst" \
            "${version}+build.5" dnf wine4office "$tmp/build-metadata-packages"
        find "$tmp/build-metadata-packages" \
            -name 'wine4office-2.3.4-1.build.5*.x86_64.rpm' -print -quit \
            | grep -q .
        "$distribution/build-rpm-repository.sh" "$tmp/repository" "$rpm"
        cmp "$distribution/wine4office.repo" "$tmp/repository/wine4office.repo"
        cat > /etc/yum.repos.d/wine4office-test.repo <<EOF
[wine4office-test]
name=Wine4Office test
baseurl=file://$tmp/repository/rpm/x86_64
enabled=1
gpgcheck=0
EOF
        dnf install -y wine4office >/dev/null
        verify_installation
        [[ $(python3 -c 'import json; print(json.load(open("/opt/wine4office/PACKAGE-INSTALLATION.json"))["provider"])') == dnf ]]
        ;;
    community)
        "$distribution/generate-community-packages.py" \
            "$release/release.json" "$tmp/community"
        bash -n "$tmp/community/aur/PKGBUILD"
        grep -F 'pkgname = wine4office-bin' "$tmp/community/aur/.SRCINFO" >/dev/null
        grep -F $'\tdepends = krb5' "$tmp/community/aur/.SRCINFO" >/dev/null
        grep -F $'\tdepends = xcb-util-cursor' \
            "$tmp/community/aur/.SRCINFO" >/dev/null
        grep -F 'wineBaseVersion = "11.17";' "$tmp/community/nix/release.nix" >/dev/null
        if command -v makepkg >/dev/null; then
            chmod -R a+rwX "$tmp/community/aur"
            if [[ $EUID -eq 0 ]]; then
                (cd "$tmp/community/aur" && \
                    runuser -u nobody -- env HOME=/tmp makepkg --printsrcinfo) \
                    > "$tmp/generated.SRCINFO"
            else
                (cd "$tmp/community/aur" && makepkg --printsrcinfo) \
                    > "$tmp/generated.SRCINFO"
            fi
            diff -u "$tmp/community/aur/.SRCINFO" "$tmp/generated.SRCINFO"
        fi
        if command -v nix-instantiate >/dev/null; then
            nix-instantiate --parse "$tmp/community/nix/flake.nix" >/dev/null
            nix-instantiate --parse "$tmp/community/nix/package.nix" >/dev/null
            nix-instantiate --parse "$tmp/community/nix/release.nix" >/dev/null
        fi
        python3 - "$release/release.json" "$tmp/adversarial.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    payload = json.load(source)
payload["manager"]["url"] = "https://updates.example/artifact'${builtins.abort(null)}"
with open(sys.argv[2], "w", encoding="utf-8") as target:
    json.dump(payload, target)
PY
        "$distribution/generate-community-packages.py" \
            "$tmp/adversarial.json" "$tmp/adversarial"
        bash -n "$tmp/adversarial/aur/PKGBUILD"
        # shellcheck disable=SC2016 # Verify a literal escaped Nix interpolation.
        grep -F '\${builtins.abort(null)}' \
            "$tmp/adversarial/nix/release.nix" >/dev/null
        python3 - "$release/release.json" "$tmp/invalid-base.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    payload = json.load(source)
payload["wine"]["base_version"] = "11.17;invalid"
with open(sys.argv[2], "w", encoding="utf-8") as target:
    json.dump(payload, target)
PY
        if "$distribution/generate-community-packages.py" \
                "$tmp/invalid-base.json" "$tmp/invalid-base" >/dev/null 2>&1; then
            echo "Community package generator accepted an invalid Wine base version" >&2
            exit 1
        fi
        ;;
    *) echo "Unknown mode: $mode" >&2; exit 2 ;;
esac

echo "$mode distribution package and repository: PASS"
