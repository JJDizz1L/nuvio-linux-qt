# systems/library — catalog domain: network factory, provider clients
# (tmdb/trakt/simkl/stremio) and repositories on top. Online in P1/P3.
add_library(nuvio_library STATIC
    include/nuvio/library/CatalogService.h src/CatalogService.cpp
    include/nuvio/library/MetaService.h    src/MetaService.cpp
    include/nuvio/library/AddonStore.h     src/AddonStore.cpp
    include/nuvio/library/AddonRegistry.h  src/AddonRegistry.cpp
    include/nuvio/library/HomeCatalogSettings.h src/HomeCatalogSettings.cpp
    include/nuvio/library/HomeCatalogSync.h src/HomeCatalogSync.cpp
    include/nuvio/library/HomeShelves.h    src/HomeShelves.cpp
    include/nuvio/library/LibraryStore.h   src/LibraryStore.cpp
    include/nuvio/library/LibrarySyncCodec.h src/LibrarySyncCodec.cpp
    include/nuvio/library/CollectionStore.h src/CollectionStore.cpp
    src/LibrarySystem.cpp)

target_include_directories(nuvio_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_library PUBLIC Qt6::Core Qt6::Network nuvio::settings)
nuvio_common_target_props(nuvio_library)
add_library(nuvio::library ALIAS nuvio_library)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(nuvio_catalog_tests tests/CatalogServiceTests.cpp)
target_link_libraries(nuvio_catalog_tests PRIVATE nuvio::library)
nuvio_common_target_props(nuvio_catalog_tests)
add_test(NAME nuvio_catalog COMMAND nuvio_catalog_tests)
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(nuvio_addons_tests tests/AddonRegistryTests.cpp)
target_link_libraries(nuvio_addons_tests PRIVATE nuvio::library Qt6::Test)
nuvio_common_target_props(nuvio_addons_tests)
add_test(NAME nuvio_addons COMMAND nuvio_addons_tests)

add_executable(nuvio_meta_tests tests/MetaServiceTests.cpp)
target_link_libraries(nuvio_meta_tests PRIVATE nuvio::library)
nuvio_common_target_props(nuvio_meta_tests)
add_test(NAME nuvio_meta COMMAND nuvio_meta_tests)

add_executable(nuvio_home_tests tests/HomeShelvesTests.cpp)
target_link_libraries(nuvio_home_tests PRIVATE nuvio::library)
nuvio_common_target_props(nuvio_home_tests)
add_test(NAME nuvio_home COMMAND nuvio_home_tests)

add_executable(nuvio_library_store_tests tests/LibraryStoreTests.cpp)
target_link_libraries(nuvio_library_store_tests PRIVATE nuvio::library)
nuvio_common_target_props(nuvio_library_store_tests)
add_test(NAME nuvio_library_store COMMAND nuvio_library_store_tests)
