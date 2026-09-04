# systems/tmdb — TMDB settings + id-resolution service (next Appendix
# A item): verbatim tmdb_* keys, profile-scoped, enabled&&key gate;
# imdb<->tmdb mapping with caches. The 2255-line metadata enrichment
# engine stays deferred per P6 (module switches persist + sync as
# honest inert flags until it lands).
add_library(nuvio_tmdb STATIC
    include/nuvio/tmdb/TmdbSettings.h src/TmdbSettings.cpp
    include/nuvio/tmdb/TmdbService.h src/TmdbService.cpp)

target_include_directories(nuvio_tmdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nuvio_tmdb PUBLIC Qt6::Core Qt6::Network nuvio::settings nuvio::platform)
nuvio_common_target_props(nuvio_tmdb)
add_library(nuvio::tmdb ALIAS nuvio_tmdb)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_executable(nuvio_tmdb_tests tests/TmdbTests.cpp)
    target_link_libraries(nuvio_tmdb_tests PRIVATE nuvio::tmdb Qt6::Network)
    nuvio_common_target_props(nuvio_tmdb_tests)
    add_test(NAME nuvio_tmdb COMMAND nuvio_tmdb_tests)
endif()
