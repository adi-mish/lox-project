cmake_minimum_required(VERSION 3.23)

project(loxpp LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/RelWithDebInfo)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/MinSizeRel)

file(GLOB LOXPP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB LOXPP_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(loxpp ${LOXPP_SOURCES} ${LOXPP_HEADERS})
target_include_directories(loxpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
  target_compile_options(loxpp PRIVATE /W4 /WX /permissive-)
  target_compile_options(loxpp PRIVATE $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(loxpp PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    $<$<CONFIG:Debug>:-O0 -g3 -fno-inline>
    $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-O3 -g -march=native -DNDEBUG>
  )
endif()
