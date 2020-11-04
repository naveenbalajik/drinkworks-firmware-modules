target_sources( ${PROJECT_NAME} PRIVATE
	src/sha256_support.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)