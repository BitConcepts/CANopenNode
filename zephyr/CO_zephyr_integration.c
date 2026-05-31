/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-to-CANopenNode integration runtime.
 *
 * Provides a runtime bridge to start/stop the CANopen stack with a selected
 * Zephyr CAN device, Node-ID, and bitrate, enabling control from code in
 * addition to prj.conf and devicetree.
 *
 * @file        CO_zephyr_integration.c
 * @author      BitConcepts, LLC <https://github.com/BitConcepts>
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

#include "CO_zephyr_integration.h"

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/sys/atomic.h> /* atomic_t, atomic_get/set/clear — used by g_running */
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "OD.h"

#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
#include "CO_zephyr_storage.h"
#endif

#if IS_ENABLED(CONFIG_CANOPENNODE_PROG_DOWNLOAD)
#include "CO_zephyr_prog_download.h"
#endif

LOG_MODULE_REGISTER(canopen_zephyr, CONFIG_CANOPEN_LOG_LEVEL);

#define CAN_NODE         DT_CHOSEN(zephyr_canbus)
#define CAN_BITRATE_KBPS (DT_PROP(CAN_NODE, bitrate) / 1000U)

#define DEV_TO_CANPTR(d) ((void *)(uintptr_t)(d))
#define CANPTR_TO_DEV(p) ((const struct device *)(p))

/* ---------- Module state ----------
 * CO / CO_storage are the runtime stack handles.
 * g_running is an atomic "stack is active" flag used by the RT thread and signalers.
 * rt_sem wakes the RT thread from pre-callbacks and periodic processing.
 */
CO_t *CO = NULL;
#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
static CO_storage_t CO_storage;
#endif
atomic_t g_running;
static const struct device *g_last_can_dev = NULL;
static uint8_t g_last_node_id = 0;
static uint16_t g_last_bitrate_kbps = 0;

#if IS_ENABLED(CONFIG_CANOPENNODE_PROG_DOWNLOAD)
static CO_ProgDL_t pdl;
static CO_ProgDL_Zephyr_t zb_ctx;
#endif

K_SEM_DEFINE(rt_sem, 0, UINT_MAX); /* RT thread wake signal */

/* ---------- Helpers ---------- */

#ifdef CO_DEBUG_COMMON
void z_canopen_log(const char *msg)
{
	LOG_DBG("%s", msg);
}
#endif

static void z_canopen_nmt_state_cb(CO_NMT_internalState_t state)
{
	const char *state_str = "UNKNOWN";
	switch (state) {
	case CO_NMT_INITIALIZING:
		state_str = "INITIALIZING";
		break;
	case CO_NMT_PRE_OPERATIONAL:
		state_str = "PRE-OPERATIONAL";
		break;
	case CO_NMT_OPERATIONAL:
		state_str = "OPERATIONAL";
		break;
	case CO_NMT_STOPPED:
		state_str = "STOPPED";
		break;
	default:
		break;
	}
	LOG_INF("NMT state changed to %s", state_str);
}

/*
 * Pre-callback used by SYNC/RPDO to poke the RT thread.
 * Gives the semaphore only when the stack is marked running.
 */
static void z_rt_signal_cb(void *object)
{
	ARG_UNUSED(object);
	if (atomic_get(&g_running)) {
		k_sem_give(&rt_sem);
	}
}

/*
 * Register a common pre-callback for SYNC and all RPDOs.
 * This lets incoming traffic promptly wake the RT thread.
 */
static void z_enable_pre_signals(CO_t *co, void (*pre_cb)(void *), void *arg)
{
#if IS_ENABLED(CONFIG_CANOPENNODE_SYNC_ENABLE) && IS_ENABLED(CONFIG_CANOPENNODE_SYNC_CALLBACK)
	CO_SYNC_initCallbackPre(co->SYNC, pre_cb, arg);
#endif
	/* BUG FIX (b798758, 2025-05-31 — BUG-002):
	 * Original guard used CONFIG_CANOPENNODE_RPDO_CALLBACK which is not a
	 * Kconfig symbol. Zephyr has a single combined PDO callback symbol:
	 * CONFIG_CANOPENNODE_PDO_CALLBACK (covers both RPDO and TPDO callbacks).
	 * Impact: CO_RPDO_initCallbackPre() was never called, so incoming RPDOs
	 * did not wake the RT thread via the semaphore. This caused higher RT
	 * latency and potential missed RPDOs under load when relying on pre-signals
	 * rather than the periodic timeout fallback.
	 */
#if IS_ENABLED(CONFIG_CANOPENNODE_RPDO_ENABLE) && IS_ENABLED(CONFIG_CANOPENNODE_PDO_CALLBACK)
	for (uint16_t i = 0; i < OD_CNT_RPDO; i++) {
		CO_RPDO_initCallbackPre(&co->RPDO[i], pre_cb, arg);
	}
#endif
}

