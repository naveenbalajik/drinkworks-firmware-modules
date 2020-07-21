target_sources( ${PROJECT_NAME} PRIVATE
	src/image_processing.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)
