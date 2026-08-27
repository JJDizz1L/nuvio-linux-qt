# ui — visual layer; depends downward only (viewmodels/systems, never upward).
add_library(nuvio_ui STATIC
    include/nuvio/ui/UiBootstrap.h              src/UiBootstrap.cpp
    include/nuvio/ui/PlaceholderImageProvider.h src/PlaceholderImageProvider.cpp
    include/nuvio/ui/NavigationModel.h          src/NavigationModel.cpp
    include/nuvio/ui/PosterProvider.h           src/PosterProvider.cpp)

target_include_directories(nuvio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

qt_add_resources(nuvio_ui nuvio_qml
    PREFIX "/nuvio"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        qml/MainShell.qml
        qml/theme/qmldir
        qml/theme/Theme.qml
        qml/pages/VideoPage.qml
        qml/pages/HomePage.qml
        qml/pages/HomePage.qml
        qml/pages/WelcomePage.qml
        qml/pages/LibraryPage.qml
        qml/components/TransportBar.qml)

# The UI compiles against the mpv system's public headers and links it — the
# ONLY sanctioned systems dependency from this layer (plus future viewmodels).
target_link_libraries(nuvio_ui
    PUBLIC  Qt6::Quick Qt6::QuickControls2 Qt6::Network
            nuvio::mpv nuvio::platform)

nuvio_common_target_props(nuvio_ui)
add_library(nuvio::ui ALIAS nuvio_ui)

# Navigation contract is GUI-free: run it headless in CI.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(nuvio_navigation_tests tests/NavigationModelTests.cpp)
target_link_libraries(nuvio_navigation_tests PRIVATE nuvio_ui Qt6::Core)
nuvio_common_target_props(nuvio_navigation_tests)
add_test(NAME nuvio_navigation COMMAND nuvio_navigation_tests)
