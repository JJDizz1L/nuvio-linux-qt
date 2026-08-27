# systems/authsync — L1 CONTAINMENT ZONE (plan §4/§7): upstream wire parity.
# Server identity constants, sync-blob codec, Supabase auth flows, contract
# tests against the production backend. NOTHING above this layer may edit wire
# behavior. Comes online in P4 *after* its QtTest contract suite exists.
add_library(nuvio_authsync STATIC
    include/nuvio/authsync/AuthConfig.h   src/AuthConfig.cpp
    include/nuvio/authsync/AuthService.h  src/AuthService.cpp
    include/nuvio/authsync/SyncRpcClient.h src/SyncRpcClient.cpp
    include/nuvio/authsync/SyncOrchestrator.h src/SyncOrchestrator.cpp
    src/AuthSyncSystem.cpp)

target_include_directories(nuvio_authsync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# settings dependency: session persistence rides the shared PropertiesStore
# (byte-compat with the Compose profile). Sibling-systems link, sanctioned.
target_link_libraries(nuvio_authsync
    PUBLIC Qt6::Core Qt6::Network nuvio::settings)
nuvio_common_target_props(nuvio_authsync)
add_library(nuvio::authsync ALIAS nuvio_authsync)

find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(nuvio_authconfig_tests tests/AuthConfigTests.cpp)
target_link_libraries(nuvio_authconfig_tests PRIVATE nuvio::authsync)
nuvio_common_target_props(nuvio_authconfig_tests)
add_test(NAME nuvio_authconfig COMMAND nuvio_authconfig_tests)
target_include_directories(nuvio_authconfig_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(nuvio_sync_rpc_tests tests/SyncRpcClientTests.cpp)
target_link_libraries(nuvio_sync_rpc_tests PRIVATE nuvio::authsync Qt6::Network)
nuvio_common_target_props(nuvio_sync_rpc_tests)
add_test(NAME nuvio_sync_rpc COMMAND nuvio_sync_rpc_tests)
add_executable(nuvio_sync_orchestrator_tests tests/SyncOrchestratorTests.cpp)
target_link_libraries(nuvio_sync_orchestrator_tests PRIVATE nuvio::authsync Qt6::Network)
nuvio_common_target_props(nuvio_sync_orchestrator_tests)
add_test(NAME nuvio_sync_orchestrator COMMAND nuvio_sync_orchestrator_tests)
add_executable(nuvio_sync_contract_tests tests/SyncContractTests.cpp)
target_link_libraries(nuvio_sync_contract_tests PRIVATE nuvio::authsync Qt6::Network)
nuvio_common_target_props(nuvio_sync_contract_tests)
# Env-gated: SKIP unless NUVIO_SYNC_CONTRACT=1 (production traffic!).
add_test(NAME nuvio_sync_contract COMMAND nuvio_sync_contract_tests)
set_tests_properties(nuvio_sync_contract PROPERTIES
    ENVIRONMENT "NUVIO_SYNC_CONTRACT=$ENV{NUVIO_SYNC_CONTRACT}")
