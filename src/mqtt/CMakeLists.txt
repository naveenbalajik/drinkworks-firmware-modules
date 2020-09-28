target_sources( ${PROJECT_NAME} PRIVATE
	src/mqtt.c
)

target_include_directories ( ${PROJECT_NAME} BEFORE PRIVATE
	include
)