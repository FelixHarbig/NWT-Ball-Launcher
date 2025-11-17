cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(person_web)

# Force lower optimization level for esp-tflite-micro (prevents GCC ICE)
idf_component_get_property(tflite_dir esp-tflite-micro COMPONENT_DIR)
target_compile_options(__idf_esp-tflite-micro PRIVATE -O1)
