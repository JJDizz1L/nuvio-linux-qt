#!/usr/bin/env bash
# Verify the Qt-line packaging assets without building a package.
# Checks: desktop entry validates + carries scheme handlers, metainfo
# validates + id matches, icon exists, flatpak manifest has the sandbox
# essentials, PKGBUILD version tracks the project, scripts are executable.
# External validators run only when installed (never fatal when absent).
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"
failures=0
check() {
    local message="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        printf 'ok: %s\n' "$message"
    else
        printf 'FAIL: %s\n' "$message"
        failures=$((failures + 1))
    fi
}

desktop="$repo_root/packaging/io.github.jjdizz1l.NuvioLinux.desktop"
metainfo="$repo_root/packaging/io.github.jjdizz1l.NuvioLinux.metainfo.xml"
icon="$repo_root/packaging/icons/io.github.jjdizz1l.NuvioLinux.svg"
flatpak_manifest="$repo_root/packaging/flatpak/io.github.jjdizz1l.NuvioLinux.yml"
pkgbuild="$repo_root/packaging/aur/PKGBUILD"

check "desktop file exists" test -f "$desktop"
check "metainfo exists" test -f "$metainfo"
check "icon exists" test -f "$icon"
check "Exec line" grep -q '^Exec=nuvio-linux-qt %U$' "$desktop"
check "scheme handlers" grep -q 'x-scheme-handler/nuvio.*x-scheme-handler/stremio' "$desktop"
check "icon reference" grep -q '^Icon=io.github.jjdizz1l.NuvioLinux$' "$desktop"
check "metainfo id" grep -q '<id>io.github.jjdizz1l.NuvioLinux</id>' "$metainfo"
check "launchable matches desktop id" grep -q 'io.github.jjdizz1l.NuvioLinux.desktop' "$metainfo"

if command -v desktop-file-validate >/dev/null 2>&1; then
    check "desktop-file-validate" desktop-file-validate "$desktop"
else
    echo "skip: desktop-file-validate absent"
fi
if command -v appstreamcli >/dev/null 2>&1; then
    check "appstreamcli validate" appstreamcli validate --no-net "$metainfo"
else
    echo "skip: appstreamcli absent"
fi

for perm in wayland x11 network pulseaudio; do
    check "flatpak $perm" grep -q -- "--socket=$perm\|--share=$perm" "$flatpak_manifest"
done
check "flatpak notifications" grep -q 'org.freedesktop.Notifications' "$flatpak_manifest"
check "flatpak libmpv pinned" grep -q 'v0.41.0.tar.gz' "$flatpak_manifest"

project_version="$(grep -A2 '^project(NuvioLinuxQt' "$repo_root/CMakeLists.txt" \
    | grep -o '[0-9][0-9.]*' | head -1)"
check "pkgver tracks project ($project_version)" grep -q "^pkgver=$project_version\$" "$pkgbuild"

for script in appimage/build-appimage.sh sign-artifacts.sh verify-packaging.sh; do
    check "executable $script" test -x "$repo_root/packaging/$script"
done

if [[ "$failures" -gt 0 ]]; then
    printf 'verify-packaging: %d FAILURES\n' "$failures" >&2
    exit 1
fi
printf 'verify-packaging: all green\n'
