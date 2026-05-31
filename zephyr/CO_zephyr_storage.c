/* SPDX-License-Identifier: Apache-2.0 */
/*
 * CANopen Object Dictionary storage for Zephyr backends.
 *
 * Zephyr-backed implementation of the CANopenNode storage object that
 * provides persistent parameter handling (load / store / restore) suitable
 * for production systems. Integrates with Zephyr subsystems (e.g. Settings
 * or NVS/flash), as selected by the application.
 *
 * @file        CO_zephyr_storage.c
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

#include "CO_zephyr_storage.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(canopen_storage, CONFIG_CANOPEN_LOG_LEVEL);

/* BUG FIX (b798758, 2025-05-31):
 * All backend guards originally used the wrong prefix:
 *   CONFIG_CANOPEN_STORAGE_BACKEND_*   (wrong — never defined by Kconfig)
 * instead of:
 *   CONFIG_CANOPENNODE_STORAGE_BACKEND_* (correct — defined in zephyr/Kconfig)
 *
 * Impact: every #ifdef / #if defined() guard below evaluated to FALSE, so
 * the Settings load/save and RAM paths were NEVER compiled in. The module
 * appeared to work (no compile error) but silently no-oped every store,
 * restore, and load operation, effectively disabling persistence entirely.
 *
 * Root cause: naming inconsistency when the storage .c was initially written
 * against a draft Kconfig that used a shorter prefix. The Kconfig was later
 * updated to the full CANOPENNODE_ prefix but the .c was not.
 *
 * Upstream relevance: this file is BitConcepts-only (not in upstream CANopenNode);
 * no upstream PR needed. Tracked in doc/BUGFIXES.md as BUG-003.
 */
#ifdef CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS
#include <zephyr/settings/settings.h>
#endif

/* Store callback for OD 0x1010 "Store Parameters".
 * Persists one configured storage entry using the selected backend.
 * Returns ODR_OK on success, ODR_HW on backend failure.
 */
static ODR_t z_store(CO_storage_entry_t *entry, CO_CANmodule_t *CANmodule)
{
#if defined(CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS)
	char key[64];
	snprintf(key, sizeof(key), "canopen/od/%04X", entry->subIndexOD);
	int err = settings_save_one(key, entry->addr, entry->len);
	if (err) {
		LOG_ERR("Settings save failed (%d) for key %s", err, key);
		return ODR_HW;
	}
#elif defined(CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM)
	LOG_DBG("Skipping store (RAM-only backend)");
#else
	LOG_WRN("No valid storage backend selected — store operation skipped");
#endif
	return ODR_OK;
}

/* Restore callback for OD 0x1011 "Restore Default Parameters".
 * Clears any persisted value for the entry (or no-ops for RAM),
 * so defaults take effect after the next load.
 */
static ODR_t z_restore(CO_storage_entry_t *entry, CO_CANmodule_t *CANmodule)
{
#if defined(CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS)
	char key[64];
	snprintf(key, sizeof(key), "canopen/od/%04X", entry->subIndexOD);
	int err = settings_delete(key);
	if (err) {
		LOG_WRN("Settings delete failed (%d) for key %s", err, key);
	}
#elif defined(CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM)
	LOG_DBG("Skipping restore (RAM-only backend)");
#else
	LOG_WRN("No valid storage backend selected — restore operation skipped");
#endif
	return ODR_OK;
}

/* Initialize CO_storage with Zephyr-backed store/restore hooks and
 * optionally load initial values from the selected backend.
 */
CO_ReturnError_t co_zephyr_storage_init(CO_storage_t *storage, CO_CANmodule_t *CANmodule,
					OD_entry_t *OD_1010_StoreParameters,
					OD_entry_t *OD_1011_RestoreDefaultParam,
					CO_storage_entry_t *entries, uint8_t entriesCount,
					uint32_t *storageInitError)
{
	if (storage == NULL || entries == NULL || entriesCount == 0 || storageInitError == NULL) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	CO_ReturnError_t ret = CO_storage_init(storage, CANmodule, OD_1010_StoreParameters,
					       OD_1011_RestoreDefaultParam, z_store, z_restore,
					       entries, entriesCount);
	if (ret != CO_ERROR_NO) {
		return ret;
	}

	/* Mark "no index error" unless we detect a bad entry below. */
	*storageInitError = 0;

	/* Optionally preload values from the backend into RAM. */
	for (uint8_t i = 0; i < entriesCount; i++) {
		CO_storage_entry_t *entry = &entries[i];

		/* Basic entry validation: require addr/len and subindex >= 2. */
		if (entry->addr == NULL || entry->len == 0 || entry->subIndexOD < 2) {
			*storageInitError = i;
			return CO_ERROR_ILLEGAL_ARGUMENT;
		}

#if defined(CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS)
		char key[64];
		snprintf(key, sizeof(key), "canopen/od/%04X", entry->subIndexOD);
		int rc = settings_load_subtree_direct(key, entry->addr, entry->len);
		if (rc < 0) {
			LOG_DBG("No settings found for %s", key);
		}
#elif defined(CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM)
		/* RAM-only backend: nothing to load; defaults already in place. */
#else
		LOG_WRN("No valid storage backend selected — skipping restore for 0x%02X",
			entry->subIndexOD);
#endif
	}
	return ret;
}
