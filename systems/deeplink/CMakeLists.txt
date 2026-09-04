# systems/deeplink — app deeplinks (Appendix A item): pure nuvio:// +
# stremio:// parse/build kernel plus the routing entry point. Online now.
add_library(nuvio_deeplink STATIC
    include/nuvio/deeplink/DeepLink.h src/DeepLink.cpp
    include/nuvio/deeplink/DeepLinkRouter.h src/DeepLinkRouter.cpp)

target_include_directories(nuvio_deeplink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_deeplink PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_deeplink)
add_library(nuvio::deeplink ALIAS nuvio_deeplink)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core)
    add_executable(nuvio_deeplink_tests tests/DeepLinkTests.cpp)
    target_link_libraries(nuvio_deeplink_tests PRIVATE nuvio::deeplink)
    nuvio_common_target_props(nuvio_deeplink_tests)
    add_test(NAME nuvio_deeplink COMMAND nuvio_deeplink_tests)
endif()
