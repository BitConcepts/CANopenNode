/**
 * CANopen Program Download (CiA 302-3 oriented, streaming + storage-friendly)
 *
 * @file        CO_Prog_Download.h
 * @ingroup     CO_Prog_Download
 * @author      BitConcepts
 * @copyright   2025 BitConcepts
 *
 * This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.
 */

#ifndef CO_PROG_DOWNLOAD_H
#define CO_PROG_DOWNLOAD_H

#include "CANopen.h"
#include "301/CO_driver.h"
#include "301/CO_ODinterface.h"

#if ((CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE) != 0
#include "storage/CO_storage.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* default configuration, see CO_config.h */
#ifndef CO_CONFIG_PROG_DOWNLOAD
#define CO_CONFIG_PROG_DOWNLOAD (0)
#endif

#if (((CO_CONFIG_PROG_DOWNLOAD) & CO_CONFIG_PROG_DOWNLOAD_ENABLE) != 0) || defined CO_DOXYGEN

/**
 * @defgroup CO_Prog_Download Program Download
 * Program Download support per CiA 302-3 using CANopen SDO (DOMAIN) transfer.
 *
 * @ingroup CO_CANopen_302
 * @{
 *
 * Bytes arrive over SDO into 0x1F50 and are forwarded to a streaming backend
 * (e.g., flash writer, bootloader) via `begin`/`write`/`commit`. This module is
 * storage-agnostic and *optionally* binds to CO_storage to persist metadata/EDS.
 *
 * ### OD entries used by this module
 * - 0x1F21: Store format (U32, RO)
 * - 0x1F23: Store EDS NMT slave (DOMAIN)
 * - 0x1F24: Store format EDS NMT slave (U32, RO)
 * - 0x1F50: Program data (DOMAIN)
 * - 0x1F51: Program control (U32)
 * - 0x1F56: Program software identification (VISIBLE_STRING)
 * - 0x1F57: Flash status identification (U32, RO)
 *
 * ### Control values for 0x1F51
 * - 0x00000000: ABORT
 * - 0x00000001: BEGIN
 * - 0x00000002: COMMIT
 * - 0x0000B007: JUMP (optional jump to bootloader)
 */

/** Streaming backend operations supplied by the application/port. */
typedef struct {
    /** Called on BEGIN. May prepare erase or open a stream. */
    bool_t (*begin)(uint32_t image_size_hint);
    /** Called for each chunk written to 0x1F50. Must be safe for repeated calls. */
    bool_t (*write)(const uint8_t* data, uint32_t len);
    /** Called on COMMIT to finalize/verify and make image ready. */
    bool_t (*commit)(void);
    /** Called on ABORT to cleanup (optional). */
    void (*abort)(void);
    /** Optional: jump to bootloader after successful programming. */
    void (*jumpToBootloader)(void);
} CO_ProgDL_StreamOps_t;

/** Status bits for 0x1F57. */
typedef enum {
    CO_PROGDL_STATUS_IDLE = 0u,
    CO_PROGDL_STATUS_RECEIVING = 1u << 0,
    CO_PROGDL_STATUS_ERASING = 1u << 1,
    CO_PROGDL_STATUS_WRITING = 1u << 2,
    CO_PROGDL_STATUS_VERIFYING = 1u << 3,
    CO_PROGDL_STATUS_DONE_OK = 1u << 4,
    CO_PROGDL_STATUS_ERROR = 1u << 5
} CO_ProgDL_StatusBit_t;

/** Control values for 0x1F51. */
typedef enum {
    CO_PROGDL_CTRL_ABORT = 0x00000000u,
    CO_PROGDL_CTRL_BEGIN = 0x00000001u,
    CO_PROGDL_CTRL_COMMIT = 0x00000002u,
    CO_PROGDL_CTRL_JUMP_BOOT = 0x0000B007u
} CO_ProgDL_Control_t;

/** Informational store format constants returned in 0x1F21/0x1F24. */
typedef enum { CO_PROGDL_STORE_FMT_PROG = 0x00000001u, CO_PROGDL_STORE_FMT_EDS = 0x00000001u } CO_ProgDL_StoreFmt_t;

