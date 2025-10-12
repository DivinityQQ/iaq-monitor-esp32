# Root CMakeLists.txt
# ESP-IDF IAQ Monitor Project

cmake_minimum_required(VERSION 3.16)

# Add nested vendor components to the search path
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/thirdparty/sensirion")

# Include ESP-IDF project.cmake before project()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name and version
project(iaq-monitor VERSION 0.6.3)

# Project-wide compile options
idf_build_set_property(COMPILE_OPTIONS "-Wall" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wextra" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-unused-parameter" APPEND)
