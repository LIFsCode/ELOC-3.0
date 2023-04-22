# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.
include(CMakePrintHelpers)

cmake_print_variables(CMAKE_SOURCE_DIR)
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

message("message to display")
cmake_print_variables(CMAKE_SOURCE_DIR)
cmake_print_variables(app_sources)

idf_component_register(SRCS ${app_sources})
