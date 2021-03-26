target_sources( ${PROJECT_NAME} PRIVATE
	src/image_processing.c
	src/capture_task_interface.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)

