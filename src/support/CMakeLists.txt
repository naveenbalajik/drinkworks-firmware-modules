target_sources( ${PROJECT_NAME} PRIVATE
	src/sha256_support.c
	src/crc16_ccitt.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)