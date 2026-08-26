# systems/settings — profile-scoped stores (byte-compatible readers/writers
# for the DesktopStorage file formats used by the Compose line). P1.
add_library(nuvio_settings STATIC src/SettingsSystem.cpp)
target_link_libraries(nuvio_settings PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_settings)
add_library(nuvio::settings ALIAS nuvio_settings)
