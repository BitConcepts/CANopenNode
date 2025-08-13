#include "CO_canopen_zephyr.h"

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

/* ---------- Kconfig-backed defaults ---------- */
#ifndef CONFIG_COZ_PERIOD_MIN_US
#define CONFIG_COZ_PERIOD_MIN_US 500
#endif
#ifndef CONFIG_COZ_PERIOD_MAX_US
#define CONFIG_COZ_PERIOD_MAX_US 5000
#endif
#ifndef CONFIG_COZ_NODE_ID
#define CONFIG_COZ_NODE_ID 1
#endif
#ifndef CONFIG_COZ_BITRATE_KBPS
#define CONFIG_COZ_BITRATE_KBPS 1000
#endif
#ifndef CONFIG_COZ_RT_THREAD_STACK_SIZE
#define CONFIG_COZ_RT_THREAD_STACK_SIZE 1024
#endif
#ifndef CONFIG_COZ_RT_THREAD_PRIORITY
#define CONFIG_COZ_RT_THREAD_PRIORITY 5
#endif
#ifndef CONFIG_COZ_RT_IDLE_MS
#define CONFIG_COZ_RT_IDLE_MS 2
#endif

/* Prefer CANopen-specific chosen; fallback to generic if present */
#if DT_HAS_CHOSEN(zephyr_co_can)
#define COZ_CAN_NODE DT_CHOSEN(zephyr_co_can)
#elif DT_HAS_CHOSEN(zephyr_canbus)
#define COZ_CAN_NODE DT_CHOSEN(zephyr_canbus)
#endif

/* ---------- Module state ---------- */

static CO_t *CO = NULL;                 /* CANopen object */
static CO_storage_t *CO_storage = NULL; /* Storage object */

static atomic_t g_running;

static struct k_work_delayable g_work;

/* RT thread wake signal (ISR-safe) */
static K_SEM_DEFINE(rt_sem, 0, UINT_MAX);

/* ---------- Helpers ---------- */
static void rt_signal_cb(void *object)
{
	ARG_UNUSED(object);

	if (!atomic_get(&g_running)) {
		return;
	}

	/* ISR-safe in Zephyr */
	k_sem_give(&rt_sem);
}

static void enable_pre_signals(CO_t *co, void (*pre_cb)(void *object), void *pre_arg)
{
	CO_SYNC_initCallbackPre(co->SYNC, pre_cb, pre_arg);

	for (uint16_t i = 0; i < CO_GET_CNT(RPDO); i++) {
		CO_RPDO_initCallbackPre(co->RPDO[i], pre_cb, pre_arg);
	}
}

/* ---------- Worker: CO_process() cadence ---------- */
static void canopen_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!atomic_get(&g_running) || g_co == NULL) {
		return;
	}

	/* elapsed time (ms->us is fine for CO_process cadence) */
	static int64_t last_ms;
	int64_t now_ms = k_uptime_get();
	uint32_t dt_us = (last_ms == 0) ? CONFIG_COZ_PERIOD_MIN_US
					: (uint32_t)MAX(now_ms - last_ms, 0) * 1000U;
	last_ms = now_ms;

#if IS_ENABLED(CONFIG_CANOPENNODE_GLOBAL_FLAG_TIMERNEXT)
	uint32_t next_us = UINT32_MAX;
	(void)CO_process(g_co, false, dt_us, &next_us);
	uint32_t delay_us = (next_us == UINT32_MAX) ? CONFIG_COZ_PERIOD_MIN_US : next_us;
#else
	(void)CO_process(g_co, false, dt_us, NULL);
	uint32_t delay_us = CONFIG_COZ_PERIOD_MIN_US;
#endif

	delay_us = CLAMP(delay_us, CONFIG_COZ_PERIOD_MIN_US, CONFIG_COZ_PERIOD_MAX_US);
	(void)k_work_reschedule(&g_work, K_USEC(delay_us));
}

