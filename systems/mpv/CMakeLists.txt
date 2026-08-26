# systems/mpv — THE video system. Sole owner of anything libmpv (plan §7):
# every mpv_* symbol referenced in the entire process lives under this dir.
add_library(nuvio_mpv STATIC
    include/nuvio/mpv/MpvTypes.h
    include/nuvio/mpv/HwdecPolicy.h    src/HwdecPolicy.cpp
    include/nuvio/mpv/MpvUserConfig.h  src/MpvUserConfig.cpp
    include/nuvio/mpv/MpvKeyMap.h      src/MpvKeyMap.cpp
    include/nuvio/mpv/MpvController.h  src/MpvController.cpp
    include/nuvio/mpv/MpvRenderer.h    src/MpvRenderer.cpp
    include/nuvio/mpv/MpvQuickItem.h   src/MpvQuickItem.cpp
)
target_include_directories(nuvio_mpv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Qt5-compat era note: Core signals cross threads; Quick used by the item/renderer.
target_link_libraries(nuvio_mpv
    PUBLIC  Qt6::Core
    PRIVATE Qt6::Quick ${NUVIO_MPVPKG})

nuvio_common_target_props(nuvio_mpv)
add_library(nuvio::mpv ALIAS nuvio_mpv)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
