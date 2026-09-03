# systems/profiles — user profiles: payload codec, salted PIN cache,
# manager (local + server CRUD, PIN verify, switching). Comes online in P7.
add_library(nuvio_profiles STATIC
    include/nuvio/profiles/ProfileStore.h  src/ProfileStore.cpp
    include/nuvio/profiles/ProfileManager.h src/ProfileManager.cpp)

target_include_directories(nuvio_profiles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_profiles
    PUBLIC Qt6::Core Qt6::Network nuvio::settings nuvio::authsync)
nuvio_common_target_props(nuvio_profiles)
add_library(nuvio::profiles ALIAS nuvio_profiles)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
    add_executable(nuvio_profiles_tests tests/ProfilesTests.cpp)
    target_link_libraries(nuvio_profiles_tests PRIVATE nuvio::profiles Qt6::Network)
    nuvio_common_target_props(nuvio_profiles_tests)
    add_test(NAME nuvio_profiles COMMAND nuvio_profiles_tests)
    add_executable(nuvio_profile_manager_tests tests/ProfileManagerTests.cpp)
    target_link_libraries(nuvio_profile_manager_tests PRIVATE nuvio::profiles Qt6::Network)
    nuvio_common_target_props(nuvio_profile_manager_tests)
    add_test(NAME nuvio_profile_manager COMMAND nuvio_profile_manager_tests)
endif()
