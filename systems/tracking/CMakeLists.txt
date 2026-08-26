# systems/tracking — Trakt/Simkl registration, scrobble coordination (P3/P4).
add_library(nuvio_tracking STATIC src/TrackingSystem.cpp)
target_link_libraries(nuvio_tracking PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_tracking)
add_library(nuvio::tracking ALIAS nuvio_tracking)
