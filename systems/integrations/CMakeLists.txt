# systems/integrations — Discord RPC, power/screensaver inhibit chain, MPRIS (P3).
add_library(nuvio_integrations STATIC
    include/nuvio/integrations/DiscordRpc.h src/DiscordRpc.cpp
    include/nuvio/integrations/ScreensaverInhibit.h src/ScreensaverInhibit.cpp
    include/nuvio/integrations/MprisService.h src/MprisService.cpp
    src/IntegrationsSystem.cpp)
target_include_directories(nuvio_integrations PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Qt6 REQUIRED COMPONENTS DBus)
target_link_libraries(nuvio_integrations
    PUBLIC Qt6::Core Qt6::Network Qt6::DBus nuvio::mpv nuvio::playback)
nuvio_common_target_props(nuvio_integrations)
add_library(nuvio::integrations ALIAS nuvio_integrations)

add_executable(nuvio_discord_tests tests/DiscordRpcTests.cpp)
target_link_libraries(nuvio_discord_tests PRIVATE nuvio::integrations)
nuvio_common_target_props(nuvio_discord_tests)
add_test(NAME nuvio_discord COMMAND nuvio_discord_tests)

add_executable(nuvio_screensaver_tests tests/ScreensaverInhibitTests.cpp)
target_link_libraries(nuvio_screensaver_tests PRIVATE nuvio::integrations)
nuvio_common_target_props(nuvio_screensaver_tests)
add_test(NAME nuvio_screensaver COMMAND nuvio_screensaver_tests)
