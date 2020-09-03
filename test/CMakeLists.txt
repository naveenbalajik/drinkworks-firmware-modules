target_sources( ${PROJECT_NAME} PRIVATE
	fifo_test.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	.
)