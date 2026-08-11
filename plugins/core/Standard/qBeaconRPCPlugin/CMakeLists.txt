# CloudCompare Beacon RPC plugin

option( PLUGIN_STANDARD_QBEACONRPC "Install Beacon RPC plugin" OFF )

if ( PLUGIN_STANDARD_QBEACONRPC )
	project( QBEACON_RPC_PLUGIN )

	AddPlugin( NAME ${PROJECT_NAME} )

	find_package(Qt6 COMPONENTS Network REQUIRED)
	target_link_libraries(${PROJECT_NAME} Qt6::Core Qt6::Network)

	add_subdirectory( include )
	add_subdirectory( src )

	target_include_directories( ${PROJECT_NAME}
								PRIVATE
								${CMAKE_CURRENT_SOURCE_DIR}
	)

	if (WIN32)
		target_link_libraries(${PROJECT_NAME} Advapi32)

		get_target_property( Qt6_NETWORK_LIB_LOCATION Qt6::Network LOCATION_RELEASE )
		copy_files(${Qt6_NETWORK_LIB_LOCATION} "${CLOUDCOMPARE_DEST_FOLDER}" 0)

		if (CMAKE_CONFIGURATION_TYPES)
			get_target_property( Qt6_NETWORK_LIB_DEBUG_LOCATION Qt6::Network LOCATION_DEBUG )
			copy_files(${Qt6_NETWORK_LIB_DEBUG_LOCATION} "${CLOUDCOMPARE_DEST_FOLDER}" 2)
		endif()
	endif()

	add_subdirectory( test )

endif()
