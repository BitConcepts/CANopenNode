# SPDX-License-Identifier: Apache-2.0

if(CONFIG_CANOPENNODE)

  set(CANOPENNODE_DIR "${ZEPHYR_CURRENT_MODULE_DIR}")
  set(CANOPENNODE_BINARY_DIR "${CMAKE_BINARY_DIR}/modules/${ZEPHYR_CURRENT_MODULE_NAME}")

  set(CANOPENNODE_OD_GEN_SCRIPT "${CANOPENNODE_DIR}/tools/eds2c_wrapper.py")
  set(CANOPENNODE_OD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(CANOPENNODE_OD_GEN_C "${CANOPENNODE_OD_GEN_DIR}/OD.c")
  set(CANOPENNODE_OD_GEN_H "${CANOPENNODE_OD_GEN_DIR}/OD.h")

  # Handle empty CONFIG_CANOPENNODE_EDS_FILE_PATH by defaulting to example EDS
  if("${CONFIG_CANOPENNODE_EDS_FILE_PATH}" STREQUAL "")
    set(CANOPENNODE_EDS_FILE "${CANOPENNODE_DIR}/example/DS301_profile.eds")
  else()
    set(CANOPENNODE_EDS_FILE_REL "${CONFIG_CANOPENNODE_EDS_FILE_PATH}")
    set(CANOPENNODE_EDS_FILE "${APPLICATION_SOURCE_DIR}/${CANOPENNODE_EDS_FILE_REL}")
    file(TO_CMAKE_PATH "${CANOPENNODE_EDS_FILE}" CANOPENNODE_EDS_FILE)
  endif()

  file(MAKE_DIRECTORY "${CANOPENNODE_OD_GEN_DIR}")

  message(STATUS "CANopenNode: Using EDS file: ${CANOPENNODE_EDS_FILE}")
  message(STATUS "CANopenNode: Will generate OD headers to: ${CANOPENNODE_OD_GEN_H}")
  message(STATUS "CANopenNode: Will generate OD sources to: ${CANOPENNODE_OD_GEN_C}")

  # Generate OD files from EDS
  add_custom_command(
      OUTPUT "${CANOPENNODE_OD_GEN_H}" "${CANOPENNODE_OD_GEN_C}"
      COMMAND ${PYTHON_EXECUTABLE} "${CANOPENNODE_OD_GEN_SCRIPT}"
              "${CANOPENNODE_EDS_FILE}"
              -o "${CANOPENNODE_OD_GEN_DIR}"
      DEPENDS "${CANOPENNODE_OD_GEN_SCRIPT}" "${CANOPENNODE_EDS_FILE}"
      COMMENT "CANopenNode: Generating Object Dictionary from ${CANOPENNODE_EDS_FILE_REL}"
      VERBATIM
  )

  add_custom_target(canopennode_od_gen
      DEPENDS "${CANOPENNODE_OD_GEN_H}" "${CANOPENNODE_OD_GEN_C}"
  )

  zephyr_library()

  zephyr_include_directories(
    "${CANOPENNODE_DIR}"
    "${CANOPENNODE_DIR}/301"
    "${CANOPENNODE_DIR}/303"
    "${CANOPENNODE_DIR}/304"
    "${CANOPENNODE_DIR}/305"
    "${CANOPENNODE_DIR}/309"
    "${CANOPENNODE_DIR}/extra"
    "${CANOPENNODE_DIR}/storage"
    "${CANOPENNODE_DIR}/zephyr"
    "${CANOPENNODE_OD_GEN_DIR}"
  )

  zephyr_library_sources(
    "${CANOPENNODE_DIR}/CANopen.c"
    "${CANOPENNODE_DIR}/301/CO_Emergency.c"
    "${CANOPENNODE_DIR}/301/CO_fifo.c"
    "${CANOPENNODE_DIR}/301/CO_HBconsumer.c"
    "${CANOPENNODE_DIR}/301/CO_NMT_Heartbeat.c"
    "${CANOPENNODE_DIR}/301/CO_Node_Guarding.c"
    "${CANOPENNODE_DIR}/301/CO_ODinterface.c"
    "${CANOPENNODE_DIR}/301/CO_PDO.c"
    "${CANOPENNODE_DIR}/301/CO_SDOclient.c"
    "${CANOPENNODE_DIR}/301/CO_SDOserver.c"
    "${CANOPENNODE_DIR}/301/CO_SYNC.c"
    "${CANOPENNODE_DIR}/301/CO_TIME.c"
    "${CANOPENNODE_DIR}/301/crc16-ccitt.c"
    "${CANOPENNODE_OD_GEN_C}"
  )

  zephyr_library_sources("${CANOPENNODE_DIR}/zephyr/CO_driver.c")

  # Optional modules
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_LEDS "${CANOPENNODE_DIR}/303/CO_LEDs.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_304 "${CANOPENNODE_DIR}/304/CO_GFC.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_304 "${CANOPENNODE_DIR}/304/CO_SRDO.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_SYNC_THREAD "${CANOPENNODE_DIR}/305/CO_LSSmaster.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_SYNC_THREAD "${CANOPENNODE_DIR}/305/CO_LSSslave.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_309 "${CANOPENNODE_DIR}/309/CO_gateway_ascii.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_SYNC_THREAD "${CANOPENNODE_DIR}/extra/CO_trace.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_STORAGE "${CANOPENNODE_DIR}/storage/CO_storage.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_STORAGE "${CANOPENNODE_DIR}/storage/CO_storageEeprom.c")
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_PROGRAM_DOWNLOAD "${CANOPENNODE_DIR}/zephyr/canopen_program.c")

endif()
