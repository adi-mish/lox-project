cmake_minimum_required(VERSION 3.15)
project(clox C)

#------------------------------------------------------------------------------
# Build configuration
#------------------------------------------------------------------------------
# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

#------------------------------------------------------------------------------
# Output directories
#------------------------------------------------------------------------------
set(OUTPUT_DIR ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR})

#------------------------------------------------------------------------------
# Language standards and flags
#------------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warning flags for C and C++
set(COMMON_WARNINGS
    -Wall
    -Wextra
    -Werror
    -Wno-unused-parameter
)

#------------------------------------------------------------------------------
# Sources and executable
#------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/src/*.h")
add_executable(clox ${SOURCES} ${HEADERS})

target_compile_options(clox PRIVATE ${COMMON_WARNINGS})

# Build-type specific flags
target_compile_definitions(clox PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)
target_compile_options(clox PRIVATE
    $<$<CONFIG:Debug>:-O0 -g>
    $<$<CONFIG:Release>:-O3 -flto>
)

#------------------------------------------------------------------------------
# Optional C++ interpreter (cpplox)
#------------------------------------------------------------------------------
option(BUILD_CPPLOX "Build C++ interpreter (cpplox)" OFF)
if(BUILD_CPPLOX)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    add_executable(cpplox ${SOURCES} ${HEADERS})
    target_compile_options(cpplox PRIVATE ${COMMON_WARNINGS})
    target_compile_options(cpplox PRIVATE
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O3 -flto>
    )
endif()
