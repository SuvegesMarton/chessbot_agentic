cmake_minimum_required(VERSION 3.20)
project(chessbot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/O2 /W4)
else()
    add_compile_options(-O3 -march=native -Wall -Wextra)
endif()

enable_testing()

add_executable(chessbot src/main.cpp src/position.cpp)
target_include_directories(chessbot PRIVATE src)

add_executable(test_main tests/test_main.cpp)
target_include_directories(test_main PRIVATE src)

add_executable(test_position tests/test_position.cpp src/position.cpp)
target_include_directories(test_position PRIVATE src)

add_test(NAME test_main COMMAND test_main)
add_test(NAME test_bitboard COMMAND test_bitboard)
add_test(NAME test_position COMMAND test_position)