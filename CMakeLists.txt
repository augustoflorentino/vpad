cmake_minimum_required(VERSION 3.16)
project(vpad VERSION 1.0.3 DESCRIPTION "Virtual gamepad with force feedback: one API over uinput, IOHIDUserDevice and ViGEmBus" LANGUAGES C)

option(VPAD_BUILD_TESTS "Build tests" OFF)
option(VPAD_BUILD_EXAMPLES "Build examples" OFF)
option(VPAD_BUILD_FUZZ "Build libFuzzer targets" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(vpad src/vpad.c src/vpad_uinput.c src/vpad_macos.c src/vpad_vigem.c src/vpad_none.c)
if(APPLE)
    target_link_libraries(vpad PRIVATE "-framework IOKit" "-framework CoreFoundation")
    set(PC_LIBS_PRIVATE "-framework IOKit -framework CoreFoundation")
elseif(WIN32)
    target_link_libraries(vpad PRIVATE setupapi)
    set(PC_LIBS_PRIVATE "-lsetupapi")
endif()
add_library(vpad::vpad ALIAS vpad)
target_include_directories(vpad PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(vpad PROPERTIES
    C_STANDARD 99
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

set(PC_CFLAGS "")
if(BUILD_SHARED_LIBS)
    target_compile_definitions(vpad PRIVATE VPAD_BUILD_SHARED INTERFACE VPAD_SHARED)
    set(PC_CFLAGS "-DVPAD_SHARED")
endif()

install(TARGETS vpad EXPORT vpadTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT vpadTargets
    NAMESPACE vpad::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vpad)

configure_package_config_file(cmake/vpadConfig.cmake.in
    ${PROJECT_BINARY_DIR}/vpadConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vpad)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/vpadConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${PROJECT_BINARY_DIR}/vpadConfig.cmake
    ${PROJECT_BINARY_DIR}/vpadConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vpad)

configure_file(vpad.pc.in ${PROJECT_BINARY_DIR}/vpad.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/vpad.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(VPAD_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(test_vpad tests/test_vpad.c)
    target_link_libraries(test_vpad PRIVATE vpad Threads::Threads)
    add_test(NAME vpad COMMAND test_vpad)
    set_tests_properties(vpad PROPERTIES SKIP_RETURN_CODE 77)
    add_library(vpad_header_check OBJECT tests/header_check.c)
    target_include_directories(vpad_header_check PRIVATE include)
endif()

if(VPAD_BUILD_EXAMPLES)
    foreach(example basic rumble)
        add_executable(vpad_${example} examples/${example}.c)
        target_link_libraries(vpad_${example} PRIVATE vpad)
    endforeach()
endif()

if(VPAD_BUILD_FUZZ)
    add_executable(fuzz_events fuzz/fuzz_events.c)
    target_include_directories(fuzz_events PRIVATE src include)
    target_link_options(fuzz_events PRIVATE -fsanitize=fuzzer)
endif()
