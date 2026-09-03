# systems/playback — orchestration ABOVE the mpv system: session assembly,
# trailer resolution, P2P engine management. Music comes online in P2/P3
# (plan §8); the target exists now so layering violations fail at link time.
add_library(nuvio_playback STATIC
    include/nuvio/playback/StreamResolver.h src/StreamResolver.cpp
    include/nuvio/playback/PlaybackSession.h src/PlaybackSession.cpp
    include/nuvio/playback/NextEpisodeRules.h src/NextEpisodeRules.cpp
    include/nuvio/playback/NextEpisodeHelper.h src/NextEpisodeHelper.cpp
    include/nuvio/playback/StreamLinkCache.h src/StreamLinkCache.cpp
    include/nuvio/playback/ParentalGuide.h src/ParentalGuide.cpp
    include/nuvio/playback/SkipResolver.h src/SkipResolver.cpp
    src/PlaybackSystem.cpp)

target_include_directories(nuvio_playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_playback
    PUBLIC Qt6::Core Qt6::Network nuvio::p2p nuvio::settings)
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

add_executable(nuvio_nextep_tests tests/NextEpisodeRulesTests.cpp)
target_link_libraries(nuvio_nextep_tests PRIVATE nuvio::playback)
nuvio_common_target_props(nuvio_nextep_tests)
add_test(NAME nuvio_nextep COMMAND nuvio_nextep_tests)

add_executable(nuvio_linkcache_tests tests/StreamLinkCacheTests.cpp)
target_link_libraries(nuvio_linkcache_tests PRIVATE nuvio::playback)
nuvio_common_target_props(nuvio_linkcache_tests)
add_test(NAME nuvio_linkcache COMMAND nuvio_linkcache_tests)

add_executable(nuvio_parental_tests tests/ParentalGuideTests.cpp)
target_link_libraries(nuvio_parental_tests PRIVATE nuvio::playback)
nuvio_common_target_props(nuvio_parental_tests)
add_test(NAME nuvio_parental COMMAND nuvio_parental_tests)

add_executable(nuvio_skip_tests tests/SkipResolverTests.cpp)
target_link_libraries(nuvio_skip_tests PRIVATE nuvio::playback)
nuvio_common_target_props(nuvio_skip_tests)
add_test(NAME nuvio_skip COMMAND nuvio_skip_tests)
