# SPDX-License-Identifier: Apache-2.0
#
# Zephyr module for CANopenNode.
#
# Adds Zephyr module support to CANopenNode: sets include paths, builds the
# Zephyr port sources, and conditionally builds core/optional CANopenNode
# components based on Kconfig. Generates the Object Dictionary (OD.c/OD.h)
# from an EDS/XDD using tools/canopen_tools.py, once per build, and wires the
# generated sources into the module library.
#
# @file        CMakeLists.txt
# @author      BitConcepts, LLC <https://github.com/BitConcepts>
# @copyright   2025 BitConcepts, LLC
#
# This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
# file except in compliance with the License. You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations under
# the License.
#
# Usage notes:
# - Expected to live at the root of the CANopenNode Zephyr module.
# - EDS source path is taken from CONFIG_CANOPENNODE_EDS_FILE_PATH; when empty,
#   a sample profile EDS is used (example/DS301_profile.eds).
# - Requires Python 3 to run tools/canopen_tools.py during the build.
# - Generated files are placed under ${CMAKE_CURRENT_BINARY_DIR}/generated and
#   added to the module via zephyr_library_sources().
# - Conditional compilation is driven entirely by CONFIG_CANOPENNODE_* options.

if(CONFIG_CANOPENNODE)

# Discover Zephyr and initialize the build system for a Zephyr application module.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Name this Zephyr build 'canopennode' (module scope).
project(canopennode)

# ------------------------------------------------------------------------------
# Paths and generated-file locations
# ------------------------------------------------------------------------------

# Absolute path to the root of this module (CANopenNode checkout).
set(CANOPENNODE_DIR            "${ZEPHYR_CURRENT_MODULE_DIR}")

