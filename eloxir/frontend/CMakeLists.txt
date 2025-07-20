add_library(Frontend STATIC
    Parser.cpp
    Resolver.cpp
)
target_include_directories(Frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Frontend PRIVATE Runtime)