/** Program Download object */
typedef struct {
    /* Core */
    CO_t* co; /**< From CO_Prog_Download_init() */

/* Optional: CANopenNode Storage binding (0x1010/0x1011) */
#if ((CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE) != 0
    CO_storage_t* storage;        /**< From CO_Prog_Download_bindStorage(), optional */
    CO_storage_entry_t* fwEntry;  /**< Storage entry for firmware image metadata, optional */
    CO_storage_entry_t* edsEntry; /**< Storage entry for EDS, optional */
#endif

    /* Streaming backend (port-supplied) */
    CO_ProgDL_StreamOps_t ops;

    /* State */
    uint32_t status;        /**< Status bitfield for 0x1F57 */
    bool_t sessionOpen;     /**< True after BEGIN until COMMIT/ABORT */
    uint32_t bytesReceived; /**< Bytes written to 0x1F50 in current session */
    uint32_t imageSizeHint; /**< Optional size hint (0 if unknown) */
    uint16_t runningCRC16;  /**< Running CRC16 (CRC16-CCITT) */

    /* 0x1F56: software identification */
    char swId[64];

    /* EDS buffering (0x1F23); can be replaced by app-specific persistence */
    uint8_t* edsBuf;
    uint32_t edsLen;

    /* OD extensions */
    OD_extension_t ext_1F21, ext_1F23, ext_1F24, ext_1F50, ext_1F51, ext_1F56, ext_1F57;
} CO_ProgDL_t;

/**
 * Initialize Program Download object.
 *
 * Function must be called in the communication reset section, after SDO/OD are ready.
 *
 * @param pdl This object will be initialized.
 * @param co  CANopen root object.
 *
 * @return #CO_ReturnError_t CO_ERROR_NO on success.
 */
CO_ReturnError_t CO_Prog_Download_init(CO_ProgDL_t* pdl, CO_t* co);

/**
 * Deinitialize Program Download object.
 *
 * @param pdl This object.
 */
void CO_Prog_Download_deinit(CO_ProgDL_t* pdl);

/**
 * Register streaming backend operations (required).
 *
 * @param pdl Program Download object.
 * @param ops Streaming operations; copied by value.
 *
 * @return 0 on success, -1 on invalid args.
 */
int CO_Prog_Download_registerStreamOps(CO_ProgDL_t* pdl, const CO_ProgDL_StreamOps_t* ops);

/**
 * Optionally set an image size hint (forwarded to ops.begin()).
 *
 * @param pdl       Program Download object.
 * @param size_hint Expected total image size in bytes (0 if unknown).
 */
void CO_Prog_Download_setImageSizeHint(CO_ProgDL_t* pdl, uint32_t size_hint);

#if ((CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE) != 0
/**
 * Optionally bind to CANopenNode Storage API.
 *
 * If bound, this module will call `storage->store(fwEntry, ...)` after a successful
 * COMMIT. Application may also persist EDS after transfer using
 * CO_Prog_Download_persistEDS().
 *
 * @param pdl      Program Download object.
 * @param storage  CO_storage instance.
 * @param fwEntry  Storage entry describing the firmware image metadata region.
 * @param edsEntry Storage entry describing EDS region.
 */
#if ((CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE) != 0
void CO_Prog_Download_bindStorage(CO_ProgDL_t* pdl, CO_storage_t* storage, CO_storage_entry_t* fwEntry,
                                  CO_storage_entry_t* edsEntry);
#endif
/**
 * Persist EDS (0x1F23) via CO_storage after transfer completes.
 *
 * This is optional and only effective if CO_storage is bound and `edsEntry` provided.
 *
 * @param pdl Program Download object.
 * @return ODR_OK on success or an appropriate ODR_* error.
 */
ODR_t CO_Prog_Download_persistEDS(CO_ProgDL_t* pdl);
#endif /* (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE */

/** @} */ /* CO_Prog_Download */

#endif /* (CO_CONFIG_PROG_DOWNLOAD) & CO_CONFIG_PROG_DOWNLOAD_ENABLE */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CO_PROG_DOWNLOAD_H */
