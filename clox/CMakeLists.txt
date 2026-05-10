cmake_minimum_required(VERSION 3.15)
project(clox C)

if(WIN32)
  set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)
elseif(UNIX)
  set(CMAKE_GENERATOR "Unix Makefiles" CACHE INTERNAL "" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(OUTPUT_BASE ${CMAKE_SOURCE_DIR}/build)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${OUTPUT_BASE}/Debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   ${OUTPUT_BASE}/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${OUTPUT_BASE}/Debug)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_BASE}/Release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_BASE}/Release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_BASE}/Release)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(COMMON_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wno-unused-parameter
)

file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/src/*.h")
add_executable(clox ${SOURCES} ${HEADERS})

target_compile_options(clox PRIVATE ${COMMON_WARNINGS})

if(MSVC)
  target_compile_options(clox PRIVATE
    $<$<CONFIG:Debug>:/Od /Zi>
    $<$<CONFIG:Release>:/O2>
    /W4 /WX
  )
else()
  target_compile_options(clox PRIVATE
    $<$<CONFIG:Debug>:-O0 -g3 -fno-inline>
    $<$<CONFIG:Release>:-O3 -flto>
  )
endif()

option(BUILD_CPPLOX "Build C++ interpreter (cpplox)" OFF)
if(BUILD_CPPLOX)
  enable_language(CXX)
  set(CMAKE_CXX_STANDARD 11)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  add_executable(cpplox ${SOURCES} ${HEADERS})
  target_compile_options(cpplox PRIVATE ${COMMON_WARNINGS})

  if(MSVC)
    target_compile_options(cpplox PRIVATE
      $<$<CONFIG:Debug>:/Od /Zi>
      $<$<CONFIG:Release>:/O2>
      /W4 /WX
    )
  else()
    target_compile_options(cpplox PRIVATE
      $<$<CONFIG:Debug>:-O0 -g>
      $<$<CONFIG:Release>:-O3 -flto>
    )
  endif()
endif()
