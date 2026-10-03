add_executable(example_simple simple.c)
target_link_libraries(example_simple PRIVATE lightning)
target_include_directories(example_simple PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(example_streaming streaming.c)
target_link_libraries(example_streaming PRIVATE lightning)
target_include_directories(example_streaming PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(example_server server.c)
target_link_libraries(example_server PRIVATE lightning)
target_include_directories(example_server PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_custom_target(lightning_examples DEPENDS
    example_simple example_streaming example_server)
