add_library(EloxirJIT STATIC
    EloxirJIT.cpp
)
target_include_directories(EloxirJIT PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(EloxirJIT PRIVATE CodeGen)
