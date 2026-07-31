# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources
	${CMAKE_SOURCE_DIR}/src/*.c
	${CMAKE_SOURCE_DIR}/src/*.cc
	${CMAKE_SOURCE_DIR}/src/*.cpp
)

# Keep desktop simulator sources under src/desktop out of firmware build.
LIST(FILTER app_sources EXCLUDE REGEX ".*/src/desktop/lvgl_launcher/.*")

idf_component_register(SRCS ${app_sources}
				   REQUIRES lvgl)
