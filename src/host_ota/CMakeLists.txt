target_sources( ${PROJECT_NAME} PRIVATE
	src/host_ota.c
	src/host_ota_pal.c
	src/host_ota_ops.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
	${CMAKE_SOURCE_DIR}/freertos/libraries/3rdparty/jsmn
	${CMAKE_SOURCE_DIR}/freertos/libraries/freertos_plus/standard/crypto/include
	${CMAKE_SOURCE_DIR}/freertos/vendors/espressif/boards/esp32/ports/ota
)