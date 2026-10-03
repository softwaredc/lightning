set(BENCH_SOURCES bench_parser.c)
set(BENCH_DEPS_DIR ${CMAKE_SOURCE_DIR}/deps)

add_executable(bench_parser ${BENCH_SOURCES})
target_link_libraries(bench_parser PRIVATE lightning)
target_include_directories(bench_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BENCH_DEPS_DIR}
)

# Compile competitor parsers directly into the benchmark binary.
if(EXISTS ${BENCH_DEPS_DIR}/picohttpparser.c)
    target_sources(bench_parser PRIVATE ${BENCH_DEPS_DIR}/picohttpparser.c)
endif()
if(EXISTS ${BENCH_DEPS_DIR}/http_parser.c)
    target_sources(bench_parser PRIVATE ${BENCH_DEPS_DIR}/http_parser.c)
endif()
if(EXISTS ${BENCH_DEPS_DIR}/llhttp.c)
    target_sources(bench_parser PRIVATE
        ${BENCH_DEPS_DIR}/llhttp.c
        ${BENCH_DEPS_DIR}/api.c
        ${BENCH_DEPS_DIR}/helpers.c)
endif()

# Competitor sources are not our code; suppress warnings for them.
set_source_files_properties(
    ${BENCH_DEPS_DIR}/picohttpparser.c
    ${BENCH_DEPS_DIR}/http_parser.c
    ${BENCH_DEPS_DIR}/llhttp.c
    ${BENCH_DEPS_DIR}/api.c
    ${BENCH_DEPS_DIR}/helpers.c
    PROPERTIES COMPILE_FLAGS "-w"
)
