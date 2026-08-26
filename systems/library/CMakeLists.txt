# systems/library — catalog domain: network factory, provider clients
# (tmdb/trakt/simkl/stremio) and repositories on top. Online in P1/P3.
add_library(nuvio_library STATIC src/LibrarySystem.cpp)
target_link_libraries(nuvio_library PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_library)
add_library(nuvio::library ALIAS nuvio_library)
