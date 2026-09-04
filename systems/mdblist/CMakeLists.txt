# systems/mdblist — MDBList ratings (next Appendix A item): verbatim
# mdblist_* keys, profile-scoped, enabled&&key gate, per-provider POST
# ratings with cache, display-ordered formatted rows for MetaPage.
add_library(nuvio_mdblist STATIC
    include/nuvio/mdblist/MdbListSettings.h src/MdbListSettings.cpp
    include/nuvio/mdblist/MdbListService.h src/MdbListService.cpp)

target_include_directories(nuvio_mdblist PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_mdblist PUBLIC Qt6::Core Qt6::Network nuvio::settings nuvio::platform)
nuvio_common_target_props(nuvio_mdblist)
add_library(nuvio::mdblist ALIAS nuvio_mdblist)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_mdblist_tests tests/MdbListTests.cpp)
    target_link_libraries(nuvio_mdblist_tests PRIVATE nuvio::mdblist Qt6::Network)
    nuvio_common_target_props(nuvio_mdblist_tests)
    add_test(NAME nuvio_mdblist COMMAND nuvio_mdblist_tests)
endif()
