# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Include the components from the neighboring driver_rmt project
set(EXTRA_COMPONENT_DIRS "../driver_rmt/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(software_uart_rmt)