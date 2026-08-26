# systems/authsync — L1 CONTAINMENT ZONE (plan §4/§7): upstream wire parity.
# Server identity constants, sync-blob codec, Supabase auth flows, contract
# tests against the production backend. NOTHING above this layer may edit wire
# behavior. Comes online in P4 *after* its QtTest contract suite exists.
add_library(nuvio_authsync STATIC src/AuthSyncSystem.cpp)
target_link_libraries(nuvio_authsync PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_authsync)
add_library(nuvio::authsync ALIAS nuvio_authsync)