/* ---------- RT thread: event-driven SYNC/RPDO/TPDO ---------- */
static void canopen_rt_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t prev = k_cycle_get_32();

	for (;;) {
		/* Wake on PRE-callback or idle timeout to service TPDO timers */
		(void)k_sem_take(&rt_sem, K_MSEC(CONFIG_COZ_RT_IDLE_MS));

		if (!atomic_get(&g_running) || CO == NULL) {
			continue;
		}

		bool_t sync = false;
		uint32_t now = k_cycle_get_32();
		uint32_t delta = now - prev; /* wraps fine for uint32_t */
		prev = now;

		uint32_t dt_us = (uint32_t)(k_cyc_to_ns_floor64(delta) / 1000U);

		CO_LOCK_OD();
#if IS_ENABLED(CONFIG_CANOPENNODE_SYNC_ENABLE)
		sync = CO_process_SYNC(CO, dt_us);
#endif
#if IS_ENABLED(CONFIG_CANOPENNODE_RPDO_ENABLE)
		CO_process_RPDO(CO, sync);
#endif
#if IS_ENABLED(CONFIG_CANOPENNODE_TPDO_ENABLE)
		CO_process_TPDO(CO, sync, dt_us);
#endif
		CO_UNLOCK_OD();
	}
}

/* Higher priority than system workqueue to minimize SYNC latency */
K_THREAD_DEFINE(canopen_rt, CONFIG_COZ_RT_THREAD_STACK_SIZE, canopen_rt_thread, NULL, NULL, NULL,
		CONFIG_COZ_RT_THREAD_PRIORITY, 0, 0);

/* ---------- Public API ---------- */
int co_canopen_start(const struct device *can_dev, uint8_t node_id, uint16_t bitrate_kbps)
{
	if (atomic_get(&g_running)) {
		return -EALREADY;
	}
	if (!can_dev || !device_is_ready(can_dev)) {
		return -ENODEV;
	}
	if (node_id == 0 || node_id > 127) {
		return -EINVAL;
	}

	if (CO != NULL) {
		CO_delete(CO);
		CO = NULL;
	}

	uint32_t heapMemoryUsed;
	CO = CO_new(NULL, &heapMemoryUsed);
	if (CO == NULL) {
		LOG_ERR("Can't allocate memory for CANopen objects");
		return -ENOMEM;
	} else {
		LOG_INF("Allocated %u bytes for CANopen objects\n", heapMemoryUsed);
	}

	CO_ReturnError_t err = 0;
	int32_t ret = 0;

/* Initialize storage module */
#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
	CO_storage_entry_t storageEntries[] = {{.addr = &OD_PERSIST_COMM,
						.len = sizeof(OD_PERSIST_COMM),
						.subIndexOD = 2,
						.attr = CO_storage_cmd | CO_storage_restore,
						.addrNV = NULL}};
	uint8_t storageEntriesCount = sizeof(storageEntries) / sizeof(storageEntries[0]);
	uint32_t storageInitError = 0;

	err = CO_storage_zephyr_init(CO_storage, CO->CANmodule, OD_ENTRY_H1010_storeParameters,
				     OD_ENTRY_H1011_restoreDefaultParameters, storageEntries,
				     storageEntriesCount, &storageInitError);
	if (err != CO_ERROR_NO) {
		LOG_ERR("Storage module initialization failed: %d", err);
		ret = -ENOMEM;
		goto error;
	}
	if (storageInitError != 0) {
		LOG_ERR("Storage module initialization error: 0x%X", storageInitError);
		ret = -EIO;
		goto error;
	}
#endif
	/* Initialzie CANopen */
	err = CO_CANinit(CO, can_dev, bitrate_kbps);
	if (err != CO_ERROR_NO) {
		LOG_ERR("CAN initialization failed: %d", err);
		ret = -EINVAL;
		goto error;
	}

/* Initialzie LSS */
#if IS_ENABLED(CONFIG_CANOPENNODE_LSS_SLAVE)
	CO_LSS_address_t lssAddress = {
		.identity = {.vendorID = OD_PERSIST_COMM.x1018_identity.vendor_ID,
			     .productCode = OD_PERSIST_COMM.x1018_identity.productCode,
			     .revisionNumber = OD_PERSIST_COMM.x1018_identity.revisionNumber,
			     .serialNumber = OD_PERSIST_COMM.x1018_identity.serialNumber}};
	err = CO_LSSinit(CO, &lssAddress, &node_id, &bitrate_kbps);
	if (err != CO_ERROR_NO) {
		LOG_ERR("LSS slave initialization failed: %d", err);
		ret = -EINVAL;
		goto error;
	}
#endif
	uint32_t errInfo = 0;

	err = CO_CANopenInit(CO,                                       /* CANopen object */
			     NULL,                                     /* alternate NMT */
			     NULL,                                     /* alternate em */
			     OD,                                       /* Object dictionary */
			     NULL,                                     /* Optional OD_statusBits */
			     CO_CONFIG_NMT,                    /* CO_NMT_control_t */
			     CO_NMT_FIRST_HB_TIME_MS,  /* firstHBTime_ms */
			     CO_CONFIG_SDO_SRV_TIMEOUT_MS, /* SDOserverTimeoutTime_ms */
			     CO_CONFIG_SDO_CLI_TIMEOUT_MS, /* SDOclientTimeoutTime_ms */
			     CO_CONFIG_SDO_CLI_BLOCK,      /* SDOclientBlockTransfer */
			     node_id, &errInfo);
	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		if (err == CO_ERROR_OD_PARAMETERS) {
			LOG_ERR("Object Dictionary entry 0x%X", errInfo);
			ret = -EINVAL;
		} else {
			LOG_ERR("CANopen initialization failed: %d", err);
			ret = -EIO
		}
		goto error;
	}

	err = CO_CANopenInitPDO(CO, CO->em, OD, node_id, &errInfo);
	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		if (err == CO_ERROR_OD_PARAMETERS) {
			LOG_ERR("Object Dictionary entry 0x%X", errInfo);
			ret = -EINVAL;
		} else {
			LOG_ERR("PDO initialization failed: %d", err);
			ret = -EIO;
		}
		goto error;
	}

	if (!CO->nodeIdUnconfigured) {
#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
		if (storageInitError != 0) {
			CO_errorReport(CO->em, CO_EM_NON_VOLATILE_MEMORY, CO_EMC_HARDWARE,
				       storageInitError);
		}
#endif
	} else {
		LOG_INF("CANopenNode - Node-id not initialized");
	}

	/* Enable PRE-callbacks so RT thread wakes on SYNC/RPDO */
	enable_pre_signals(CO, rt_signal_cb, NULL);

	k_work_init_delayable(&g_work, canopen_work_handler);
	atomic_set(&g_running, 1);
	(void)k_work_schedule(&g_work, K_NO_WAIT);

	CO_CANsetNormalMode(CO->CANmodule);
	LOG_INF("CANopenNode - Running...");

	return 0;

