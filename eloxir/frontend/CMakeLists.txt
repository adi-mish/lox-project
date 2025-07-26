add_library(Frontend STATIC
    Scanner.cpp
    Parser.cpp
    Resolver.cpp
    Expr.cpp
    Stmt.cpp
)

target_include_directories(Frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link with CodeGen and Runtime if needed
# target_link_libraries(Frontend PUBLIC CodeGen Runtime)