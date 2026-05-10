add_library(Frontend STATIC
    Scanner.cpp
    Parser.cpp
    Resolver.cpp
    Expr.cpp
    Stmt.cpp
)

target_include_directories(Frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
