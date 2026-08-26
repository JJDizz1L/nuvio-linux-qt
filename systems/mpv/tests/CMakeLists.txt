# Policy/discovery/keymap suites — deliberately headless-pure: they run on
# any CI box without a display server or GPU (plan §7.2 testability rule).
find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

add_executable(nuvio_mpv_tests MpvPolicyTests.cpp)
target_link_libraries(nuvio_mpv_tests PRIVATE Qt6::Test nuvio::mpv)
nuvio_common_target_props(nuvio_mpv_tests)
add_test(NAME nuvio_mpv_policy COMMAND nuvio_mpv_tests)
