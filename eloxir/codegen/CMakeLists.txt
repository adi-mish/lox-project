add_library(CodeGen STATIC
    CodeGenVisitor.cpp
    BuiltinsIR.cpp
)
target_include_directories(CodeGen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CodeGen PRIVATE Frontend Runtime)
