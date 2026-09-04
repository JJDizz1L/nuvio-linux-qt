#!/usr/bin/env bash
# Assemble an AppDir from a cmake install tree and turn it into an AppImage.
# Usage: build-appimage.sh <install-root> <output-appimage> [appimagetool]
#   <install-root>   DESTDIR-style tree (cmake --install <build> --prefix <root>)
#   <output-appimage> destination file (dir is created)
#   [appimagetool]    default: appimagetool on PATH
# Env: APPIMAGE_UPDATE_INFORMATION (zsync string feeding the A10 updater,
#   e.g. "gh-releases-zsync|JJDizz1L|nuvio-linux-qt|latest|*.AppImage.zsync").
#
# Library vendoring is a small auditable ldd loop (NOT linuxdeploy: its
# bundled strip predates DT_RELR (.relr.dyn) and fatally chokes on
# contemporary distro libraries). Host-provided by design: libc family,
# NSS, and the GL/Vulkan/dri driver stack (LD_LIBRARY_PATH covers the
# bundled closure, so no patchelf/rpath rewriting is needed - AppRun is
# the only entry point).
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <install-root> <output-appimage> [appimagetool]" >&2
    exit 2
fi
app_root="$1"
output_appimage="$2"
appimagetool_path="${3:-appimagetool}"
update_information="${APPIMAGE_UPDATE_INFORMATION:-}"

if [[ -d "$app_root/usr" ]]; then
    bindir="$app_root/usr/bin"
    sharedir="$app_root/usr/share"
else
    bindir="$app_root/bin"
    sharedir="$app_root/share"
fi
if [[ ! -x "$bindir/nuvio-linux-qt" ]]; then
    echo "Install tree has no nuvio-linux-qt binary: $app_root" >&2
    exit 1
fi
if ! command -v "$appimagetool_path" >/dev/null 2>&1; then
    echo "appimagetool not found: $appimagetool_path" >&2
    exit 1
fi
if ! command -v qmake6 >/dev/null 2>&1 && ! command -v qmake >/dev/null 2>&1; then
    echo "qmake (qt6-base) is required to locate Qt plugins/QML." >&2
    exit 1
fi
qmake_bin="$(command -v qmake6 || command -v qmake)"
qt_plugins="$("$qmake_bin" -query QT_INSTALL_PLUGINS)"
qt_qml="$("$qmake_bin" -query QT_INSTALL_QML)"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/nuvio-appimage.XXXXXX")"
cleanup() { rm -rf "$work_dir"; }
trap cleanup EXIT

app_dir="$work_dir/Nuvio.AppDir"
mkdir -p "$app_dir/usr/bin" "$app_dir/usr/lib" "$app_dir/usr/share"
cp -a "$bindir/nuvio-linux-qt" "$app_dir/usr/bin/"
[[ -f "$bindir/TorrServer" ]] && cp -a "$bindir/TorrServer" "$app_dir/usr/bin/"
cp -a "$sharedir"/. "$app_dir/usr/share/"

# Host-provided: libc family, NSS, and the GL/Vulkan/driver stack. (GL
# dispatch and dri drivers must match the host kernel/drivers; bundling
# them breaks rendering on foreign machines.)
exclude_re='^lib(c|m|pthread|dl|rt|nsl|util|resolv|nss[^/]*|gcc_s)\.so|libGL[^/]*\.so|libEGL[^/]*\.so|libOpenGL[^/]*\.so|libGLX[^/]*\.so|libGLdispatch[^/]*\.so|libvulkan[^/]*\.so|libdrm_amdgpu|libxcb-dri|dri/|vdpau/|va/drivers/'

vendored=""
vendor_elf() {
    local elf="$1"
    local line lib path base
    while IFS= read -r line; do
        lib="${line%% *}"
        path="$(printf '%s' "$line" | sed -n 's/.*=> \([^ ]*\) .*/\1/p')"
        [[ -z "$path" || "$path" == *"not found"* ]] && continue
        base="$(basename "$path")"
        if [[ "$base" =~ $exclude_re ]]; then
            continue
        fi
        if [[ " $vendored " != *" $base "* ]]; then
            vendored="$vendored $base"
            cp -a "$path" "$app_dir/usr/lib/"
            vendor_elf "$path"
        fi
    done < <(ldd "$elf" 2>/dev/null | grep '=>')
}

