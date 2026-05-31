# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_config_bridge)

# Include the module being tested so CO_zephyr_config.h and CO_config.h resolve
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../..          # repo root → CO_config.h via 301/
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../zephyr/include  # CO_zephyr_config.h
)

target_sources(app PRIVATE src/main.c)
