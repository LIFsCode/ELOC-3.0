cmake_minimum_required(VERSION 3.16.0)

# Derive PROJECT_VER from the VERSION define in project_config.h so the firmware's
# embedded esp_app_desc_t carries the real firmware version instead of the
# git-describe fallback. Re-run configure whenever the header changes.
set(eloc_project_config_header "${CMAKE_CURRENT_SOURCE_DIR}/include/project_config.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${eloc_project_config_header}")

file(STRINGS "${eloc_project_config_header}" eloc_version_line REGEX "^[ \t]*#define[ \t]+VERSION[ \t]+\"[^\"]+\"")
if(NOT eloc_version_line)
    message(FATAL_ERROR "Could not find '#define VERSION \"...\"' in ${eloc_project_config_header}")
endif()
string(REGEX MATCH "\"([^\"]+)\"" _eloc_version_match "${eloc_version_line}")
set(PROJECT_VER "${CMAKE_MATCH_1}")
if(NOT PROJECT_VER)
    message(FATAL_ERROR "Failed to parse VERSION string out of ${eloc_project_config_header}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(idf-wav-sdcard)