error:
	if (CO != NULL) {
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
	atomic_set(&g_running, 0);

	(void)k_work_cancel_delayable(&g_work);

	CO_CANmodule_disable(CO->CANmodule);
	CO_delete(CO);
	CO = NULL;

	LOG_ING("CANopenNode - Stopped");
}

bool co_canopen_is_running(void)
{
	return atomic_get(&g_running);
}
/* Resolve CAN device (DT chosen or Kconfig fallback) and start */
int co_canopen_start_auto(void)
{
	const struct device *can_dev = NULL;

#ifdef COZ_CAN_NODE
	can_dev = DEVICE_DT_GET(COZ_CAN_NODE);
	if (!device_is_ready(can_dev)) {
		return -ENODEV;
	}
#elif defined(CONFIG_COZ_CAN_DEV_NAME)
	if (CONFIG_COZ_CAN_DEV_NAME[0] != '\0') {
		can_dev = device_get_binding(CONFIG_COZ_CAN_DEV_NAME);
		if (!can_dev || !device_is_ready(can_dev)) {
			return -ENODEV;
		}
	} else {
		return -ENODEV;
	}
#else
	return -ENODEV;
#endif

	return co_canopen_start(can_dev, CONFIG_COZ_NODE_ID, CONFIG_COZ_BITRATE_KBPS);
}

/* Auto-start at POST_KERNEL if enabled */
static int co_canopen_init_sys(const struct device *unused)
{
	ARG_UNUSED(unused);

#if IS_ENABLED(CONFIG_COZ_AUTO_START)
	(void)co_canopen_start_auto();
#endif
	return 0;
}
SYS_INIT(co_canopen_init_sys, POST_KERNEL, CONFIG_COZ_SYSINIT_PRIO);
