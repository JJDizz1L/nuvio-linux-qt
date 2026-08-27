# systems/settings — profile-scoped stores (byte-compatible readers/writers
# for the DesktopStorage file formats used by the Compose line). P1.
add_library(nuvio_settings STATIC src/SettingsSystem.cpp src/PropertiesStore.cpp)
target_include_directories(nuvio_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_settings PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_settings)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
add_library(nuvio::settings ALIAS nuvio_settings)
