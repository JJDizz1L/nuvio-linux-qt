# systems/settings — profile-scoped stores (byte-compatible readers/writers
# for the DesktopStorage file formats used by the Compose line). P1.
add_library(nuvio_settings STATIC
    include/nuvio/settings/AppSettings.h       src/AppSettings.cpp
    include/nuvio/settings/PropertiesStore.h   src/PropertiesStore.cpp
    include/nuvio/settings/SyncIdentity.h      src/SyncIdentity.cpp
    include/nuvio/settings/SyncPreferenceJson.h src/SyncPreferenceJson.cpp
    src/SettingsSystem.cpp)
target_include_directories(nuvio_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_settings PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_settings)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
add_library(nuvio::settings ALIAS nuvio_settings)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(nuvio_appsettings_tests tests/AppSettingsTests.cpp)
target_link_libraries(nuvio_appsettings_tests PRIVATE nuvio_settings Qt6::Core)
nuvio_common_target_props(nuvio_appsettings_tests)
add_test(NAME nuvio_appsettings COMMAND nuvio_appsettings_tests)
add_executable(nuvio_sync_identity_tests tests/SyncIdentityTests.cpp)
target_link_libraries(nuvio_sync_identity_tests PRIVATE nuvio_settings Qt6::Core)
nuvio_common_target_props(nuvio_sync_identity_tests)
add_test(NAME nuvio_sync_identity COMMAND nuvio_sync_identity_tests)
add_executable(nuvio_sync_prefs_tests tests/SyncPreferenceJsonTests.cpp)
target_link_libraries(nuvio_sync_prefs_tests PRIVATE nuvio_settings Qt6::Core)
nuvio_common_target_props(nuvio_sync_prefs_tests)
add_test(NAME nuvio_sync_prefs COMMAND nuvio_sync_prefs_tests)
