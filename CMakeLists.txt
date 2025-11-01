# Root CMakeLists.txt
# ESP-IDF IAQ Monitor Project

cmake_minimum_required(VERSION 3.16)

# Add nested vendor components to the search path
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/thirdparty/sensirion")

# Include ESP-IDF project.cmake before project()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name and version
project(iaq-monitor VERSION 0.7.6)

# Project-wide compile options
idf_build_set_property(COMPILE_OPTIONS "-Wall" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wextra" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-unused-parameter" APPEND)

# Propagate project version to code as IAQ_VERSION_* macros
idf_build_set_property(COMPILE_DEFINITIONS "IAQ_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "IAQ_VERSION_MINOR=${PROJECT_VERSION_MINOR}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "IAQ_VERSION_PATCH=${PROJECT_VERSION_PATCH}" APPEND)

# Create SPIFFS image for web portal if source dir exists
set(WEB_DIR "${CMAKE_CURRENT_LIST_DIR}/www")
if (EXISTS ${WEB_DIR})
    message(STATUS "Including SPIFFS web content from: ${WEB_DIR}")
    spiffs_create_partition_image(www ${WEB_DIR} FLASH_IN_PROJECT)
else()
    message(STATUS "No web dir '${WEB_DIR}' found; SPIFFS image will be empty or preflashed")
endif()
