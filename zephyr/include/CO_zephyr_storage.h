/* SPDX-License-Identifier: Apache-2.0 */
/*
 * CANopen Object Dictionary storage for Zephyr backends.
 *
 * Zephyr-backed implementation of the CANopenNode storage object that
 * provides persistent parameter handling (load / store / restore) suitable
 * for production systems. Integrates with Zephyr subsystems (e.g. Settings
 * or NVS/flash), as selected by the application.
 *
 * @file        CO_zephyr_storage.h
 * @author      Janez Paternoster (original template)
 * @author      BitConcepts, LLC <https://github.com/BitConcepts>
 * @copyright   2021 Janez Paternoster
 * @copyright   2025 BitConcepts, LLC
 *
 * This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_STORAGE_H
#define ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_STORAGE_H

#include "storage/CO_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup co_zephyr_storage CANopenNode ↔ Zephyr storage bridge
 * @brief Persistence glue for OD parameters using Zephyr backends.
 *
 * This interface wires CANopenNode's storage object (@ref CO_storage_t) to a Zephyr
 * persistence backend chosen via Kconfig (e.g., Settings, RAM). It enables:
 *
 * - Loading parameters at boot/reset (OD 0x1011 semantics).
 * - Storing parameters on request (OD 0x1010 semantics).
 * - Optional restore-to-default handling.
 *
 * ### Kconfig overview
 * Select one of the integration backends at build time (symbol names may vary
 * per project):
 * - `CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS` — Zephyr Settings API
 * - `CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM` — RAM-backed mock (volatile)
 * - `CONFIG_CANOPENNODE_STORAGE_BACKEND_NONE` — disabled/no-op
 *
 * Also enable the storage module:
 * - `CONFIG_CANOPENNODE_STORAGE_ENABLE`
 *
 * ### Typical usage
 * @code{.c}
 * #include "CO_zephyr_storage.h"
 * // ...
 * CO_storage_t storage;
 * uint32_t storage_err = 0;
 *
 * CO_ReturnError_t ret = co_zephyr_storage_init(
 *     &storage,
 *     CO->CANmodule,
 *     OD_ENTRY_H1010_storeParameters,
 *     OD_ENTRY_H1011_restoreDefaultParameters,
 *     storageEntries, entryCount,
 *     &storage_err
 * );
 *
 * if (ret != CO_ERROR_NO) {
 *     // handle init error
 * }
 * if (storage_err != 0) {
 *     // optional: report backend-specific error code
 * }
 * @endcode
 *
 * @note This header only declares the initialization function. Subsequent
 *       load/store operations are driven by the stack via OD 0x1010/0x1011
 *       access (e.g., from SDO writes, gateway commands, or application code).
 *
 * @{
 */

/**
 * @brief Initialize CANopen storage using a Zephyr-selected backend.
 *
 * Binds a @ref CO_storage_t instance to the configured Zephyr persistence
 * provider and connects it to the Object Dictionary entries for:
 * - OD 0x1010: Store Parameters
 * - OD 0x1011: Restore Default Parameters
 *
 * Depending on the selected backend, this may create or open storage areas,
 * validate metadata, and perform a one-time load of persisted values into RAM.
 *
 * @param[out] storage                     Pointer to a @ref CO_storage_t object to
 * initialize.
 * @param[in]  CANmodule                   Active @ref CO_CANmodule_t used for
 * timing/logging.
 * @param[in]  OD_1010_StoreParameters     OD entry pointer for 0x1010 (Store).
 * @param[in]  OD_1011_RestoreDefaultParam OD entry pointer for 0x1011 (Restore Defaults).
 * @param[in]  entries                     Array of @ref CO_storage_entry_t mappings to
 * persist.
 * @param[in]  entriesCount                Number of elements in @p entries.
 * @param[out] storageInitError            Optional backend-specific error code (0 on
 * success).
 *
 * @retval CO_ERROR_NO          Success.
 * @retval CO_ERROR_ILLEGAL_ARGUMENT
 *                              Any required pointer is NULL or arguments are inconsistent.
 * @retval CO_ERROR_OUT_OF_MEMORY
 *                              Allocation failed in the selected backend.
 * @retval CO_ERROR_DATA_CORRUPT
 *                              Stored data failed integrity/format checks
 * (backend-specific).
 * @retval CO_ERROR_INVALID_STATE
 *                              Backend not available or not initialized correctly.
 *
 * @note When `storageInitError` is non-NULL, it will contain an implementation-
 *       specific error value that can be logged or surfaced via EMCY/status bits.
 *       A value of 0 indicates no backend error.
 * @note This function does not take ownership of @p entries; they must remain
 *       valid for the lifetime of @p storage.
 */
CO_ReturnError_t co_zephyr_storage_init(CO_storage_t *storage, CO_CANmodule_t *CANmodule,
					OD_entry_t *OD_1010_StoreParameters,
					OD_entry_t *OD_1011_RestoreDefaultParam,
					CO_storage_entry_t *entries, uint8_t entriesCount,
					uint32_t *storageInitError);

/** @} */ /* end of co_zephyr_storage */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_STORAGE_H */
