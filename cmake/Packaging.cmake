# cmake/Packaging.cmake — CPack DEB/RPM cutover (Appendix A). Included
# from the root only when NUVIO_INSTALL=ON. Runtime dependencies resolve
# via shlibdeps/rpm-autoreq from the actual link (libmpv + system Qt6),
# so the lists below stay honest without hand pinning.
set(CPACK_PACKAGE_NAME "nuvio-linux-qt")
set(CPACK_PACKAGE_VENDOR "JJDizz1L")
set(CPACK_PACKAGE_CONTACT "https://github.com/JJDizz1L/nuvio-linux-qt/issues")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Nuvio media center for Linux (Qt Quick line)")
set(CPACK_PACKAGE_DESCRIPTION
    "Qt Quick media center: addon catalogs, torrent and debrid playback, \
offline downloads, Trakt/SIMKL scrobbling, and watch-state sync \
data-compatible with the Compose product line.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# DEB (dpkg-shlibdeps derives Depends from the link closure).
set(CPACK_DEB_COMPONENT_INSTALL OFF)
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libnotify-bin")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE
    "https://github.com/JJDizz1L/nuvio-linux-qt")

# RPM (autoreq/autoprov derive Requires from the link closure).
set(CPACK_RPM_PACKAGE_SUMMARY "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _binary_payload w9.xzdio")
set(CPACK_RPM_PACKAGE_SUGGESTS "libnotify")

set(CPACK_GENERATOR "DEB;RPM")
include(CPack)
