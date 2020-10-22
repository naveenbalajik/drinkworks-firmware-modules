target_sources( ${PROJECT_NAME} PRIVATE
	src/sysParam.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)