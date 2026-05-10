add_library(LoxLLVM STATIC
    LoxIRCodeGen.cpp
)
target_include_directories(LoxLLVM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LoxLLVM PUBLIC LoxIR CodeGen Runtime)