# Directory where Object Dictionary generated files will be emitted.
set(CANOPENNODE_OD_GEN_DIR     "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Generated OD C and header outputs from the EDS converter.
set(CANOPENNODE_OD_GEN_C       "${CANOPENNODE_OD_GEN_DIR}/OD.c")
set(CANOPENNODE_OD_GEN_H       "${CANOPENNODE_OD_GEN_DIR}/OD.h")

# Unified tool that drives the EDS/XDD->C/H conversion.
set(CANOPENNODE_OD_GEN_SCRIPT  "${CANOPENNODE_DIR}/tools/canopen_tools.py")

# ------------------------------------------------------------------------------
# Include directories exported to the module and users
# ------------------------------------------------------------------------------

# Make module root, public Zephyr include shim, and generated OD include dir visible.
zephyr_include_directories(
  "${CANOPENNODE_DIR}"
  "${CANOPENNODE_DIR}/zephyr/include"
  "${CANOPENNODE_OD_GEN_DIR}"
)

# ------------------------------------------------------------------------------
# Always-built Zephyr port sources
# ------------------------------------------------------------------------------

# Core Zephyr-facing integration and driver glue (always compiled).
zephyr_library_sources(
  "${CANOPENNODE_DIR}/zephyr/CO_zephyr_integration.c"
  "${CANOPENNODE_DIR}/zephyr/CO_zephyr_driver.c"
)

# ------------------------------------------------------------------------------
# Object Dictionary (EDS -> OD.c/.h) generation
# ------------------------------------------------------------------------------

# The EDS path can be specified via Kconfig (CONFIG_CANOPENNODE_EDS_FILE_PATH).
# If not provided, fall back to a sample profile shipped with the module.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if("${CONFIG_CANOPENNODE_EDS_FILE_PATH}" STREQUAL "")
  # Default to the example DS301 EDS located in the module.
  set(CANOPENNODE_EDS_FILE "${CANOPENNODE_DIR}/example/DS301_profile.eds")
  set(CANOPENNODE_EDS_FILE_DISPLAY "${CANOPENNODE_EDS_FILE}")
else()
  # Interpret the Kconfig path as relative to the application source dir.
  set(CANOPENNODE_EDS_FILE_REL "${CONFIG_CANOPENNODE_EDS_FILE_PATH}")
  set(CANOPENNODE_EDS_FILE "${APPLICATION_SOURCE_DIR}/${CANOPENNODE_EDS_FILE_REL}")
  # Normalize to CMake-style path separators for portability.
  file(TO_CMAKE_PATH "${CANOPENNODE_EDS_FILE}" CANOPENNODE_EDS_FILE)
  set(CANOPENNODE_EDS_FILE_DISPLAY "${CANOPENNODE_EDS_FILE_REL}")
endif()

# Ensure the output directory exists.
file(MAKE_DIRECTORY "${CANOPENNODE_OD_GEN_DIR}")

# Hard fail if the requested EDS file cannot be found to avoid confusing builds.
if(NOT EXISTS "${CANOPENNODE_EDS_FILE}")
  message(FATAL_ERROR "CANopenNode: EDS file not found: ${CANOPENNODE_EDS_FILE}")
endif()

# Friendly status messages to help diagnose generation paths.
message(STATUS "CANopenNode: Using EDS file: ${CANOPENNODE_EDS_FILE}")
message(STATUS "CANopenNode: Will generate OD headers to: ${CANOPENNODE_OD_GEN_H}")
message(STATUS "CANopenNode: Will generate OD sources to: ${CANOPENNODE_OD_GEN_C}")

# Ensure we only ever add this rule once in the whole build, even if the module
# is pulled in multiple times (e.g., by samples/tests that include it repeatedly).
get_property(_co_od_rule_defined GLOBAL PROPERTY CANOPENNODE_OD_RULE_DEFINED SET)
if(NOT _co_od_rule_defined)
  # Custom command that invokes the wrapper to generate OD.c/.h from the EDS.
  add_custom_command(
    OUTPUT     "${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}"
    COMMAND    ${Python3_EXECUTABLE} "${CANOPENNODE_OD_GEN_SCRIPT}"
               "${CANOPENNODE_EDS_FILE}"
               -o "${CANOPENNODE_OD_GEN_DIR}"
    DEPENDS    "${CANOPENNODE_OD_GEN_SCRIPT}" "${CANOPENNODE_EDS_FILE}"
    COMMENT    "CANopenNode: Generating Object Dictionary from ${CANOPENNODE_EDS_FILE_DISPLAY}"
    VERBATIM
  )

  # Phony target representing the successful generation of the OD sources.
  add_custom_target(canopennode_od_gen
    DEPENDS "${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}"
  )

  # Mark generated files so IDEs and Ninja know they come from a command.
  set_source_files_properties(
    "${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}"
    PROPERTIES GENERATED TRUE
  )

  # Add generated OD sources to the Zephyr library; this also establishes build-ordering.
  zephyr_library_sources("${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}")

  # Ensure the module's library waits for OD generation before compiling sources that include OD.h.
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} canopennode_od_gen)

  # Remember that the rule has been defined to avoid duplicate add_custom_command/target redefinition.
  set_property(GLOBAL PROPERTY CANOPENNODE_OD_RULE_DEFINED TRUE)
endif()

# Export the generated include directory so headers can be resolved by users and module code.
zephyr_include_directories("${CANOPENNODE_OD_GEN_DIR}")

# ------------------------------------------------------------------------------
# Core/common sources (built regardless of feature flags)
# ------------------------------------------------------------------------------

# Core CANopenNode runtime, OD interface, and SDO server always compiled.
zephyr_library_sources(
  "${CANOPENNODE_DIR}/CANopen.c"
  "${CANOPENNODE_DIR}/301/CO_ODinterface.c"
  "${CANOPENNODE_DIR}/301/CO_SDOserver.c"
)

# Emergency producer/consumer module (feature selection happens at compile/runtime).
zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_Emergency.c")

# ------------------------------------------------------------------------------
# Optional components (guarded by Kconfig symbols)
# ------------------------------------------------------------------------------

# FIFO utilities are needed by several features; include only when required.
if(CONFIG_CANOPENNODE_FIFO_ENABLE
   OR CONFIG_CANOPENNODE_SDO_CLIENT_ENABLE
   OR CONFIG_CANOPENNODE_GTW_ASCII
   OR CONFIG_CANOPENNODE_GTW_ASCII_SDO)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_fifo.c")
endif()