/* ---------- RT Thread: SYNC/RPDO/TPDO ---------- */
#if IS_ENABLED(CONFIG_CANOPENNODE_RT_THREAD)

__weak uint8_t canopen_get_node_id(void);

/*
 * Function used to restart CANopen stack with last known parameters.
 */
static int z_canopen_restart(void)
{
	const struct device *can_dev = g_last_can_dev ? g_last_can_dev : DEVICE_DT_GET(CAN_NODE);
	uint8_t node_id = g_last_node_id ? g_last_node_id : (uint8_t)canopen_get_node_id();
	uint16_t bitrate = g_last_bitrate_kbps ? g_last_bitrate_kbps : CAN_BITRATE_KBPS;

	if (atomic_get(&g_running)) {
		canopen_stop();
		/* small settle time for CAN driver/filters */
		k_sleep(K_MSEC(10));
	}
	return canopen_start(can_dev, node_id, bitrate);
}

/*
 * Real-time CANopen processing thread.
 *
 * Responsibilities:
 * - Waits on a semaphore or timeout.
 * - Calls CO_process() (mainline timing and housekeeping).
 * - Runs SYNC/RPDO/TPDO processing in a tight, low-latency section.
 *
 * Timing:
 * - Uses uptime (ms) for CO_process() dt.
 * - Uses CPU cycle delta for RT section dt to improve precision.
 *
 * Concurrency:
 * - OD access is protected with CO_LOCK_OD()/CO_UNLOCK_OD().
 * - Thread runs continuously but only acts when the stack is running.
 */
static void z_canopen_rt_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Name this thread for debuggability — visible in Zephyr thread list. */
	k_thread_name_set(k_current_get(), "canopen_rt");

	int64_t last_ms = 0;
	uint32_t prev_cyc = k_cycle_get_32();
	/*
	 * fallback_us: semaphore timeout when timerNext_us is UINT32_MAX (no active
	 * timer). Controlled by CONFIG_CANOPENNODE_RT_THREAD_IDLE_MS (Kconfig).
	 * Default is 10 ms; set lower for tighter latency at the cost of CPU.
	 */
	const uint32_t fallback_us = (uint32_t)CONFIG_CANOPENNODE_RT_THREAD_IDLE_MS * 1000U;
	uint32_t timeout_us = fallback_us;

	while (true) {
		k_sem_take(&rt_sem, K_USEC(timeout_us));

		if (!atomic_get(&g_running) || CO == NULL) {
			continue;
		}

		/* Mainline: CO_process() */
		CO_NMT_reset_cmd_t reset = CO_RESET_NOT;
		int64_t now_ms = k_uptime_get();
		uint32_t dt_us =
			(last_ms == 0) ? fallback_us : (uint32_t)MAX(now_ms - last_ms, 0) * 1000U;
		last_ms = now_ms;

#if IS_ENABLED(CONFIG_CANOPENNODE_RT_THREAD_TIMERNEXT)
		uint32_t next_main_us = UINT32_MAX;
		reset = CO_process(CO, false, dt_us, &next_main_us);
#else
		reset = CO_process(CO, false, dt_us, NULL);
#endif
		/* RT part: SYNC, RPDO, TPDO */
		uint32_t now_cyc = k_cycle_get_32();
		uint32_t delta_cyc = now_cyc - prev_cyc;
		prev_cyc = now_cyc;

		uint32_t dt_rt_us = (uint32_t)(k_cyc_to_ns_floor64(delta_cyc) / 1000U);

		CO_LOCK_OD(CO->CANmodule);

		uint32_t next_sync_us = UINT32_MAX;
		uint32_t next_rpdo_us = UINT32_MAX;
		uint32_t next_tpdo_us = UINT32_MAX;

#if IS_ENABLED(CONFIG_CANOPENNODE_SYNC_ENABLE)
		bool_t sync = CO_process_SYNC(CO, dt_rt_us, &next_sync_us);
#else
		bool_t sync = false;
#endif
#if IS_ENABLED(CONFIG_CANOPENNODE_RPDO_ENABLE)
		CO_process_RPDO(CO, sync, dt_rt_us, &next_rpdo_us);
#endif
#if IS_ENABLED(CONFIG_CANOPENNODE_TPDO_ENABLE)
		CO_process_TPDO(CO, sync, dt_rt_us, &next_tpdo_us);
#endif

		CO_UNLOCK_OD(CO->CANmodule);

		/* Compute next wakeup based on returned timers */
		uint32_t next_rt_us = MIN(next_sync_us, MIN(next_rpdo_us, next_tpdo_us));

#if IS_ENABLED(CONFIG_CANOPENNODE_RT_THREAD_TIMERNEXT)
		uint32_t next_all = MIN(next_rt_us, next_main_us);
		timeout_us = (next_all == 0U || next_all == UINT32_MAX) ? fallback_us : next_all;
#else
		timeout_us = fallback_us;
#endif
		if (reset == CO_RESET_NOT) {
			/* No action */
			continue;
		} else if (reset == CO_RESET_COMM) {
			/* Reset CANopen stack */
			LOG_INF("Restarting CANopen stack");
			z_canopen_restart();
		} else if (reset == CO_RESET_APP) {
			/* Stop CANopen stack and reboot device */
			LOG_INF("Rebooting device");
			canopen_stop();
			k_sleep(K_MSEC(100));
			sys_reboot(SYS_REBOOT_COLD);
		} else if (reset == CO_RESET_QUIT) {
			/* Stop CANopen stack and exit thread */
			LOG_INF("Stopping CANopen stack");
			canopen_stop();
			break;
		} else {
			LOG_WRN("Unexpected reset code: %d", reset);
		}
	}
}

