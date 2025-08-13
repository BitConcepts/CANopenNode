/**
 * @file        CO_canopen_zephyr.c
 * @brief       Zephyr integration layer for CANopenNode
 *
 * This file integrates CANopenNode with the Zephyr RTOS. It provides:
 * - RT thread for SYNC/RPDO/TPDO via PRE-callbacks
 * - Worker-based scheduling for CO_process()
 * - Support for devicetree and Kconfig-based configuration
 * - Optional non-volatile parameter storage (CiA 302-6)
 *
 * @authors
 *   Janez Paternoster <https://github.com/CANopenNode>
 *   BitConcepts <https://github.com/BitConcepts>
 *
 * @copyright
 *   CANopenNode is licensed under the Apache License, Version 2.0.
 *   Modifications Copyright (c) 2025 BitConcepts, LLC.
 */

#include "CO_zephyr_integration.h"

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "OD.h"

#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
#include "CO_storage_zephyr.h"
#endif

LOG_MODULE_REGISTER(canopennode, CONFIG_CANOPEN_LOG_LEVEL);

#define CAN_NODE         DT_CHOSEN(zephyr_canbus)
#define CAN_BITRATE_KBPS (DT_PROP(CAN_NODE, bitrate) / 1000U)

/* ---------- Module state ---------- */

static CO_t *CO = NULL;
static CO_storage_t *CO_storage = NULL;
static atomic_t g_running;

K_SEM_DEFINE(rt_sem, 0, UINT_MAX); /* RT thread wake signal */

/* ---------- Helpers ---------- */

static void rt_signal_cb(void *object)
{
	ARG_UNUSED(object);
	if (atomic_get(&g_running)) {
		k_sem_give(&rt_sem);
	}
}

static void enable_pre_signals(CO_t *co, void (*pre_cb)(void *), void *arg)
{
	CO_SYNC_initCallbackPre(co->SYNC, pre_cb, arg);
	for (uint16_t i = 0; i < CO_GET_CNT(RPDO); i++) {
		CO_RPDO_initCallbackPre(co->RPDO[i], pre_cb, arg);
	}
}

/* ---------- RT Thread: SYNC/RPDO/TPDO ---------- */
#if IS_ENABLED(CANOPENNODE_RT_THREAD)
static void canopen_rt_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t last_ms = 0;
	uint32_t prev_cyc = k_cycle_get_32();
	const uint32_t fallback_us = 1000U;
	uint32_t timeout_us = fallback_us;

	while (true) {
		k_sem_take(&rt_sem, K_USEC(timeout_us));

		if (!atomic_get(&g_running) || CO == NULL) {
			continue;
		}

		/* Mainline: CO_process() */
		int64_t now_ms = k_uptime_get();
		uint32_t dt_us =
			(last_ms == 0) ? fallback_us : (uint32_t)MAX(now_ms - last_ms, 0) * 1000U;
		last_ms = now_ms;

#if IS_ENABLED(CONFIG_CANOPENNODE_RT_THREAD_TIMERNEXT)
		uint32_t next_us = UINT32_MAX;
		(void)CO_process(CO, false, dt_us, &next_us);
		timeout_us = (next_us == 0U || next_us == UINT32_MAX) ? fallback_us : next_us;
#else
		(void)CO_process(CO, false, dt_us, NULL);
		timeout_us = fallback_us;
#endif

		/* RT part: SYNC, RPDO, TPDO */
		uint32_t now_cyc = k_cycle_get_32();
		uint32_t delta_cyc = now_cyc - prev_cyc;
		prev_cyc = now_cyc;

		uint32_t dt_rt_us = (uint32_t)(k_cyc_to_ns_floor64(delta_cyc) / 1000U);

		CO_LOCK_OD();
#if IS_ENABLED(CONFIG_CANOPENNODE_SYNC_ENABLE)
		bool_t sync = CO_process_SYNC(CO, dt_rt_us);
#else
		bool_t sync = false;
#endif
#if IS_ENABLED(CONFIG_CANOPENNODE_RPDO_ENABLE)
		CO_process_RPDO(CO, sync);
#endif
#if IS_ENABLED(CONFIG_CANOPENNODE_TPDO_ENABLE)
		CO_process_TPDO(CO, sync, dt_rt_us);
#endif
		CO_UNLOCK_OD();
	}
}

K_THREAD_DEFINE(canopen_rt, CONFIG_CANOPENNODE_RT_THREAD_STACK_SIZE, canopen_rt_thread, NULL, NULL,
		NULL, CONFIG_CANOPENNODE_RT_THREAD_PRIORITY, 0, 0);

#endif /* IS_ENABLED(CANOPENNODE_RT_THREAD) */

/* ---------- Public API ---------- */

