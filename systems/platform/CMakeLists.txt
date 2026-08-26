# systems/platform — window-state storage, fullscreen policy, display info,
# XDG base-directory resolution. Leaf target: depends on nothing above itself.
add_library(nuvio_platform STATIC
    include/nuvio/platform/XdgPaths.h
    src/XdgPaths.cpp)
target_include_directories(nuvio_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_platform PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_platform)
add_library(nuvio::platform ALIAS nuvio_platform)
