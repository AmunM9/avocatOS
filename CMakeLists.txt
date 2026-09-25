# avocatOS — firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06
cmake_minimum_required(VERSION 3.16)
# Firmware version shown in Ajustes > Información and checked by updates.
set(PROJECT_VER "0.4.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# LVGL 9.5 repeats IRAM section attributes on declaration + definition; GCC 15
# keeps the first one (still IRAM) but warns. Do not fail the build on it.
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=attributes" APPEND)
project(avocatos)
