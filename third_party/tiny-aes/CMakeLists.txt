# third_party/tiny-aes — kokke/tiny-AES-c (public domain:
# "All material in this repository is in the public domain",
# https://github.com/kokke/tiny-AES-c).
#
# tiny-AES-c binds ONE key size per translation unit (key schedule
# struct is compile-time sized) with fixed symbol names, so it is
# compiled three times (aes128/192/256.c) with per-size symbol
# prefixes. The C++ side (systems/plugins AesEngine) selects the view
# by key length; context buffers are sized per key schedule
# (176/208/240 + 16 IV) and layouts match aes.h exactly.
add_library(tiny_aes STATIC aes128.c aes192.c aes256.c aes.h)
target_include_directories(tiny_aes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(tiny_aes PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
target_compile_options(tiny_aes PRIVATE -w -O2)
add_library(tiny_aes::tiny_aes ALIAS tiny_aes)
