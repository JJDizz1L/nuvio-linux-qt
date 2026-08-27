# systems/trailer — YouTube trailer extraction kernel + innertube client.
add_library(nuvio_trailer STATIC
    include/nuvio/trailer/TrailerKernel.h src/TrailerKernel.cpp)

target_include_directories(nuvio_trailer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_trailer PUBLIC Qt6::Core Qt6::Network)
nuvio_common_target_props(nuvio_trailer)
add_library(nuvio::trailer ALIAS nuvio_trailer)

add_executable(nuvio_trailer_tests tests/TrailerKernelTests.cpp)
target_link_libraries(nuvio_trailer_tests PRIVATE nuvio::trailer)
nuvio_common_target_props(nuvio_trailer_tests)
add_test(NAME nuvio_trailer COMMAND nuvio_trailer_tests)
