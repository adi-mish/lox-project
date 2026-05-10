add_library(LoxOpt STATIC
    LoxOptimizationPasses.cpp
)
target_include_directories(LoxOpt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LoxOpt PUBLIC LoxIR)
