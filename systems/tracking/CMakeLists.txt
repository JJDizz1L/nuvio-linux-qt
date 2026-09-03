# systems/tracking — provider registry + scrobble coordination (T1 live).
add_library(nuvio_tracking STATIC
    include/nuvio/tracking/TrackingTypes.h
    include/nuvio/tracking/TrackingRegistry.h src/TrackingRegistry.cpp
    include/nuvio/tracking/ScrobblePump.h     src/ScrobblePump.cpp
    include/nuvio/tracking/TraktAuth.h        src/TraktAuth.cpp
    include/nuvio/tracking/TraktScrobble.h    src/TraktScrobble.cpp
    include/nuvio/tracking/SimklAuth.h        src/SimklAuth.cpp
    include/nuvio/tracking/SimklScrobble.h    src/SimklScrobble.cpp
    src/TrackingSystem.cpp)

target_include_directories(nuvio_tracking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_tracking PUBLIC Qt6::Core Qt6::Network nuvio::settings)
nuvio_common_target_props(nuvio_tracking)
add_library(nuvio::tracking ALIAS nuvio_tracking)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_tracking_tests tests/TrackingTests.cpp)
    target_link_libraries(nuvio_tracking_tests PRIVATE nuvio::tracking)
    nuvio_common_target_props(nuvio_tracking_tests)
    add_test(NAME nuvio_tracking COMMAND nuvio_tracking_tests)
    add_executable(nuvio_trakt_tests tests/TraktTests.cpp)
    target_link_libraries(nuvio_trakt_tests PRIVATE nuvio::tracking)
    nuvio_common_target_props(nuvio_trakt_tests)
    add_test(NAME nuvio_trakt COMMAND nuvio_trakt_tests)
    add_executable(nuvio_simkl_tests tests/SimklTests.cpp)
    target_link_libraries(nuvio_simkl_tests PRIVATE nuvio::tracking)
    nuvio_common_target_props(nuvio_simkl_tests)
    add_test(NAME nuvio_simkl COMMAND nuvio_simkl_tests)
endif()
