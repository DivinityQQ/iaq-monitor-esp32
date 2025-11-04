# Root CMakeLists.txt
# ESP-IDF IAQ Monitor Project

cmake_minimum_required(VERSION 3.16)

# Add nested vendor components to the search path
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/thirdparty/sensirion")

# Include ESP-IDF project.cmake before project()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name and version
project(iaq-monitor VERSION 0.8.0)

# Derive version components robustly and export as compile definitions.
# Some environments may leave PROJECT_VERSION_PATCH empty when it is 0;
# compute explicitly to avoid empty macro values.
set(IAQ_VER "${PROJECT_VERSION}")
if(NOT IAQ_VER)
    set(IAQ_VER "0.8.0")
endif()
string(REPLACE "." ";" _IAQ_VER_LIST "${IAQ_VER}")
list(LENGTH _IAQ_VER_LIST _IAQ_VER_LEN)
list(GET _IAQ_VER_LIST 0 IAQ_VERSION_MAJOR_VAL)
list(GET _IAQ_VER_LIST 1 IAQ_VERSION_MINOR_VAL)
if(_IAQ_VER_LEN GREATER 2)
    list(GET _IAQ_VER_LIST 2 IAQ_VERSION_PATCH_VAL)
else()
    set(IAQ_VERSION_PATCH_VAL 0)
endif()

# Project-wide compile options
idf_build_set_property(COMPILE_OPTIONS "-Wall" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wextra" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-unused-parameter" APPEND)

# Propagate project version to code as IAQ_VERSION_* macros
idf_build_set_property(COMPILE_DEFINITIONS "IAQ_VERSION_MAJOR=${IAQ_VERSION_MAJOR_VAL}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "IAQ_VERSION_MINOR=${IAQ_VERSION_MINOR_VAL}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "IAQ_VERSION_PATCH=${IAQ_VERSION_PATCH_VAL}" APPEND)

# Web content image generation is handled in main/CMakeLists.txt via
# littlefs_create_partition_image once the LittleFS component is available.
