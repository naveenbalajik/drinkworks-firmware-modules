target_sources( ${PROJECT_NAME} PRIVATE
	src/shadow.c
)

target_include_directories ( ${PROJECT_NAME} BEFORE PRIVATE
	include
)
