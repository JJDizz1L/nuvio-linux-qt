# ui — visual layer; depends downward only (viewmodels/systems, never upward).
add_library(nuvio_ui STATIC
    include/nuvio/ui/UiBootstrap.h             src/UiBootstrap.cpp
    include/nuvio/ui/PlaceholderImageProvider.h src/PlaceholderImageProvider.cpp)

target_include_directories(nuvio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

qt_add_resources(nuvio_ui nuvio_qml
    PREFIX "/nuvio"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        qml/MainShell.qml
        qml/theme/qmldir
        qml/theme/Theme.qml
        qml/pages/VideoPage.qml
        qml/components/TransportBar.qml)

# The UI compiles against the mpv system's public headers and links it — the
# ONLY sanctioned systems dependency from this layer (plus future viewmodels).
target_link_libraries(nuvio_ui
    PUBLIC  Qt6::Quick Qt6::QuickControls2
            nuvio::mpv nuvio::platform)

nuvio_common_target_props(nuvio_ui)
add_library(nuvio::ui ALIAS nuvio_ui)
