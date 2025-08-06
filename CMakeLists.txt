# SPDX-License-Identifier: Apache-2.0

if(CONFIG_CANOPENNODE)

  set(CANOPENNODE_DIR ${ZEPHYR_CURRENT_MODULE_DIR})

  zephyr_library()

  zephyr_include_directories(
    ${CANOPENNODE_DIR}
    ${CANOPENNODE_DIR}/301
    ${CANOPENNODE_DIR}/303
    ${CANOPENNODE_DIR}/304
    ${CANOPENNODE_DIR}/305
    ${CANOPENNODE_DIR}/309
    ${CANOPENNODE_DIR}/extra
    ${CANOPENNODE_DIR}/storage
    ${CANOPENNODE_DIR}/zephyr
  )

  # === Core CANopen Stack (CiA 301) ===
  zephyr_library_sources(
    ${CANOPENNODE_DIR}/CANopen.c
    ${CANOPENNODE_DIR}/301/CO_Emergency.c
    ${CANOPENNODE_DIR}/301/CO_NMT_Heartbeat.c
    ${CANOPENNODE_DIR}/301/CO_PDO.c
    ${CANOPENNODE_DIR}/301/CO_SDO.c
    ${CANOPENNODE_DIR}/301/CO_SYNC.c
    ${CANOPENNODE_DIR}/301/CO_TIME.c
    ${CANOPENNODE_DIR}/301/CO_driver.c
    ${CANOPENNODE_DIR}/301/CO_ODinterface.c
    ${CANOPENNODE_DIR}/301/CO_fifo.c
    ${CANOPENNODE_DIR}/301/CO_trace.c
  )

  # === Optional Features ===

  # Heartbeat consumer & LSS (CiA 305)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_305 ${CANOPENNODE_DIR}/305/CO_HBconsumer.c)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_305 ${CANOPENNODE_DIR}/305/CO_LSSmaster.c)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_305 ${CANOPENNODE_DIR}/305/CO_LSSslave.c)

  # LEDs (CiA 303-3)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_LEDS ${CANOPENNODE_DIR}/303/CO_LEDs.c)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_LEDS ${CANOPENNODE_DIR}/zephyr/canopen_leds.c)

  # SYNC thread (CiA 303-6)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_SYNC_THREAD ${CANOPENNODE_DIR}/zephyr/canopen_sync.c)

  # Storage (OD persistence)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_STORAGE ${CANOPENNODE_DIR}/storage/CO_storageBlank.c)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_STORAGE ${CANOPENNODE_DIR}/zephyr/canopen_storage.c)

  # Program download (CiA 302-3 draft)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_PROGRAM_DOWNLOAD ${CANOPENNODE_DIR}/zephyr/canopen_program.c)

  # ASCII gateway (CiA 309)
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_309 ${CANOPENNODE_DIR}/309/CO_gateway_ascii.c)

  # Extra utilities
  zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_EXTRA ${CANOPENNODE_DIR}/extra/CO_extra.c)

  # Zephyr-specific CAN driver glue
  zephyr_library_sources(${CANOPENNODE_DIR}/zephyr/CO_driver_blank.c)

endif()
