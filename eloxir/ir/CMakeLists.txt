add_library(LoxIR STATIC
    LoxIR.cpp
    LoxIRPrinter.cpp
    LoxIRVerifier.cpp
    LoxPassManager.cpp
)
target_include_directories(LoxIR PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
