# systems/updater — in-app updater (Appendix A item): version compare,
# Linux asset selection, GitHub release parsing, and the AppUpdater
# controller (check/download/xdg-open-install). Online in this phase.
add_library(nuvio_updater STATIC
    include/nuvio/updater/UpdateVersion.h src/UpdateVersion.cpp
    include/nuvio/updater/UpdateAssets.h src/UpdateAssets.cpp
    include/nuvio/updater/AppUpdate.h src/AppUpdate.cpp
    include/nuvio/updater/AppUpdater.h src/AppUpdater.cpp)

target_include_directories(nuvio_updater PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_updater PUBLIC Qt6::Core Qt6::Network nuvio::settings)
nuvio_common_target_props(nuvio_updater)
add_library(nuvio::updater ALIAS nuvio_updater)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_updater_tests tests/UpdaterTests.cpp)
    target_link_libraries(nuvio_updater_tests PRIVATE nuvio::updater Qt6::Network)
    nuvio_common_target_props(nuvio_updater_tests)
    add_test(NAME nuvio_updater COMMAND nuvio_updater_tests)
endif()
