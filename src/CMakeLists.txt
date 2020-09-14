add_subdirectory(fleetProvisioning)
add_subdirectory(nvs_utility)
add_subdirectory(shci)
add_subdirectory(image_processing)
add_subdirectory(credential_storage_utility)
add_subdirectory(event_records)
add_subdirectory(TimeSync)

# mjson is a submodule. To prevent modification to the repo
# the files and compile definitions are added here
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_RPC=0)
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_MERGE=1)
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_PRETTY=1)
target_compile_definitions( ${PROJECT_NAME} PUBLIC MJSON_ENABLE_NEXT=1)
target_sources( ${PROJECT_NAME} PRIVATE
	mjson/src/mjson.c
)

target_include_directories( ${PROJECT_NAME} BEFORE PRIVATE
	mjson/src
)

