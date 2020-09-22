target_sources( ${PROJECT_NAME} PRIVATE
	src/host_ota.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)