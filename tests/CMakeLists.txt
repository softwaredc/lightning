set(LHT_TESTS
    test_parser
    test_chunked
    test_url
    test_streaming
    test_fuzz
)

foreach(t ${LHT_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE lightning)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
