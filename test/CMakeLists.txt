SET(mempool_test_SRCS
    ${CMAKE_SOURCE_DIR}/test/src/mempool_test.cc
)

add_executable(mempool_test ${mempool_test_SRCS})

target_link_libraries(mempool_test pthread atomic)
