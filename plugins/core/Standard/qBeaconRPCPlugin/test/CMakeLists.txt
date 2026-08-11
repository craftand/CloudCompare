find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

add_executable(TestOrchestratorConfig
    ${CMAKE_CURRENT_LIST_DIR}/TestOrchestratorConfig.cpp
)
target_include_directories(TestOrchestratorConfig PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(TestOrchestratorConfig PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME TestOrchestratorConfig COMMAND TestOrchestratorConfig)

add_executable(TestJsonRPCResult
    ${CMAKE_CURRENT_LIST_DIR}/TestJsonRPCResult.cpp
)
target_include_directories(TestJsonRPCResult PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(TestJsonRPCResult PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
add_test(NAME TestJsonRPCResult COMMAND TestJsonRPCResult)

add_executable(TestSseParser
    ${CMAKE_CURRENT_LIST_DIR}/TestSseParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sseclient.cpp
)
target_include_directories(TestSseParser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(TestSseParser PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
add_test(NAME TestSseParser COMMAND TestSseParser)

add_executable(TestJsonRPCClient
    ${CMAKE_CURRENT_LIST_DIR}/TestJsonRPCClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/jsonrpcclient.cpp
)
target_include_directories(TestJsonRPCClient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(TestJsonRPCClient PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
add_test(NAME TestJsonRPCClient COMMAND TestJsonRPCClient)
