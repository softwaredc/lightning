cmake_minimum_required(VERSION 3.14)
project(lightning C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# SIMD: build the fast paths when the compiler supports it. We compile the
# whole library with AVX2/SSE4.2 on x86_64 when available; runtime dispatch
# (lht_has_*) selects the actual code path so the same binary runs everywhere.
include(CheckCCompilerFlag)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i386|i686)")
    check_c_compiler_flag("-mavx2" LHT_HAS_AVX2_FLAG)
    check_c_compiler_flag("-msse4.2" LHT_HAS_SSE42_FLAG)
    if(LHT_HAS_AVX2_FLAG)
        set(LHT_SIMD_FLAGS "-mavx2")
    elseif(LHT_HAS_SSE42_FLAG)
        set(LHT_SIMD_FLAGS "-msse4.2")
    endif()
endif()

set(LHT_SOURCES
    src/lightning.c
    src/chunked.c
    src/url.c
)

add_library(lightning STATIC ${LHT_SOURCES})
target_include_directories(lightning PUBLIC include)
target_compile_options(lightning PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    ${LHT_SIMD_FLAGS}
)

add_library(lightning_shared SHARED ${LHT_SOURCES})
target_include_directories(lightning_shared PUBLIC include)
set_target_properties(lightning_shared PROPERTIES OUTPUT_NAME lightning)
target_compile_options(lightning_shared PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    ${LHT_SIMD_FLAGS}
)

# Build options
option(LHT_BUILD_TESTS "Build tests" ON)
option(LHT_BUILD_BENCHMARKS "Build benchmarks" ON)
option(LHT_BUILD_EXAMPLES "Build examples" ON)
option(LHT_SANITIZE "Build with ASan/UBSan" OFF)

if(LHT_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(LHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LHT_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(LHT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(TARGETS lightning ARCHIVE DESTINATION lib)
install(DIRECTORY include/lightning DESTINATION include)
