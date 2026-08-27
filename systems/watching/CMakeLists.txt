# systems/watching — library watch-state foundation (queue item #2).
# Compose-parity resume (`watch_progress.properties`) + watched (`watched.properties`)
# persistence, pure selection policy, and a QML-facing player recorder.
add_library(nuvio_watching STATIC
    include/nuvio/watching/WatchProgress.h
    src/WatchProgress.cpp
    include/nuvio/watching/WatchCodec.h
    src/WatchCodec.cpp
    include/nuvio/watching/ContinueWatchingPrefs.h
    src/ContinueWatchingPrefs.cpp
    include/nuvio/watching/WatchingStore.h
    src/WatchingStore.cpp
    include/nuvio/watching/ProgressSyncCodec.h
    src/ProgressSyncCodec.cpp
    include/nuvio/watching/WatchRecorder.h
    src/WatchRecorder.cpp)

add_executable(nuvio_progress_codec_tests tests/ProgressSyncCodecTests.cpp)
target_link_libraries(nuvio_progress_codec_tests PRIVATE nuvio::watching Qt6::Core)
nuvio_common_target_props(nuvio_progress_codec_tests)
add_test(NAME nuvio_progress_codec COMMAND nuvio_progress_codec_tests)
target_include_directories(nuvio_watching PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_watching PUBLIC Qt6::Core nuvio::settings)
nuvio_common_target_props(nuvio_watching)
add_library(nuvio::watching ALIAS nuvio_watching)

add_executable(nuvio_watching_tests tests/WatchingTests.cpp)
target_link_libraries(nuvio_watching_tests PRIVATE nuvio::watching Qt6::Core)
nuvio_common_target_props(nuvio_watching_tests)
add_test(NAME nuvio_watching_watchstate COMMAND nuvio_watching_tests)

add_executable(nuvio_cw_prefs_tests tests/ContinueWatchingPrefsTests.cpp)
target_link_libraries(nuvio_cw_prefs_tests PRIVATE nuvio::watching Qt6::Core)
nuvio_common_target_props(nuvio_cw_prefs_tests)
add_test(NAME nuvio_cw_prefs COMMAND nuvio_cw_prefs_tests)
