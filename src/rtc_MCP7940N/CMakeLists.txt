target_sources( ${PROJECT_NAME} PRIVATE
	src/rtc_MCP7940N.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
)

#set(COMPONENT_ADD_INCLUDEDIRS
#  driver/include
#  )

#set(COMPONENT_PRIV_INCLUDEDIRS
#  driver/private_include
#  )

#set(COMPONENT_REQUIRES driver)
