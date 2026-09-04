# systems/notifications — episode-release alerts (next Appendix A item):
# pure date kernel + request builder, persisted followed shows, manager
# with Cinemeta refresh and a notify-send fire path (fork desktop parity
# would be a stub; Linux ships the real bus instead - see header).
add_library(nuvio_notifications STATIC
    include/nuvio/notifications/ReleaseDate.h src/ReleaseDate.cpp
    include/nuvio/notifications/ReleaseNotifications.h src/ReleaseNotifications.cpp)

target_include_directories(nuvio_notifications PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_notifications PUBLIC Qt6::Core Qt6::Network nuvio::settings nuvio::platform nuvio::library)
nuvio_common_target_props(nuvio_notifications)
add_library(nuvio::notifications ALIAS nuvio_notifications)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_notifications_tests tests/ReleaseNotificationsTests.cpp)
    target_link_libraries(nuvio_notifications_tests PRIVATE nuvio::notifications Qt6::Network)
    nuvio_common_target_props(nuvio_notifications_tests)
    add_test(NAME nuvio_notifications COMMAND nuvio_notifications_tests)
endif()
