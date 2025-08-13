cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canopennode)

# ---- Paths ----
set(CANOPENNODE_DIR         "${ZEPHYR_CURRENT_MODULE_DIR}")
set(CANOPENNODE_OD_GEN_DIR  "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CANOPENNODE_OD_GEN_C    "${CANOPENNODE_OD_GEN_DIR}/OD.c")
set(CANOPENNODE_OD_GEN_H    "${CANOPENNODE_OD_GEN_DIR}/OD.h")
set(CANOPENNODE_OD_GEN_SCRIPT "${CANOPENNODE_DIR}/tools/eds2c_wrapper.py")

# ---- Include dirs ----
zephyr_include_directories(
  "${CANOPENNODE_DIR}"
  "${CANOPENNODE_DIR}/zephyr/include"
  "${CANOPENNODE_OD_GEN_DIR}"
)

# ---- Always-built Zephyr port sources ----
zephyr_library_sources(
  "${CANOPENNODE_DIR}/zephyr/CO_zephyr_integration.c"
  "${CANOPENNODE_DIR}/zephyr/CO_zephyr_driver.c"
)

# ---- Object Dictionary generation (EDS -> OD.c/.h) ----
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if("${CONFIG_CANOPENNODE_EDS_FILE_PATH}" STREQUAL "")
  set(CANOPENNODE_EDS_FILE "${CANOPENNODE_DIR}/example/DS301_profile.eds")
  set(CANOPENNODE_EDS_FILE_DISPLAY "${CANOPENNODE_EDS_FILE}")
else()
  set(CANOPENNODE_EDS_FILE_REL "${CONFIG_CANOPENNODE_EDS_FILE_PATH}")
  set(CANOPENNODE_EDS_FILE "${APPLICATION_SOURCE_DIR}/${CANOPENNODE_EDS_FILE_REL}")
  file(TO_CMAKE_PATH "${CANOPENNODE_EDS_FILE}" CANOPENNODE_EDS_FILE)
  set(CANOPENNODE_EDS_FILE_DISPLAY "${CANOPENNODE_EDS_FILE_REL}")
endif()

file(MAKE_DIRECTORY "${CANOPENNODE_OD_GEN_DIR}")

if(NOT EXISTS "${CANOPENNODE_EDS_FILE}")
  message(FATAL_ERROR "CANopenNode: EDS file not found: ${CANOPENNODE_EDS_FILE}")
endif()

message(STATUS "CANopenNode: Using EDS file: ${CANOPENNODE_EDS_FILE}")
message(STATUS "CANopenNode: Will generate OD headers to: ${CANOPENNODE_OD_GEN_H}")
message(STATUS "CANopenNode: Will generate OD sources to: ${CANOPENNODE_OD_GEN_C}")

# Ensure we only ever add this rule once in the whole build
get_property(_co_od_rule_defined GLOBAL PROPERTY CANOPENNODE_OD_RULE_DEFINED SET)
if(NOT _co_od_rule_defined)
  add_custom_command(
    OUTPUT     "${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}"
    COMMAND    ${Python3_EXECUTABLE} "${CANOPENNODE_OD_GEN_SCRIPT}"
               "${CANOPENNODE_EDS_FILE}"
               -o "${CANOPENNODE_OD_GEN_DIR}"
    DEPENDS    "${CANOPENNODE_OD_GEN_SCRIPT}" "${CANOPENNODE_EDS_FILE}"
    COMMENT    "CANopenNode: Generating Object Dictionary from ${CANOPENNODE_EDS_FILE_DISPLAY}"
    VERBATIM
  )

  # Phony target that represents the OD being generated
  add_custom_target(canopennode_od_gen
    DEPENDS "${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}"
  )

  # Mark generated and add as sources so Ninja needs the rule
  set_source_files_properties("${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}" PROPERTIES GENERATED TRUE)
  zephyr_library_sources("${CANOPENNODE_OD_GEN_C}" "${CANOPENNODE_OD_GEN_H}")

  # Make the module library wait for the OD generation
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} canopennode_od_gen)

  # Remember we defined this rule so it won't be added twice
  set_property(GLOBAL PROPERTY CANOPENNODE_OD_RULE_DEFINED TRUE)
endif()

# Include dir so headers resolve
zephyr_include_directories("${CANOPENNODE_OD_GEN_DIR}")