# CRC16 helper selection:
# - If CONFIG_CANOPENNODE_CRC16_EXTERNAL: use Zephyr shim (module file)
# - Otherwise: use CANopenNode's built-in CRC16 implementation
if(CONFIG_CANOPENNODE_CRC16_ENABLE)
  if(CONFIG_CANOPENNODE_CRC16_EXTERNAL)
    zephyr_library_sources(
      "${CANOPENNODE_DIR}/zephyr/CO_zephyr_crc16-ccitt.c"
    )
  else()
    zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_crc16-ccitt.c")
  endif()
endif()

# Heartbeat consumer (CiA 301, section HB consumer).
if(CONFIG_CANOPENNODE_HB_CONS_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_HBconsumer.c")
endif()

# NMT / Heartbeat producer (always present; behavior gated by config).
zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_NMT_Heartbeat.c")

# Node Guarding master/slave (CiA 301): include when either role is enabled.
if(CONFIG_CANOPENNODE_NODE_GUARDING_SLAVE_ENABLE OR CONFIG_CANOPENNODE_NODE_GUARDING_MASTER_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_Node_Guarding.c")
endif()

# PDO (RPDO/TPDO) processing.
if(CONFIG_CANOPENNODE_RPDO_ENABLE OR CONFIG_CANOPENNODE_TPDO_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_PDO.c")
endif()

# SDO Client.
if(CONFIG_CANOPENNODE_SDO_CLIENT_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_SDOclient.c")
endif()

# SYNC producer/consumer.
if(CONFIG_CANOPENNODE_SYNC_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_SYNC.c")
endif()

# TIME object.
if(CONFIG_CANOPENNODE_TIME_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_TIME.c")
endif()

# Program Download (CiA 302-3)
if(CONFIG_CANOPENNODE_PROG_DOWNLOAD)
  zephyr_library_sources(
    "${CANOPENNODE_DIR}/302/CO_Prog_Download.c"
    "${CANOPENNODE_DIR}/zephyr/CO_zephyr_prog_download.c"
  )
endif()

# LEDs (CiA 303-3): include stack LED state machine and Zephyr GPIO bridge.
if(CONFIG_CANOPENNODE_LEDS_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/303/CO_LEDs.c")
  zephyr_library_sources("${CANOPENNODE_DIR}/zephyr/CO_zephyr_leds.c")
endif()

# GFC / SRDO (CiA 304).
if(CONFIG_CANOPENNODE_GFC_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/304/CO_GFC.c")
endif()
if(CONFIG_CANOPENNODE_SRDO_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/304/CO_SRDO.c")
endif()

# LSS (CiA 305) master/slave.
if(CONFIG_CANOPENNODE_LSS_MASTER)
  zephyr_library_sources("${CANOPENNODE_DIR}/305/CO_LSSmaster.c")
endif()
if(CONFIG_CANOPENNODE_LSS_SLAVE)
  zephyr_library_sources("${CANOPENNODE_DIR}/305/CO_LSSslave.c")
endif()

# ASCII Gateway (CiA 309).
if(CONFIG_CANOPENNODE_GTW_ASCII)
  zephyr_library_sources("${CANOPENNODE_DIR}/309/CO_gateway_ascii.c")
endif()

# Trace (non-standard extension).
if(CONFIG_CANOPENNODE_TRACE_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/extra/CO_trace.c")
endif()

# ------------------------------------------------------------------------------
# Storage backends (Zephyr-specific)
# ------------------------------------------------------------------------------

# Select storage backend glue based on Kconfig:
# - SETTINGS: full storage stack with EEPROM mapping + Zephyr storage bridge
# - RAM: lightweight volatile storage bridge
if(CONFIG_CANOPENNODE_STORAGE_ENABLE)
  zephyr_library_sources(
    "${CANOPENNODE_DIR}/storage/CO_storage.c"
    "${CANOPENNODE_DIR}/zephyr/CO_zephyr_storage.c"
  )
  if(CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS)
    zephyr_library_sources("${CANOPENNODE_DIR}/storage/CO_storageEeprom.c")
  endif()
endif()

endif() # CONFIG_CANOPENNODE
