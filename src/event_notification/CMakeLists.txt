target_sources( ${PROJECT_NAME} PRIVATE
	src/event_notification.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)