int co_canopen_start(const struct device *can_dev, uint8_t node_id, uint16_t bitrate_kbps)
{
	if (atomic_get(&g_running)) {
		return -EALREADY;
	}
	if (!can_dev) {
		can_dev = DEVICE_DT_GET(CAN_NODE);
	}
	if (!can_dev || !device_is_ready(can_dev)) {
		return -ENODEV;
	}
	if (node_id == 0 || node_id > 127) {
		return -EINVAL;
	}
	if (bitrate_kbps == 0) {
		bitrate_kbps = CONFIG_CANOPENNODE_BITRATE_KBPS;
	}

	if (CO != NULL) {
		CO_delete(CO);
		CO = NULL;
	}

	uint32_t heap_used = 0;
	CO = CO_new(NULL, &heap_used);
	if (CO == NULL) {
		LOG_ERR("[%s] Memory allocation failed", __func__);
		return -ENOMEM;
	}
	LOG_INF("[%s] Allocated %u bytes for CANopen", __func__, heap_used);

	int ret = 0;
	CO_ReturnError_t err;
	uint32_t errInfo = 0;

#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
	CO_storage_entry_t storageEntries[] = {{.addr = &OD_PERSIST_COMM,
						.len = sizeof(OD_PERSIST_COMM),
						.subIndexOD = 2,
						.attr = CO_storage_cmd | CO_storage_restore,
						.addrNV = NULL}};
	uint8_t entryCount = ARRAY_SIZE(storageEntries);
	uint32_t storageErr = 0;

	err = CO_zephyr_storage_init(CO_storage, CO->CANmodule, OD_ENTRY_H1010_storeParameters,
				     OD_ENTRY_H1011_restoreDefaultParameters, storageEntries,
				     entryCount, &storageErr);

	if (err != CO_ERROR_NO) {
		LOG_ERR("[%s] Storage init failed: %d", __func__, err);
		ret = -ENOMEM;
		goto error;
	}
	if (storageErr != 0) {
		LOG_ERR("[%s] Storage error: 0x%X", __func__, storageErr);
		ret = -EIO;
		goto error;
	}
#endif

	err = CO_CANinit(CO, can_dev, bitrate_kbps);
	if (err != CO_ERROR_NO) {
		LOG_ERR("[%s] CAN init failed: %d", __func__, err);
		ret = -EINVAL;
		goto error;
	}

#if IS_ENABLED(CONFIG_CANOPENNODE_LSS_SLAVE)
	CO_LSS_address_t lssAddr = {
		.identity = {.vendorID = OD_PERSIST_COMM.x1018_identity.vendor_ID,
			     .productCode = OD_PERSIST_COMM.x1018_identity.productCode,
			     .revisionNumber = OD_PERSIST_COMM.x1018_identity.revisionNumber,
			     .serialNumber = OD_PERSIST_COMM.x1018_identity.serialNumber}};
	err = CO_LSSinit(CO, &lssAddr, &node_id, &bitrate_kbps);
	if (err != CO_ERROR_NO) {
		LOG_ERR("[%s] LSS init failed: %d", __func__, err);
		ret = -EINVAL;
		goto error;
	}
#endif

	err = CO_CANopenInit(CO, NULL, NULL, OD, NULL, CO_CONFIG_NMT_CONTROL,
			     CO_NMT_FIRST_HB_TIME_MS, CO_CONFIG_SDO_SRV_TIMEOUT_MS,
			     CO_CONFIG_SDO_CLI_TIMEOUT_MS, CO_CONFIG_SDO_CLI_BLOCK, node_id,
			     &errInfo);

	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		LOG_ERR("[%s] CANopen init failed: %d (OD entry 0x%X)", __func__, err, errInfo);
		ret = -EIO;
		goto error;
	}

	err = CO_CANopenInitPDO(CO, CO->em, OD, node_id, &errInfo);
	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		LOG_ERR("[%s] PDO init failed: %d (OD entry 0x%X)", __func__, err, errInfo);
		ret = -EIO;
		goto error;
	}

	if (!CO->nodeIdUnconfigured) {
#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
		if (storageErr != 0) {
			CO_errorReport(CO->em, CO_EM_NON_VOLATILE_MEMORY, CO_EMC_HARDWARE,
				       storageErr);
		}
#endif
	} else {
		LOG_INF("[%s] Node-ID not configured (LSS active)", __func__);
	}

	enable_pre_signals(CO, rt_signal_cb, NULL);
	atomic_set(&g_running, 1);
	CO_CANsetNormalMode(CO->CANmodule);

	LOG_INF("[%s] CANopenNode running", __func__);
	return 0;

error:
	if (CO) {
		CO_delete(CO);
		CO = NULL;
	}
	return ret;
}

void co_canopen_stop(void)
{
	if (!atomic_get(&g_running)) {
		return;
	}

	atomic_clear(&g_running);

	if (CO != NULL) {
		CO_CANmodule_disable(CO->CANmodule);
		CO_delete(CO);
		CO = NULL;
		LOG_INF("[%s] CANopenNode stopped", __func__);
	}
}

bool co_canopen_is_running(void)
{
	return atomic_get(&g_running);
}

static int co_canopen_init_sys(const struct device *unused)
{
	ARG_UNUSED(unused);
#if IS_ENABLED(CONFIG_CANOPENNODE_RT_THREAD_AUTO_START)
	(void)co_canopen_start(NULL, CONFIG_CANOPENNODE_INIT_NODE_ID, CAN_BITRATE_KBPS);
#endif
	return 0;
}
SYS_INIT(co_canopen_init_sys, POST_KERNEL, CONFIG_CANOPENNODE_INIT_PRIORITY);
