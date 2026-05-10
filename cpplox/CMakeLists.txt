cmake_minimum_required(VERSION 3.20)

project(cpplox LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPPLOX_ENABLE_VM_STATS "Compile optional cpplox VM execution counters" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/RelWithDebInfo)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/MinSizeRel)

file(GLOB_RECURSE CPPLOX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE CPPLOX_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(cpplox ${CPPLOX_SOURCES} ${CPPLOX_HEADERS})
target_include_directories(cpplox PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cpplox/bytecode"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cpplox/frontend"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cpplox/runtime"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cpplox/support"
)
if(CPPLOX_ENABLE_VM_STATS)
  target_compile_definitions(cpplox PRIVATE CPPLOX_ENABLE_VM_STATS=1)
endif()

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
