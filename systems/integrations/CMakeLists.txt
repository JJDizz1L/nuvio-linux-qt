# systems/integrations — Discord RPC, power/screensaver inhibit chain, MPRIS (P3).
add_library(nuvio_integrations STATIC
    include/nuvio/integrations/DiscordRpc.h src/DiscordRpc.cpp
    src/IntegrationsSystem.cpp)
target_include_directories(nuvio_integrations PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_integrations PUBLIC Qt6::Core Qt6::Network)
nuvio_common_target_props(nuvio_integrations)
add_library(nuvio::integrations ALIAS nuvio_integrations)

add_executable(nuvio_discord_tests tests/DiscordRpcTests.cpp)
target_link_libraries(nuvio_discord_tests PRIVATE nuvio::integrations)
nuvio_common_target_props(nuvio_discord_tests)
add_test(NAME nuvio_discord COMMAND nuvio_discord_tests)
