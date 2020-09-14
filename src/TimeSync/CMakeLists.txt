target_sources( ${PROJECT_NAME} PRIVATE
	src/TimeSync.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
	${CMAKE_SOURCE_DIR}/freertos/vendors/espressif/esp-idf/components/lwip/include/apps/sntp/
	${CMAKE_SOURCE_DIR}/freertos/vendors/espressif/esp-idf/components/lwip/lwip/src/include/lwip/apps
	
)