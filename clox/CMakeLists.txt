cmake_minimum_required(VERSION 3.15)
project(clox C)

#------------------------------------------------------------------------------
# Generator selection
#------------------------------------------------------------------------------
if(WIN32)
  # Force Ninja on Windows
  set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)
elseif(UNIX)
  # Force Unix Makefiles on Unix
  set(CMAKE_GENERATOR "Unix Makefiles" CACHE INTERNAL "" FORCE)
endif()

#------------------------------------------------------------------------------
# Build configuration
#------------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

#------------------------------------------------------------------------------
#------------------------------------------------------------------------------  
# Output directories (per-configuration)  
#------------------------------------------------------------------------------
set(OUTPUT_BASE ${CMAKE_SOURCE_DIR}/build)

# For single-config generators (e.g. Unix Makefiles, Ninja)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${OUTPUT_BASE}/Debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   ${OUTPUT_BASE}/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${OUTPUT_BASE}/Debug)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_BASE}/Release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_BASE}/Release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_BASE}/Release)

#------------------------------------------------------------------------------
# Language standards and flags
#------------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common warnings
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

# Apply common warnings
target_compile_options(clox PRIVATE ${COMMON_WARNINGS})

# MSVC-specific flags
if(MSVC)
  target_compile_options(clox PRIVATE
    $<$<CONFIG:Debug>:/Od /Zi>
    $<$<CONFIG:Release>:/O2>
    /W4 /WX
  )
else()
  target_compile_options(clox PRIVATE
    $<$<CONFIG:Debug>:-O0 -g>
    $<$<CONFIG:Release>:-O3 -flto>
  )
endif()

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
