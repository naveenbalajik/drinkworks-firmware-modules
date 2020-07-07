target_sources( ${PROJECT_NAME} PRIVATE
	src/fleetProvisioning.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
	${CMAKE_SOURCE_DIR}/freertos/libraries/abstractions/pkcs11/include
	${CMAKE_SOURCE_DIR}/freertos/libraries/freertos_plus/standard/pkcs11/include
	${CMAKE_SOURCE_DIR}/freertos/libraries/3rdparty/pkcs11
	${CMAKE_SOURCE_DIR}/freertos/libraries/3rdparty/jsmn
)