/* Spawn the real-time processing thread at boot; priority/stack are Kconfig-driven. */
K_THREAD_DEFINE(canopen_rt, CONFIG_CANOPENNODE_RT_THREAD_STACK_SIZE, z_canopen_rt_thread, NULL,
		NULL, NULL, CONFIG_CANOPENNODE_RT_THREAD_PRIORITY, 0, 0);

#endif /* IS_ENABLED(CONFIG_CANOPENNODE_RT_THREAD) */

/* ---------- Public API ---------- */

int canopen_start(const struct device *can_dev, uint8_t node_id, uint16_t bitrate_kbps)
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
		bitrate_kbps = CAN_BITRATE_KBPS;
	}

	if (CO != NULL) {
		CO_delete(CO);
		CO = NULL;
	}

	uint32_t heap_used = 0;
	CO = CO_new(NULL, &heap_used);
	if (CO == NULL) {
		LOG_ERR("Memory allocation failed");
		return -ENOMEM;
	}
	LOG_INF("Allocated %u bytes for CANopen", heap_used);

	int ret = 0;
	CO_ReturnError_t err;
	uint32_t errInfo = 0;

#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
	CO_storage_entry_t storageEntries[] = {{
		.addr = &OD_PERSIST_COMM,
		.len = sizeof(OD_PERSIST_COMM),
		.subIndexOD = 2,
		.attr = CO_storage_cmd | CO_storage_restore,
	}};
	uint8_t entryCount = ARRAY_SIZE(storageEntries);
	uint32_t storageErr = 0;

	/*
	 * Use index-only OD entry shortcuts (OD_ENTRY_H1010, OD_ENTRY_H1011)
	 * rather than name-suffixed forms (OD_ENTRY_H1010_storeParameters etc.).
	 * The name suffix depends on the exact object name in the user's XDD,
	 * which differs between applications. The index-only form is always
	 * available and OD-agnostic.
	 * Reference: upstream example/main_blank.c uses OD_ENTRY_H1010_storeParameters
	 * (DS301 profile). iSMART firmware uses storeParameterField. Neither is universal.
	 */
	err = co_zephyr_storage_init(&CO_storage, CO->CANmodule, OD_ENTRY_H1010, OD_ENTRY_H1011,
				     storageEntries, entryCount, &storageErr);

	if (err != CO_ERROR_NO) {
		LOG_ERR("Storage init failed: %d", err);
		ret = -ENOMEM;
		goto error;
	}
	if (storageErr != 0) {
		LOG_ERR("Storage error: 0x%X", storageErr);
		ret = -EIO;
		goto error;
	}
#endif

	err = CO_CANinit(CO, DEV_TO_CANPTR(can_dev), bitrate_kbps);
	if (err != CO_ERROR_NO) {
		LOG_ERR("CAN init failed: %d", err);
		ret = -EINVAL;
		goto error;
	}

#if IS_ENABLED(CONFIG_CANOPENNODE_LSS_SLAVE)
	/*
	 * Identity object (0x1018) is in OD_PERSIST_COMM for the standard DS301
	 * profile (and most real CANopen ODs). Using OD_ROM would fail to compile
	 * because 0x1018 is PERSIST_COMM-group, not ROM-group.
	 * Reference: upstream example/main_blank.c uses OD_PERSIST_COMM.
	 */
	CO_LSS_address_t lssAddr = {
		.identity = {.vendorID = OD_PERSIST_COMM.x1018_identity.vendor_ID,
			     .productCode = OD_PERSIST_COMM.x1018_identity.productCode,
			     .revisionNumber = OD_PERSIST_COMM.x1018_identity.revisionNumber,
			     .serialNumber = OD_PERSIST_COMM.x1018_identity.serialNumber}};
	err = CO_LSSinit(CO, &lssAddr, &node_id, &bitrate_kbps);
	if (err != CO_ERROR_NO) {
		LOG_ERR("LSS init failed: %d", err);
		ret = -EINVAL;
		goto error;
	}
