# systems/integrations — Discord RPC, power/screensaver inhibit chain, MPRIS (P3).
add_library(nuvio_integrations STATIC src/IntegrationsSystem.cpp)
target_link_libraries(nuvio_integrations PUBLIC Qt6::Core)
nuvio_common_target_props(nuvio_integrations)
add_library(nuvio::integrations ALIAS nuvio_integrations)
