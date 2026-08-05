cmake_minimum_required(VERSION 3.16)
project(vpad_consumer LANGUAGES C)

find_package(vpad REQUIRED)

add_executable(consumer ${CMAKE_CURRENT_SOURCE_DIR}/../../examples/basic.c)
target_link_libraries(consumer PRIVATE vpad::vpad)
