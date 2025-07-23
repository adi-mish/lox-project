cmake_minimum_required(VERSION 3.22)
project(eloxir LANGUAGES CXX)

find_package(LLVM 19.1 REQUIRED CONFIG)         # honour llvm-config defaults
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_subdirectory(runtime)
add_subdirectory(frontend)
add_subdirectory(codegen)
add_subdirectory(jit)
add_executable(eloxir tools/repl.cpp)
target_link_libraries(eloxir PRIVATE
    Frontend CodeGen Runtime EloxirJIT
    LLVMSupport LLVMOrcJIT LLVMAsmParser LLVMCore LLVMExecutionEngine
)
