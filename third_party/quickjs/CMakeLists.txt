# third_party/quickjs — vendored Bellard QuickJS 2024-01-13 (MIT, see
# LICENSE/VERSION). Same engine lineage as the fork's libquickjs
# (quickjs-kt JNI bindings); pinned here so the plugin runtime never
# drifts with system packages (quickjs-ng is a different fork).
# Deliberately outside nuvio_common_target_props: third-party C with
# its own warning profile (-w keeps the build log ours).
add_library(quickjs STATIC
    quickjs.c
    cutils.c
    libregexp.c
    libunicode.c
    libbf.c)

target_include_directories(quickjs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(quickjs PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
target_compile_options(quickjs PRIVATE -w -O2)
# The upstream Makefile passes -DCONFIG_VERSION; replicate it here.
target_compile_definitions(quickjs PRIVATE CONFIG_VERSION="2024-01-13")
target_link_libraries(quickjs PUBLIC m)
add_library(quickjs::quickjs ALIAS quickjs)
