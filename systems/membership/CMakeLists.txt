# systems/membership — supporter access + overview + community
# (next Appendix A item): tier/entitlement model with a userId-keyed
# cached payload, RPC verification (15-min cadence, 1/2/4s retry),
# overview state, contributors/supporters-wall fetching. Entitlement-
# gated cosmetics (supporter themes, backgrounds, avatars) consult
# hasEntitlement() when they land; nothing gates on it yet.
add_library(nuvio_membership STATIC
    include/nuvio/membership/Membership.h src/Membership.cpp
    include/nuvio/membership/MemberAccess.h src/MemberAccess.cpp
    include/nuvio/membership/MembershipOverview.h src/MembershipOverview.cpp
    include/nuvio/membership/CommunityService.h src/CommunityService.cpp)

target_include_directories(nuvio_membership PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_membership PUBLIC Qt6::Core Qt6::Network nuvio::settings nuvio::platform nuvio::authsync)
nuvio_common_target_props(nuvio_membership)
add_library(nuvio::membership ALIAS nuvio_membership)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_membership_tests tests/MembershipTests.cpp)
    target_link_libraries(nuvio_membership_tests PRIVATE nuvio::membership nuvio::authsync Qt6::Network)
    nuvio_common_target_props(nuvio_membership_tests)
    add_test(NAME nuvio_membership COMMAND nuvio_membership_tests)
endif()
