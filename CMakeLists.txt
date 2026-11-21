cmake_minimum_required(VERSION 3.20)
project(chessbot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/O2 /W4)
    set(CHESSBOT_NATIVE_FLAGS "")
else()
    add_compile_options(-O3 -Wall -Wextra)
    set(CHESSBOT_NATIVE_FLAGS -march=native)
endif()

# -march=native is applied per target rather than globally. The vendored
# cpp-httplib WebSocket *client* (exercised only by test_web_server) is
# miscompiled by GCC at -O3 -march=native on this toolchain and crashes
# intermittently, so that target intentionally omits it. All other targets
# keep the project's -march=native build per AGENTS.md.
function(chessbot_enable_native target)
    if(CHESSBOT_NATIVE_FLAGS)
        target_compile_options(${target} PRIVATE ${CHESSBOT_NATIVE_FLAGS})
    endif()
endfunction()

enable_testing()

add_executable(chessbot src/main.cpp src/position.cpp src/game.cpp src/web_server.cpp)
target_include_directories(chessbot PRIVATE src third_party)
if(WIN32)
    target_link_libraries(chessbot PRIVATE ws2_32)
endif()
chessbot_enable_native(chessbot)

add_executable(test_main tests/test_main.cpp)
target_include_directories(test_main PRIVATE src)
chessbot_enable_native(test_main)

add_executable(test_position tests/test_position.cpp src/position.cpp)
target_include_directories(test_position PRIVATE src)
chessbot_enable_native(test_position)

add_executable(test_bitboard tests/test_bitboard.cpp)
target_include_directories(test_bitboard PRIVATE src)
chessbot_enable_native(test_bitboard)

add_executable(test_move tests/test_move.cpp)
target_include_directories(test_move PRIVATE src)
chessbot_enable_native(test_move)

add_executable(test_position_helpers tests/test_position_helpers.cpp src/position.cpp)
target_include_directories(test_position_helpers PRIVATE src)
chessbot_enable_native(test_position_helpers)

add_executable(test_web_server tests/test_web_server.cpp src/web_server.cpp src/game.cpp src/position.cpp)
target_include_directories(test_web_server PRIVATE src third_party)
if(WIN32)
    target_link_libraries(test_web_server PRIVATE ws2_32)
endif()
# Intentionally no chessbot_enable_native(test_web_server): see note above.

add_test(NAME test_main COMMAND test_main)
add_test(NAME test_bitboard COMMAND test_bitboard)
add_test(NAME test_move COMMAND test_move)
add_test(NAME test_position COMMAND test_position)
add_test(NAME test_position_helpers COMMAND test_position_helpers)
add_test(NAME test_web_server COMMAND test_web_server)