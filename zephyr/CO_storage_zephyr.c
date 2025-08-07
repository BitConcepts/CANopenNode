#include "CO_storage_zephyr.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(canopen_storage, CONFIG_CANOPENNODE_LOG_LEVEL);

#if (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE

#ifdef CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS
#include <zephyr/settings/settings.h>
#endif

/* Store OD entry (1010) */
static ODR_t store_zephyr(CO_storage_entry_t *entry, CO_CANmodule_t *CANmodule)
{
#if defined(CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS)

	char key[64];
	snprintf(key, sizeof(key), "canopen/od/%04X", entry->subIndexOD);
	int err = settings_save_one(key, entry->addr, entry->len);
	if (err) {
		LOG_ERR("Settings save failed (%d) for key %s", err, key);
		return ODR_HW;
	}

#elif defined(CONFIG_CANOPEN_STORAGE_BACKEND_RAM)

	LOG_DBG("Skipping store (RAM-only backend)");

#else

	LOG_WRN("No valid storage backend selected — store operation skipped");

#endif

	return ODR_OK;
}

/* Restore OD entry (1011) */
static ODR_t restore_zephyr(CO_storage_entry_t *entry, CO_CANmodule_t *CANmodule)
{
#if defined(CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS)

	char key[64];
	snprintf(key, sizeof(key), "canopen/od/%04X", entry->subIndexOD);
	int err = settings_delete(key);
	if (err) {
		LOG_WRN("Settings delete failed (%d) for key %s", err, key);
	}

#elif defined(CONFIG_CANOPEN_STORAGE_BACKEND_RAM)

	LOG_DBG("Skipping restore (RAM-only backend)");

#else

	LOG_WRN("No valid storage backend selected — restore operation skipped");

#endif

	return ODR_OK;
}

/* Initialization */
CO_ReturnError_t CO_storage_zephyr_init(CO_storage_t *storage, CO_CANmodule_t *CANmodule,
					OD_entry_t *OD_1010_StoreParameters,
					OD_entry_t *OD_1011_RestoreDefaultParam,
					CO_storage_entry_t *entries, uint8_t entriesCount,
					uint32_t *storageInitError)
{
	if (storage == NULL || entries == NULL || entriesCount == 0 || storageInitError == NULL) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	CO_ReturnError_t ret = CO_storage_init(storage, CANmodule, OD_1010_StoreParameters,
					       OD_1011_RestoreDefaultParam, store_zephyr,
					       restore_zephyr, entries, entriesCount);

	if (ret != CO_ERROR_NO) {
		return ret;
	}

	*storageInitError = 0;

	for (uint8_t i = 0; i < entriesCount; i++) {
		CO_storage_entry_t *entry = &entries[i];

		if (entry->addr == NULL || entry->len == 0 || entry->subIndexOD < 2) {
			*storageInitError = i;
			return CO_ERROR_ILLEGAL_ARGUMENT;
		}

#if defined(CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS)

		char key[64];
		snprintf(key, sizeof(key), "canopen/od/%04X", entry->subIndexOD);
		int rc = settings_load_subtree_direct(key, entry->addr, entry->len);
		if (rc < 0) {
			LOG_DBG("No settings found for %s", key);
		}

#elif defined(CONFIG_CANOPEN_STORAGE_BACKEND_RAM)

		// RAM-only, already loaded from default or boot values

#else

		LOG_WRN("No valid storage backend selected — skipping restore for 0x%02X",
			entry->subIndexOD);

#endif
	}

	return ret;
}

#endif /* (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE */
