target_sources( ${PROJECT_NAME} PRIVATE
	src/bleGap.c
	src/bleConnect.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)