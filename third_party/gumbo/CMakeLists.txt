# third_party/gumbo — google/gumbo-parser v0.10.1 (Apache 2.0, see
# COPYING). HTML5 parser for the plugin DOM (cheerio) bridge; CSS
# selector matching is layered in C++ (SelectorMatcher).
add_library(gumbo STATIC
    src/attribute.c
    src/char_ref.c
    src/error.c
    src/parser.c
    src/string_buffer.c
    src/string_piece.c
    src/tag.c
    src/tokenizer.c
    src/utf8.c
    src/util.c
    src/vector.c)

target_include_directories(gumbo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(gumbo PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
target_compile_options(gumbo PRIVATE -w -O2)
add_library(gumbo::gumbo ALIAS gumbo)
