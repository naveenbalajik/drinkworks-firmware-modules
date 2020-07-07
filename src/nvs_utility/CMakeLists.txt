target_sources( ${PROJECT_NAME} PRIVATE
	src/nvs_utility.c
)

target_include_directories ( ${PROJECT_NAME} BEFORE PRIVATE
	include
)