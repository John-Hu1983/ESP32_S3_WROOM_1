# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources
	${CMAKE_SOURCE_DIR}/src/*.c
	${CMAKE_SOURCE_DIR}/src/*.cc
	${CMAKE_SOURCE_DIR}/src/*.cpp
)

# Keep desktop simulator sources under src/desktop out of firmware build.
LIST(FILTER app_sources EXCLUDE REGEX ".*/src/desktop/lvgl_launcher/.*")

set(assets_root_dir ${CMAKE_SOURCE_DIR}/lib/assets)

idf_component_register(SRCS ${app_sources}
				   REQUIRES lvgl spiffs esp_audio_codec)

if(COMMAND spiffs_create_partition_image)
	if(EXISTS ${assets_root_dir})
		spiffs_create_partition_image(storage ${assets_root_dir} FLASH_IN_PROJECT)
		if(TARGET spiffs_storage_bin)
			add_dependencies(${COMPONENT_LIB} spiffs_storage_bin)
		endif()
	endif()
endif()
