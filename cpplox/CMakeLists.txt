cmake_minimum_required(VERSION 3.20)

project(cpplox LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/build/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/build/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/build/RelWithDebInfo)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_SOURCE_DIR}/build/MinSizeRel)

file(GLOB CPPLOX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB CPPLOX_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(cpplox ${CPPLOX_SOURCES} ${CPPLOX_HEADERS})
target_include_directories(cpplox PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
  target_compile_options(cpplox PRIVATE /W4 /WX /permissive-)
  target_compile_options(cpplox PRIVATE $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(cpplox PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-O0 -g3 -fno-inline>
    $<$<CONFIG:Release>:-O3 -flto -march=native -DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-O3 -g -march=native -DNDEBUG>
  )
  target_link_options(cpplox PRIVATE
    $<$<CONFIG:Release>:-flto>
  )
endif()
