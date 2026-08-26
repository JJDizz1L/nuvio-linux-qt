# Compiler/portability policy for the Nuvio Qt line.
# Read me before "improving" flags: every line here exists because shipping a
# bundled-JVM-style CPU-gate regression (x86-64-v4 disaster, AGENTS.md) taught
# us the cost. Defaults must run everywhere, not fast on one box.

if(CMAKE_CXX_FLAGS MATCHES "march=native|-mtune=native|-march=x86-64-v[234]")
    if(NOT DEFINED ENV{NUVIO_ALLOW_NATIVE_FLAGS})
        message(FATAL_ERROR
            "Refusing native/arch-specific -march/-mtune flags.\n"
            "Nuvio release binaries must remain baseline-x86-64 portable\n"
            "(see AGENTS.md 'Generic x86-64 portability').\n"
            "Escapes require the NUVIO_ALLOW_NATIVE_FLAGS environment variable.")
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# NOTE: deliberately NO -fvisibility=hidden here: QLoggingCategory accessors
# and any future QML type registration must keep default visibility across
# our static-lib boundaries; re-enable per-target later if a genuine ABI
# boundary appears (plan §7.2 keeps headers narrow instead).

function(nuvio_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3)
        return()
    endif()
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
    if(NUVIO_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

function(nuvio_common_target_props target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    nuvio_apply_warnings(${target})
endfunction()
