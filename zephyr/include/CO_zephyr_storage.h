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
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
 */

#ifndef ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_STORAGE_H
#define ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_STORAGE_H

#include "storage/CO_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize CANopen storage using Zephyr backends (Settings, RAM).
 *
 * This implementation uses the selected Kconfig storage backend.
 *
 * @param storage                 Pointer to CO_storage object
 * @param CANmodule               CAN module used for logging and sync
 * @param OD_1010_StoreParameters Object Dictionary entry for OD 1010
 * @param OD_1011_RestoreDefaultParam Object Dictionary entry for OD 1011
 * @param entries                 Array of storage entries
 * @param entriesCount            Number of entries in the array
 * @param storageInitError        Pointer to variable to store error index (if any)
 *
 * @return CO_ReturnError_t       CO_ERROR_NO on success, otherwise error code
 */
CO_ReturnError_t co_zephyr_storage_init(CO_storage_t *storage, CO_CANmodule_t *CANmodule,
					OD_entry_t *OD_1010_StoreParameters,
					OD_entry_t *OD_1011_RestoreDefaultParam,
					CO_storage_entry_t *entries, uint8_t entriesCount,
					uint32_t *storageInitError);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_STORAGE_H */
