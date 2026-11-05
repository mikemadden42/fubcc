cmake_minimum_required(VERSION 3.10)

project(fubcc CXX)

if (NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
    message(FATAL_ERROR "fubcc reads Mach-O binaries and only builds on macOS")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fubcc fubcc.cc)

target_compile_options(fubcc PRIVATE -Wall -Wextra -pedantic)
set_target_properties(fubcc PROPERTIES
    OSX_ARCHITECTURES "x86_64;arm64"
)
target_link_libraries(fubcc PRIVATE "-framework CoreFoundation")

enable_testing()
add_test(NAME fubcc
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh $<TARGET_FILE:fubcc>
)
