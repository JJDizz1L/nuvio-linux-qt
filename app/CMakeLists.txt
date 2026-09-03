qt_add_executable(nuvio-linux-qt
    main.cpp
    bootstrap/LogCategories.h      bootstrap/LogCategories.cpp
    bootstrap/ModuleRegistry.h     bootstrap/ModuleRegistry.cpp
    bootstrap/SmokeRunner.h        bootstrap/SmokeRunner.cpp)

set(NUVIO_APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(bootstrap/Version.h.in "${NUVIO_APP_GENERATED_DIR}/Version.h" @ONLY)

target_include_directories(nuvio-linux-qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NUVIO_APP_GENERATED_DIR})
target_link_libraries(nuvio-linux-qt PRIVATE
    Qt6::Quick Qt6::QuickControls2
    nuvio::ui          # registration + qml resources + mpv public types
    nuvio::mpv nuvio::platform
    # Reserved domains: linked so the dependency law is exercised from day
    # one and --dump-modules reflects built reality (identity TUs only).
    nuvio::playback nuvio::library nuvio::authsync nuvio::profiles
    nuvio::tracking  nuvio::integrations nuvio::settings nuvio::trailer
    nuvio::watching)   # watch-state foundation (resume/watched recorder)

nuvio_common_target_props(nuvio-linux-qt)

# Bundle the vendored TorrServer (GOAMD64=v1, git-lfs) into the build tree
# so the p2p resolver's build-tree candidates are live without env
# overrides. Skips gracefully when the checkout has no smudged LFS copy
# (pointer files fail the ELF-magic probe). CMake 3.16-safe:
# file(READ ... HEX), no file(SIZE).
set(NUVIO_TORRSERVER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../assets/torrserver/linux-amd64/TorrServer")
set(NUVIO_TORRSERVER_BUNDLED "")
if(EXISTS "${NUVIO_TORRSERVER_SRC}")
    file(READ "${NUVIO_TORRSERVER_SRC}" NUVIO_TORRSERVER_MAGIC LIMIT 4 HEX)
    if(NUVIO_TORRSERVER_MAGIC STREQUAL "7f454c46") # ELF \x7fELF
        set(NUVIO_TORRSERVER_DIR "${CMAKE_BINARY_DIR}/native/torrserver/linux-amd64")
        add_custom_target(nuvio_torrserver_bundle ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${NUVIO_TORRSERVER_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${NUVIO_TORRSERVER_SRC}"
                    "${NUVIO_TORRSERVER_DIR}/TorrServer"
            COMMENT "Bundling TorrServer binary into build tree"
            VERBATIM)
        # copy_if_different preserves the source's 0755 bits.
        set(NUVIO_TORRSERVER_BUNDLED "${NUVIO_TORRSERVER_DIR}/TorrServer")
    endif()
endif()
if(NUVIO_INSTALL AND NUVIO_TORRSERVER_BUNDLED)
    install(FILES "${NUVIO_TORRSERVER_BUNDLED}"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE)
endif()

# Console stays visible during the exploration phase on purpose (logs);
# packaging flips WIN32 subsystem later via option.
if(NUVIO_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS nuvio-linux-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
