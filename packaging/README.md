# Packaging (Qt line)

All formats build from the same cmake install tree. Dev builds stay lean:
`NUVIO_INSTALL` defaults OFF; packagers configure `-DNUVIO_INSTALL=ON`.

```bash
cmake -S . -B build/pkg -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON -DNUVIO_INSTALL=ON -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/pkg && ctest --test-dir build/pkg
DESTDIR="$PWD/pkgroot" cmake --install build/pkg
cmake --build build/pkg --target package   # .deb + .rpm via CPack
./packaging/appimage/build-appimage.sh "$PWD/pkgroot" NuvioLinuxQt-x86_64.AppImage
```

## Formats

- **DEB/RPM** (`cmake/Packaging.cmake`): FHS layout (`/usr/bin`,
  `/usr/share`), runtime deps via dpkg-shlibdeps/rpm-autoreq from the
  actual link (Qt6 modules + libmpv.so.2 — verified with `readelf -d`).
  A Depends-less DEB means the builder distro has no shlibdeps database
  (e.g. Arch); release DEBs are built on Debian/Ubuntu runners where it
  resolves. Hand-pinned package names are deliberately avoided (t64
  transitions make them per-release lies).
- **AppImage** (`packaging/appimage/`): auditable ldd vendoring (260ish
  libs + Qt platform/xcb/wayland/svg/TLS plugins + QtQuick/QtQml
  modules). Host-provided by design: libc family, NSS, GL/Vulkan/dri
  (drivers must match the host). linuxdeploy is NOT used — its bundled
  strip predates DT_RELR and dies on contemporary distro libraries.
  `APPIMAGE_UPDATE_INFORMATION` embeds the zsync string the in-app
  updater polls. `APPIMAGETOOL_EXTRACT_AND_RUN=1` for FUSE-less runners.
- **Flatpak** (`packaging/flatpak/`): KDE 6.8 runtime, pinned libmpv
  v0.41.0 build, finish-args mirror the app's needs (GL, audio,
  downloads, pipewire, Discord IPC, session notifications). Validate
  with `flatpak-builder --show-manifest` (full builds need the SDK).
- **AUR** (`packaging/aur/PKGBUILD`): release-tarball package; bump
  `pkgver` per release and refresh `.SRCINFO` with `makepkg --printsrcinfo`.
- **Signing** (`packaging/sign-artifacts.sh`): detached `.asc` per
  artifact when `NUVIO_SIGN_KEY` is set (keyring seeded from the
  `NUVIO_SIGN_KEYRING` secret in CI); keyless runs skip loudly but pass.
- **Release** (`.github/workflows/release.yml`): tag `v*` → build, test,
  DEB/RPM, AppImage+zsync, sign, verify, publish to the GitHub release
  the updater reads. Unrun as of writing (no tag cut yet).

## Portals

No portal helper code is needed: file access stays inside
`--filesystem` scopes, `xdg-open` resolves through the host portal
automatically under Flatpak, and desktop alerts use the session
notification bus (granted in the manifest). Qt's own platform
integration covers file dialogs if any land later.
