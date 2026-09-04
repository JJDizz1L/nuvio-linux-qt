# systems/plugins — Stremio-style plugin runtime (next Appendix A
# item): vendored QuickJS + C++ engine wrapper, verbatim JS polyfill,
# host bridges (fetch/url/dom/crypto), repository + streams tier + UI.
add_library(nuvio_plugins STATIC
    include/nuvio/plugins/JsEngine.h src/JsEngine.cpp
    include/nuvio/plugins/PluginCrypto.h src/PluginCrypto.cpp
    include/nuvio/plugins/DomBridge.h src/DomBridge.cpp
    include/nuvio/plugins/PluginHost.h src/PluginHost.cpp
    include/nuvio/plugins/PluginRuntime.h src/PluginRuntime.cpp
    include/nuvio/plugins/PluginRepository.h src/PluginRepository.cpp)

target_include_directories(nuvio_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_plugins PUBLIC Qt6::Core Qt6::Network quickjs::quickjs tiny_aes::tiny_aes gumbo::gumbo nuvio::settings nuvio::platform nuvio::authsync nuvio::tmdb)
# Asymmetric sign/verify rides OpenSSL when present, honest errors
# otherwise (never a silent gap; see PluginCrypto.h).
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(nuvio_plugins PUBLIC OpenSSL::Crypto)
    target_compile_definitions(nuvio_plugins PUBLIC HAVE_OPENSSL)
    message(STATUS "Nuvio plugins: OpenSSL crypto enabled")
else()
    message(STATUS "Nuvio plugins: OpenSSL missing - RSA/ECDSA bridge errors honestly")
endif()
nuvio_common_target_props(nuvio_plugins)
add_library(nuvio::plugins ALIAS nuvio_plugins)

# Verbatim JS polyfill (extracted from the fork's JsBindings; see
# resources/README.md). Embedded so tests + app share one copy.
qt_add_resources(nuvio_plugins plugin_js
    PREFIX "/nuvio/plugins"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES resources/polyfill.js)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_plugins_tests tests/PluginsTests.cpp)
    target_link_libraries(nuvio_plugins_tests PRIVATE nuvio::plugins Qt6::Network)
    nuvio_common_target_props(nuvio_plugins_tests)
    add_test(NAME nuvio_plugins COMMAND nuvio_plugins_tests)
    add_executable(nuvio_plugin_crypto_tests tests/PluginCryptoTests.cpp)
    target_link_libraries(nuvio_plugin_crypto_tests PRIVATE nuvio::plugins Qt6::Network)
    nuvio_common_target_props(nuvio_plugin_crypto_tests)
    add_test(NAME nuvio_plugin_crypto COMMAND nuvio_plugin_crypto_tests)
    add_executable(nuvio_dom_tests tests/DomBridgeTests.cpp)
    target_link_libraries(nuvio_dom_tests PRIVATE nuvio::plugins Qt6::Network)
    nuvio_common_target_props(nuvio_dom_tests)
    add_test(NAME nuvio_dom COMMAND nuvio_dom_tests)
    add_executable(nuvio_plugin_host_tests tests/PluginHostTests.cpp)
    target_link_libraries(nuvio_plugin_host_tests PRIVATE nuvio::plugins Qt6::Network)
    nuvio_common_target_props(nuvio_plugin_host_tests)
    add_test(NAME nuvio_plugin_host COMMAND nuvio_plugin_host_tests)
    add_executable(nuvio_plugin_runtime_tests tests/PluginRuntimeTests.cpp)
    target_link_libraries(nuvio_plugin_runtime_tests PRIVATE nuvio::plugins Qt6::Network)
    nuvio_common_target_props(nuvio_plugin_runtime_tests)
    add_test(NAME nuvio_plugin_runtime COMMAND nuvio_plugin_runtime_tests)
    add_executable(nuvio_plugin_repo_tests tests/PluginRepositoryTests.cpp)
    target_link_libraries(nuvio_plugin_repo_tests PRIVATE nuvio::plugins nuvio::authsync nuvio::tmdb Qt6::Network)
    nuvio_common_target_props(nuvio_plugin_repo_tests)
    add_test(NAME nuvio_plugin_repo COMMAND nuvio_plugin_repo_tests)
endif()
