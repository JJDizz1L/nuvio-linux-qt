# systems/downloads — offline download manager (DL1 live): queue ops,
# persisted rows, QNAM transfers with Range resume. Trailer/download UI
# (DL2), next-episode offline preference wiring (DL2).
add_library(nuvio_downloads STATIC
    include/nuvio/downloads/DownloadManager.h src/DownloadManager.cpp)

target_include_directories(nuvio_downloads PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_downloads PUBLIC Qt6::Core Qt6::Network nuvio::settings nuvio::platform)
nuvio_common_target_props(nuvio_downloads)
add_library(nuvio::downloads ALIAS nuvio_downloads)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_downloads_tests tests/DownloadManagerTests.cpp)
    target_link_libraries(nuvio_downloads_tests PRIVATE nuvio::downloads Qt6::Network)
    nuvio_common_target_props(nuvio_downloads_tests)
    add_test(NAME nuvio_downloads COMMAND nuvio_downloads_tests)
endif()
