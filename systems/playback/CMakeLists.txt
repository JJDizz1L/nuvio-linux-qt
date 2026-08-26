# systems/playback — orchestration ABOVE the mpv system: session assembly,
# trailer resolution, P2P engine management. Music comes online in P2/P3
# (plan §8); the target exists now so layering violations fail at link time.
add_library(nuvio_playback STATIC src/PlaybackSystem.cpp)
target_link_libraries(nuvio_playback PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_playback)
add_library(nuvio::playback ALIAS nuvio_playback)
