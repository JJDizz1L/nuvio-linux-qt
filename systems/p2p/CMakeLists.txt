# systems/p2p — TorrServer orchestration: wire-contract protocol layer,
# process lifecycle, stream-start state machine (plan §8 P3 first slice).
add_library(nuvio_p2p STATIC
    include/nuvio/p2p/TorrServerProtocol.h src/TorrServerProtocol.cpp
    include/nuvio/p2p/TorrServerProcess.h  src/TorrServerProcess.cpp
    include/nuvio/p2p/P2pEngine.h          src/P2pEngine.cpp)

target_include_directories(nuvio_p2p PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_p2p
    PUBLIC Qt6::Core Qt6::Network nuvio::platform)
nuvio_common_target_props(nuvio_p2p)
add_library(nuvio::p2p ALIAS nuvio_p2p)

add_executable(nuvio_p2p_protocol_tests tests/TorrServerProtocolTests.cpp)
target_link_libraries(nuvio_p2p_protocol_tests PRIVATE nuvio::p2p)
nuvio_common_target_props(nuvio_p2p_protocol_tests)
add_test(NAME nuvio_p2p_protocol COMMAND nuvio_p2p_protocol_tests)
