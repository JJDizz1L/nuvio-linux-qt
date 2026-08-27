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
    nuvio::playback nuvio::library nuvio::authsync
    nuvio::tracking  nuvio::integrations nuvio::settings nuvio::trailer
    nuvio::watching)   # watch-state foundation (resume/watched recorder)

nuvio_common_target_props(nuvio-linux-qt)
# Console stays visible during the exploration phase on purpose (logs);
# packaging flips WIN32 subsystem later via option.
if(NUVIO_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS nuvio-linux-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
