project(lobatto-zero-values-2)

add_executable(${PROJECT_NAME} 
        main.cpp
)
include (../../CMake.common)

set(BIN ${PROJECT_BINARY_DIR}/${PROJECT_NAME})
add_test(lobatto-zero-values-2 ${BIN})
