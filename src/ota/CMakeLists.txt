target_sources( ${PROJECT_NAME} PRIVATE
	src/ota_update.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)