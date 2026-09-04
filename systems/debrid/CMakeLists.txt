# systems/debrid — premium download providers (D1 live): settings,
# provider wire clients, device auth. Template engine + resolver (D2),
# cloud library (D3).
add_library(nuvio_debrid STATIC
    include/nuvio/debrid/DebridTypes.h
    include/nuvio/debrid/DebridSettings.h  src/DebridSettings.cpp
    include/nuvio/debrid/DebridApi.h       src/DebridApi.cpp
    include/nuvio/debrid/DebridAuth.h      src/DebridAuth.cpp
    include/nuvio/debrid/DebridResolver.h   src/DebridResolver.cpp
    include/nuvio/debrid/DebridStreamValues.h src/DebridStreamValues.cpp
    include/nuvio/debrid/StreamTemplateEngine.h src/StreamTemplateEngine.cpp
    include/nuvio/debrid/DebridFileSelect.h src/DebridFileSelect.cpp
    include/nuvio/debrid/CloudLibrary.h     src/CloudLibrary.cpp)

target_include_directories(nuvio_debrid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_debrid PUBLIC Qt6::Core Qt6::Network nuvio::settings)
nuvio_common_target_props(nuvio_debrid)
add_library(nuvio::debrid ALIAS nuvio_debrid)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_debrid_tests tests/DebridTests.cpp)
    target_link_libraries(nuvio_debrid_tests PRIVATE nuvio::debrid)
    nuvio_common_target_props(nuvio_debrid_tests)
    add_test(NAME nuvio_debrid COMMAND nuvio_debrid_tests)
    add_executable(nuvio_template_tests tests/TemplateEngineTests.cpp)
    target_link_libraries(nuvio_template_tests PRIVATE nuvio::debrid)
    nuvio_common_target_props(nuvio_template_tests)
    add_test(NAME nuvio_template COMMAND nuvio_template_tests)
    add_executable(nuvio_fileselect_tests tests/FileSelectTests.cpp)
    target_link_libraries(nuvio_fileselect_tests PRIVATE nuvio::debrid)
    nuvio_common_target_props(nuvio_fileselect_tests)
    add_test(NAME nuvio_fileselect COMMAND nuvio_fileselect_tests)
    add_executable(nuvio_resolver_tests tests/ResolverTests.cpp)
    target_link_libraries(nuvio_resolver_tests PRIVATE nuvio::debrid Qt6::Network)
    nuvio_common_target_props(nuvio_resolver_tests)
    add_test(NAME nuvio_resolver COMMAND nuvio_resolver_tests)
    add_executable(nuvio_cloud_tests tests/CloudLibraryTests.cpp)
    target_link_libraries(nuvio_cloud_tests PRIVATE nuvio::debrid)
    nuvio_common_target_props(nuvio_cloud_tests)
    add_test(NAME nuvio_cloud COMMAND nuvio_cloud_tests)
endif()
