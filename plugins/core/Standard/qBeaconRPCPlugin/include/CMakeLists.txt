target_sources( ${PROJECT_NAME}
	PRIVATE
                ${CMAKE_CURRENT_LIST_DIR}/JsonRPCPlugin.h
                ${CMAKE_CURRENT_LIST_DIR}/jsonrpcclient.h
                ${CMAKE_CURRENT_LIST_DIR}/sseclient.h
                ${CMAKE_CURRENT_LIST_DIR}/orchestrator_config.h
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)
