cmake_minimum_required(VERSION 3.10)

project(fubcc CXX)

# find_package(Boost COMPONENTS system filesystem REQUIRED)

set (CMAKE_CXX_STANDARD 17)
if (NOT MSVC)
  set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic")
endif()

add_executable(fubcc fubcc.cc)

# target_include_directories(fubcc PRIVATE ${Boost_INCLUDE_DIRS})
# target_link_libraries(fubcc PRIVATE ${Boost_LIBRARIES})

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set_target_properties(fubcc PROPERTIES
        OSX_ARCHITECTURES "x86_64;arm64"
    )
endif()
