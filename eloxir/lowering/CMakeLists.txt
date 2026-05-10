add_library(LoxLowering STATIC
    AstLowerer.cpp
)
target_include_directories(LoxLowering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LoxLowering PUBLIC Frontend LoxIR)