#endif

	err = CO_CANopenInit(CO, NULL, NULL, OD, NULL, CO_CONFIG_NMT_CONTROL,
			     CO_NMT_FIRST_HB_TIME_MS, CO_CONFIG_SDO_SRV_TIMEOUT_MS,
			     CO_CONFIG_SDO_CLI_TIMEOUT_MS, CO_CONFIG_SDO_CLI_BLOCK, node_id,
			     &errInfo);

	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		LOG_ERR("CANopen init failed: %d (OD entry 0x%X)", err, errInfo);
		ret = -EIO;
		goto error;
	}

	err = CO_CANopenInitPDO(CO, CO->em, OD, node_id, &errInfo);
	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		LOG_ERR("PDO init failed: %d (OD entry 0x%X)", err, errInfo);
		ret = -EIO;
		goto error;
	}

#if IS_ENABLED(CONFIG_CANOPENNODE_PROG_DOWNLOAD)
	{
		/* If your binder takes a partition ID and optional CO_storage handle: */
		err = CO_Prog_Download_zephyr_bind_default(&pdl, &zb_ctx);
		/* BUG FIX (b798758, 2025-05-31 — BUG-001):
		 * Original code checked 'ret' (always 0 at this point) instead of
		 * 'err' (the return value of the bind call). Result: a bind failure
		 * was silently ignored and canopen_start() returned 0 (success) even
		 * though Program Download was not initialised. Any subsequent CANopen
		 * download request would hit uninitialised ops pointers.
		 */
		if (err != CO_ERROR_NO) {
			LOG_ERR("Program Download bind failed: %d", err);
			ret = -EINVAL;
			goto error;
		}
	}
#endif

	if (!CO->nodeIdUnconfigured) {
#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
		if (storageErr != 0) {
			CO_errorReport(CO->em, CO_EM_NON_VOLATILE_MEMORY, CO_EMC_HARDWARE,
				       storageErr);
		}
#endif
	} else {
		LOG_INF("Node-ID not configured (LSS active)");
	}

#if IS_ENABLED(CONFIG_CANOPENNODE_NMT_CALLBACK)
	CO_NMT_initCallbackChanged(CO->NMT, z_canopen_nmt_state_cb);
#endif

	z_enable_pre_signals(CO, z_rt_signal_cb, NULL);
	atomic_set(&g_running, 1);
	CO_CANsetNormalMode(CO->CANmodule);

	/* Save previous values */
	g_last_can_dev = can_dev;
	g_last_node_id = node_id;
	g_last_bitrate_kbps = bitrate_kbps;

	LOG_INF("CANopenNode running");
	return 0;

error:
	if (CO) {
		CO_delete(CO);
		CO = NULL;
	}
	return ret;
}

void canopen_stop(void)
{
	if (!atomic_get(&g_running)) {
		return;
	}

	atomic_clear(&g_running);

	if (CO != NULL) {
		CO_CANmodule_disable(CO->CANmodule);
		CO_delete(CO);
		CO = NULL;
		LOG_INF("CANopenNode stopped");
	}
}

void canopen_error_report(uint8_t errorBit, uint16_t errorCode, uint32_t infoCode)
{
	if (CO && CO->em) {
		CO_errorReport(CO->em, errorBit, errorCode, infoCode);
	}
}

void canopen_error_reset(uint8_t errorBit, uint32_t infoCode)
{
	if (CO && CO->em) {
		CO_errorReset(CO->em, errorBit, infoCode);
	}
}

__weak uint8_t canopen_get_node_id(void)
{
	return CONFIG_CANOPENNODE_INIT_NODE_ID;
}

/*
 * System init hook.
 * Optionally auto-starts CANopen at POST_KERNEL if configured via Kconfig.
 */
static int z_co_init_sys(void)
{
#if IS_ENABLED(CONFIG_CANOPENNODE_RT_THREAD_AUTO_START)
	(void)canopen_start(NULL, canopen_get_node_id(), CAN_BITRATE_KBPS);
#endif

	return 0;
}
SYS_INIT(z_co_init_sys, POST_KERNEL, CONFIG_CANOPENNODE_INIT_PRIORITY);
