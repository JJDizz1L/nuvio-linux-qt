# systems/playback — orchestration ABOVE the mpv system: session assembly,
# trailer resolution, P2P engine management. Music comes online in P2/P3
# (plan §8); the target exists now so layering violations fail at link time.
add_library(nuvio_playback STATIC
    include/nuvio/playback/StreamResolver.h src/StreamResolver.cpp
    include/nuvio/playback/PlaybackSession.h src/PlaybackSession.cpp
    src/PlaybackSystem.cpp)

target_include_directories(nuvio_playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_playback
    PUBLIC Qt6::Core Qt6::Network nuvio::p2p)
nuvio_common_target_props(nuvio_playback)
add_library(nuvio::playback ALIAS nuvio_playback)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(nuvio_stream_tests tests/StreamResolverTests.cpp)
target_link_libraries(nuvio_stream_tests PRIVATE nuvio::playback)
nuvio_common_target_props(nuvio_stream_tests)
add_test(NAME nuvio_stream COMMAND nuvio_stream_tests)

add_executable(nuvio_playback_session_tests tests/PlaybackSessionTests.cpp)
target_link_libraries(nuvio_playback_session_tests PRIVATE nuvio::playback)
nuvio_common_target_props(nuvio_playback_session_tests)
add_test(NAME nuvio_playback_session COMMAND nuvio_playback_session_tests)
