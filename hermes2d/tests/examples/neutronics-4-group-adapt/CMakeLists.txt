if(NOT H2D_REAL)
	return()
endif(NOT H2D_REAL)

project(example-neutronics-4-group-adapt)

add_executable(${PROJECT_NAME} main.cpp)
include (../../CMake.common)

set(BIN ${PROJECT_BINARY_DIR}/${PROJECT_NAME})
add_test(example-neutronics-4-group-adapt-projection ${BIN})
set_tests_properties(example-neutronics-4-group-adapt-projection PROPERTIES LABELS slow)

