add_library(Runtime STATIC
    RuntimeAPI.cpp
    RuntimeCacheStats.cpp
    RuntimeSymbols.cpp
    ObjShape.cpp
)
target_include_directories(Runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
