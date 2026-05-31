# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_pdo_bitwise)

set(MODULE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

target_include_directories(app PRIVATE
    ${MODULE_ROOT}
    ${MODULE_ROOT}/zephyr/include
)

target_sources(app PRIVATE src/main.c)
