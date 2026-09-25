# avocatOS desktop simulator (macOS/Linux, SDL2).
#   cmake -S sim -B sim/build && cmake --build sim/build -j
# LVGL sources come from the ESP-IDF managed component (run idf.py reconfigure once).
cmake_minimum_required(VERSION 3.16)
project(avocatos_sim C)
set(CMAKE_C_STANDARD 11)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL_DIR ${ROOT}/managed_components/lvgl__lvgl CACHE PATH "LVGL source")
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    message(FATAL_ERROR "LVGL not found in ${LVGL_DIR}. Run ./tools/idf.sh reconfigure first.")
endif()

find_path(SDL2_INCLUDE_DIRS SDL2/SDL.h HINTS /opt/homebrew/include /usr/local/include /usr/include REQUIRED)
find_library(SDL2_LIBRARIES SDL2 HINTS /opt/homebrew/lib /usr/local/lib REQUIRED)

file(GLOB_RECURSE LVGL_SRCS ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRCS})
target_include_directories(lvgl PUBLIC ${LVGL_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -w -O2)

file(GLOB AVO_SRCS
    ${ROOT}/components/avo_core/src/*.c
    ${ROOT}/components/avo_ui/src/*.c
    ${ROOT}/components/avo_ui/assets/fonts/*.c
    ${ROOT}/components/avo_ui/assets/img/*.c)
add_executable(avocatos_sim main.c hal_sim.c ${AVO_SRCS})
target_include_directories(avocatos_sim PRIVATE
    ${ROOT}/components/avo_core/include ${ROOT}/components/avo_hal/include
    ${ROOT}/components/avo_ui/include ${ROOT}/components/avo_ui/src)
target_compile_options(avocatos_sim PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -O1 -g)
target_link_libraries(avocatos_sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
