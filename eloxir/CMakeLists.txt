cmake_minimum_required(VERSION 3.22)
project(eloxir LANGUAGES C CXX)

# 1) Enforce generator per platform
if (CMAKE_HOST_UNIX AND NOT CMAKE_GENERATOR STREQUAL "Unix Makefiles")
  message(FATAL_ERROR
    "On UNIX platforms, re-run CMake with: -G \"Unix Makefiles\""
  )
elseif (CMAKE_HOST_WIN32 AND NOT CMAKE_GENERATOR STREQUAL "Ninja")
  message(FATAL_ERROR
    "On Windows, re-run CMake with: -G Ninja"
  )
endif()

# 2) Standard settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3) Find LLVM, include dirs, definitions…
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 4) Subdirectories
add_subdirectory(runtime)
add_subdirectory(frontend)
add_subdirectory(codegen)
add_subdirectory(jit)

# 5) Define your executable
add_executable(eloxir tools/repl.cpp)
target_link_libraries(eloxir PRIVATE
  Frontend CodeGen Runtime EloxirJIT
  LLVMSupport LLVMOrcJIT LLVMAsmParser LLVMCore LLVMExecutionEngine
  LLVMPasses LLVMAnalysis LLVMTransformUtils LLVMScalarOpts LLVMTarget
  LLVMAsmPrinter LLVMX86Desc LLVMX86Info LLVMX86CodeGen LLVMX86AsmParser
)

# 6) Apply minimal warnings + sanitizers
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(eloxir PRIVATE
    $<$<CONFIG:Debug>: -g3 -Wall -Wextra -Wpedantic>
    $<$<CONFIG:Release>:-Wall -Wextra -Wpedantic -O3 -DNDEBUG>
  )
elseif (MSVC)
  target_compile_options(eloxir PRIVATE
    $<$<CONFIG:Debug>:/W4 /WX /Zi /Od>
    $<$<CONFIG:Release>:/W4 /WX /O2 /DNDEBUG>
  )
endif()
