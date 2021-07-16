target_sources( ${PROJECT_NAME} PRIVATE
	src/sha256_support.c
	src/crc16_ccitt.c
	src/pressure.c
	src/temperature.c
	src/reset_support.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)