# Root CMakeLists.txt
# ESP-IDF IAQ Monitor Project

cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF project.cmake before project()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name and version
project(iaq-monitor VERSION 0.1.0)

# Project-wide compile options
idf_build_set_property(COMPILE_OPTIONS "-Wall" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wextra" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-unused-parameter" APPEND)