set(NODE_SRC
    node.cpp
    node_compressor.cpp
    db.cpp
    processor.cpp
    txpool.cpp
)

add_library(node STATIC ${NODE_SRC})
add_dependencies(node sqlite core p2p pow)
target_link_libraries(node sqlite core p2p pow)

add_subdirectory(unittests)
add_subdirectory(functionaltests)
