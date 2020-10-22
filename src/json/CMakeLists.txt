#
# Intent is that individual modules, required by project, and included in that projects root CMakeLists.txt file
#

# mjson is a submodule. To prevent modification to the repo
# the files and compile definitions are added here
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_RPC=0)
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_MERGE=1)
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_PRETTY=1)
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_NEXT=1)
target_sources( ${PROJECT_NAME} PRIVATE
	src/json.c
	mjson/src/mjson.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	include
	mjson/src
)

