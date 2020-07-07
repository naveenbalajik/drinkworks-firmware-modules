target_sources( ${PROJECT_NAME} PRIVATE
	src/fleetProvisioning.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
	../../freertos/libraries/abstractions/pkcs11/include
	../../freertos/libraries/freertos_plus/standard/pkcs11/include
	../../freertos/libraries/3rdparty/pkcs11
	../../freertos/libraries/3rdparty/jsmn
)