# ---- Core/common sources ----
zephyr_library_sources(
  "${CANOPENNODE_DIR}/CANopen.c"
  "${CANOPENNODE_DIR}/301/CO_ODinterface.c"
)

# Emergency (always build; features are runtime/compile-time gated)
zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_Emergency.c")

# FIFO only if needed
if(CONFIG_CANOPENNODE_FIFO_ENABLE
   OR CONFIG_CANOPENNODE_SDO_CLIENT_ENABLE
   OR CONFIG_CANOPENNODE_GTW_ASCII
   OR CONFIG_CANOPENNODE_GTW_ASCII_SDO)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_fifo.c")
endif()

# CRC16 helper
if(CONFIG_CANOPENNODE_CRC16_ENABLE)
  if(CONFIG_CANOPENNODE_CRC16_EXTERNAL)
    zephyr_library_sources("${CANOPENNODE_DIR}/zephyr/crc16-ccitt_zephyr.c")
  else()
    zephyr_library_sources("${CANOPENNODE_DIR}/301/crc16-ccitt.c")
  endif()
endif()

# Heartbeat consumer
if(CONFIG_CANOPENNODE_HB_CONS_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_HBconsumer.c")
endif()

# NMT / Heartbeat
zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_NMT_Heartbeat.c")

# Node Guarding
if(CONFIG_CANOPENNODE_NODE_GUARDING_SLAVE_ENABLE OR CONFIG_CANOPENNODE_NODE_GUARDING_MASTER_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_Node_Guarding.c")
endif()

# PDO
if(CONFIG_CANOPENNODE_RPDO_ENABLE OR CONFIG_CANOPENNODE_TPDO_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_PDO.c")
endif()

# SDO Server
if(CONFIG_CANOPENNODE_SDO_SERVER_SEGMENTED OR CONFIG_CANOPENNODE_SDO_SERVER_BLOCK)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_SDOserver.c")
endif()

# SDO Client
if(CONFIG_CANOPENNODE_SDO_CLIENT_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_SDOclient.c")
endif()

# SYNC
if(CONFIG_CANOPENNODE_SYNC_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_SYNC.c")
endif()

# TIME
if(CONFIG_CANOPENNODE_TIME_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/301/CO_TIME.c")
endif()

# LEDs (CiA 303-3)
if(CONFIG_CANOPENNODE_LEDS_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/303/CO_LEDs.c")
  zephyr_library_sources("${CANOPENNODE_DIR}/zephyr/CO_zephyr_leds.c")
endif()

# GFC / SRDO (CiA 304)
if(CONFIG_CANOPENNODE_GFC_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/304/CO_GFC.c")
endif()
if(CONFIG_CANOPENNODE_SRDO_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/304/CO_SRDO.c")
endif()

# LSS (CiA 305)
if(CONFIG_CANOPENNODE_LSS_MASTER)
  zephyr_library_sources("${CANOPENNODE_DIR}/305/CO_LSSmaster.c")
endif()
if(CONFIG_CANOPENNODE_LSS_SLAVE)
  zephyr_library_sources("${CANOPENNODE_DIR}/305/CO_LSSslave.c")
endif()

# Gateway (CiA 309)
if(CONFIG_CANOPENNODE_GTW_ASCII)
  zephyr_library_sources("${CANOPENNODE_DIR}/309/CO_gateway_ascii.c")
endif()

# Trace (non-standard)
if(CONFIG_CANOPENNODE_TRACE_ENABLE)
  zephyr_library_sources("${CANOPENNODE_DIR}/extra/CO_trace.c")
endif()

# ---- Storage backend (Zephyr-specific choice) ----
if(CONFIG_CANOPENNODE_STORAGE_ENABLE)
  if(CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS)
    zephyr_library_sources(
      "${CANOPENNODE_DIR}/storage/CO_storage.c"
      "${CANOPENNODE_DIR}/storage/CO_storageEeprom.c"
      "${CANOPENNODE_DIR}/zephyr/CO_zephyr_storage.c"
    )
  elseif(CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM)
    zephyr_library_sources("${CANOPENNODE_DIR}/zephyr/CO_zephyr_storage.c")
  elseif(CONFIG_CANOPENNODE_STORAGE_BACKEND_NONE)
    add_compile_definitions(CONFIG_CANOPENNODE_STORAGE_BACKEND_NONE=1)
  endif()
endif()