vendor_elf "$app_dir/usr/bin/nuvio-linux-qt"

# Qt platform + integration plugins this app can hit (xcb/wayland GL,
# svg icons, TLS for every https fetch the app performs).
for plugin in platforms/libqxcb.so \
    platforms/libqwayland-generic.so platforms/libqwayland-egl.so \
    xcbglintegrations/libqxcb-glx-integration.so \
    xcbglintegrations/libqxcb-egl-integration.so \
    wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
    imageformats/libqsvg.so \
    tls/libqopensslbackend.so; do
    if [[ -f "$qt_plugins/$plugin" ]]; then
        mkdir -p "$app_dir/usr/plugins/$(dirname "$plugin")"
        cp -a "$qt_plugins/$plugin" "$app_dir/usr/plugins/$plugin"
        vendor_elf "$app_dir/usr/plugins/$plugin"
    fi
done

# QML modules matching the app's imports (QtQuick incl. Controls/Basic/
# Layouts/Window/Templates, QtQml).
mkdir -p "$app_dir/usr/qml"
for module in QtQuick QtQml; do
    if [[ -d "$qt_qml/$module" ]]; then
        cp -a "$qt_qml/$module" "$app_dir/usr/qml/"
    fi
done
for qml_elf in $(find "$app_dir/usr/qml" -name '*.so'); do
    vendor_elf "$qml_elf"
done

# Authoritative desktop entry (Exec=AppRun target, scheme handlers kept).
cat > "$app_dir/io.github.jjdizz1l.NuvioLinux.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Version=1.0
Name=Nuvio
GenericName=Media Player
Comment=Discover movies and shows, keep your library and watch progress in sync
Exec=nuvio-linux-qt %U
Icon=io.github.jjdizz1l.NuvioLinux
Terminal=false
Categories=AudioVideo;Video;Player;
StartupNotify=true
MimeType=x-scheme-handler/nuvio;x-scheme-handler/stremio;
EOF

icon_src="$(find "$app_dir/usr/share" -name 'io.github.jjdizz1l.NuvioLinux.svg' | head -1)"
if [[ -z "$icon_src" ]]; then
    echo "Expected icon in install tree." >&2
    exit 1
fi
cp "$icon_src" "$app_dir/io.github.jjdizz1l.NuvioLinux.svg"
ln -sf "io.github.jjdizz1l.NuvioLinux.svg" "$app_dir/.DirIcon"

cat > "$app_dir/AppRun" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$here/usr/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$here/usr/plugins:${QT_PLUGIN_PATH:-}"
export QML2_IMPORT_PATH="$here/usr/qml:${QML2_IMPORT_PATH:-}"
export XDG_DATA_DIRS="$here/usr/share:${XDG_DATA_DIRS:-/usr/share}"
exec "$here/usr/bin/nuvio-linux-qt" "$@"
EOF
chmod +x "$app_dir/AppRun"

lib_count="$(find "$app_dir/usr/lib" | wc -l)"
printf 'Vendored %s libraries + Qt plugins/QML into the AppDir.\n' "$lib_count"

mkdir -p "$(dirname "$output_appimage")"
appimagetool_args=()
# AppImage-distributed appimagetool (FUSE-less CI) needs extraction first:
# set APPIMAGETOOL_EXTRACT_AND_RUN=1 there. Native builds reject the flag.
if [[ "${APPIMAGETOOL_EXTRACT_AND_RUN:-}" == "1" ]]; then
    appimagetool_args+=(--appimage-extract-and-run)
fi
appimagetool_args+=("$app_dir" "$output_appimage")
if [[ -n "$update_information" ]]; then
    appimagetool_args=(-u "$update_information" "${appimagetool_args[@]}")
fi
ARCH=x86_64 "$appimagetool_path" "${appimagetool_args[@]}"

[[ -f "$output_appimage" ]] || {
    echo "AppImage was not produced: $output_appimage" >&2
    exit 1
}
chmod +x "$output_appimage"
printf 'Built AppImage artifact: %s\n' "$output_appimage"
