# Nuvio Linux (Qt line) — top-level build definition.
#
# Portability charter (see ../../nuvio-linux-qt.md §7, AGENTS.md):
#  * CMake >= 3.16, Qt >= 6.2, mpv client API >= 2.x — chosen to span
#    Debian/Ubuntu LTS (Qt 6.2/6.4 era) through rolling releases.
#  * Baseline ISA only: NO -march=native / arch-tuning anywhere (enforced in
#    NuvioCompilerSettings.cmake). Distro compilers' own defaults are the ISA.
#  * Public APIs only. No Qt private headers, no qt_add_qml_module /
#    qt_standard_project_setup (6.3+-isms); plain AUTOMOC/AUTOUIC/qt_add_resources.
#
# Layout: one directory == one CMake target == one review/test scope.
# Link-time dependency law: systems/* may never link/include ui/*.
cmake_minimum_required(VERSION 3.16)

project(NuvioLinuxQt
        VERSION 0.1.20.0
        DESCRIPTION "Nuvio Linux media player (Qt Quick exploration line)"
        LANGUAGES CXX)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(NuvioCompilerSettings)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)      # resources added explicitly per-target
set(CMAKE_AUTOUIC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(NUVIO_QT_REQUIRED_COMPONENTS Core Gui Quick QuickControls2 Network)
find_package(Qt6 6.2 REQUIRED COMPONENTS ${NUVIO_QT_REQUIRED_COMPONENTS})
# QtTest is required only by test targets and resolved within those lists.

# ---- mpv -------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MPV QUIET IMPORTED_TARGET mpv)
endif()
if(NOT MPV_FOUND)
    find_path(MPV_INCLUDE_DIR NAMES mpv/client.h mpv/render.h)
    find_library(MPV_LIBRARY NAMES mpv)
    if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
        message(FATAL_ERROR "libmpv development files not found (need mpv/client.h + mpv/render.h). Install e.g. Debian/Ubuntu: libmpv-dev, Fedora: mpv-devel, Arch: mpv.")
    endif()
    add_library(nuvio_mpv_external UNKNOWN IMPORTED)
    set_target_properties(nuvio_mpv_external PROPERTIES
        IMPORTED_LOCATION "${MPV_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}")
    set(NUVIO_MPVPKG nuvio_mpv_external)
else()
    set(NUVIO_MPVPKG PkgConfig::MPV)
endif()
message(STATUS "Nuvio Qt line: mpv linkage -> ${NUVIO_MPVPKG}")

include(CTest)

# Order encodes layering; leaves (mpv, library, ...) may not reference ui/.
add_subdirectory(systems/mpv)
add_subdirectory(systems/p2p)
add_subdirectory(systems/playback)
add_subdirectory(systems/settings)      # first: authsync stores sessions in it
add_subdirectory(systems/platform)
add_subdirectory(systems/trailer)
add_subdirectory(systems/library)
add_subdirectory(systems/watching)        # watch-state foundation (P1)
add_subdirectory(systems/authsync)
add_subdirectory(systems/profiles)        # user profiles (P7, over authsync)
add_subdirectory(systems/debrid)          # premium providers (A2)
add_subdirectory(systems/downloads)       # offline downloads (A3)
add_subdirectory(systems/tracking)
add_subdirectory(systems/integrations)
add_subdirectory(ui)
add_subdirectory(app)
