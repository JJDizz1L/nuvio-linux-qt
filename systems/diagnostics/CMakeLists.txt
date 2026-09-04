# systems/diagnostics — crash reporting (Appendix A item): Sentry
# metadata/sanitizer/envelope kernel, shared opt-in store, and the
# reporting client (terminate + fatal-signal markers, envelope POST).
# Online now; the DSN rides NUVIO_SENTRY_DSN (empty = unsupported).
add_library(nuvio_diagnostics STATIC
    include/nuvio/diagnostics/SentryMetadata.h src/SentryMetadata.cpp
    include/nuvio/diagnostics/SentrySanitizer.h src/SentrySanitizer.cpp
    include/nuvio/diagnostics/SentryEnvelope.h src/SentryEnvelope.cpp
    include/nuvio/diagnostics/SentrySettings.h src/SentrySettings.cpp
    include/nuvio/diagnostics/SentryClient.h src/SentryClient.cpp)

target_include_directories(nuvio_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_diagnostics PUBLIC Qt6::Core Qt6::Network nuvio::settings)
nuvio_common_target_props(nuvio_diagnostics)
add_library(nuvio::diagnostics ALIAS nuvio_diagnostics)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_diagnostics_tests tests/DiagnosticsTests.cpp)
    target_link_libraries(nuvio_diagnostics_tests PRIVATE nuvio::diagnostics Qt6::Network)
    nuvio_common_target_props(nuvio_diagnostics_tests)
    add_test(NAME nuvio_diagnostics COMMAND nuvio_diagnostics_tests)
endif()
