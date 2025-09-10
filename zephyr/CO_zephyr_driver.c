/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr CAN backend for CANopenNode driver (CO_driver).
 *
 * Zephyr-specific implementation that adapts CANopenNode’s generic CAN
 * driver to the Zephyr CAN API. It configures bitrate/mode, installs RX
 * filters, handles TX queueing and completion callbacks, and propagates
 * bus/error status into CO_CANmodule for the CANopen stack.
 *
 * @file        CO_zephyr_driver.c
 * @author      Janez Paternoster (original template)
 * @author      BitConcepts, LLC <https://github.com/BitConcepts>
 * @copyright   2004 - 2020 Janez Paternoster
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

#include "301/CO_driver.h"

#include <string.h>
#include <zephyr/drivers/can.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(canopen_driver, CONFIG_CANOPEN_LOG_LEVEL);

#define CANPTR_TO_DEV(ptr) ((const struct device *)(ptr))

K_KERNEL_STACK_DEFINE(canopen_tx_workq_stack, CONFIG_CANOPENNODE_TX_WORKQUEUE_STACK_SIZE);

/* Retry backoff tunables (very small; CAN is fast and we only need to yield briefly) */
#ifndef CONFIG_CANOPENNODE_TX_RETRY_INITIAL_MS
#define CONFIG_CANOPENNODE_TX_RETRY_INITIAL_MS 1
#endif
#ifndef CONFIG_CANOPENNODE_TX_RETRY_MAX_MS
#define CONFIG_CANOPENNODE_TX_RETRY_MAX_MS 64
#endif

struct k_work_q canopen_tx_workq;

struct canopen_tx_work_container {
	struct k_work_delayable dwork;
	CO_CANmodule_t *CANmodule;
	uint32_t backoff_ms; /* current backoff (0 means “unset”) */
};

struct canopen_tx_work_container canopen_tx_queue;

K_MUTEX_DEFINE(canopen_send_mutex);
K_MUTEX_DEFINE(canopen_emcy_mutex);
K_MUTEX_DEFINE(canopen_co_mutex);

inline void z_co_send_lock(void)
{
	k_mutex_lock(&canopen_send_mutex, K_FOREVER);
}

inline void z_co_send_unlock(void)
{
	k_mutex_unlock(&canopen_send_mutex);
}

inline void z_co_emcy_lock(void)
{
	k_mutex_lock(&canopen_emcy_mutex, K_FOREVER);
}

inline void z_co_emcy_unlock(void)
{
	k_mutex_unlock(&canopen_emcy_mutex);
}

inline void z_co_od_lock(void)
{
	k_mutex_lock(&canopen_co_mutex, K_FOREVER);
}

inline void z_co_od_unlock(void)
{
	k_mutex_unlock(&canopen_co_mutex);
}

/*
 * Detach all installed RX filters from the Zephyr CAN device for this module.
 *
 * Notes:
 * - Uses filter_id == -ENOSPC as a sentinel for "no filter installed".
 * - Safe to call multiple times; non-installed entries are skipped.
 */
static void z_co_detach_all_rx_filters(CO_CANmodule_t *CANmodule)
{
	uint_fast16_t i;

	if (!CANmodule || !CANmodule->CANptr || !CANmodule->rxArray) {
		return;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);

	for (i = 0U; i < CANmodule->rxSize; i++) {
		if (CANmodule->rxArray[i].filter_id != -ENOSPC) {
			can_remove_rx_filter(dev, CANmodule->rxArray[i].filter_id);
			CANmodule->rxArray[i].filter_id = -ENOSPC;
		}
	}
}

/*
 * RX callback bound to hardware filters.
 *
 * Matches incoming frames against the software buffer table (priority order),
 * performs optional RTR handling, repacks data into CO_CANrxMsg_t, and invokes
 * the registered CANopen rx callback for the first matching buffer.
 *
 * Context:
 * - Called from Zephyr CAN driver context (may be IRQ or thread depending on driver).
 * - Keep it short; heavy work is done later by the stack.
 */
static void z_co_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
	CO_CANmodule_t *CANmodule = (CO_CANmodule_t *)user_data;
	CO_CANrxMsg_t rxMsg;
	CO_CANrx_t *buffer;
	int i;

	ARG_UNUSED(dev);

	/* Loop through registered rx buffers in priority order */
	for (i = 0; i < CANmodule->rxSize; i++) {
		buffer = &CANmodule->rxArray[i];

		if (buffer->filter_id == -ENOSPC || buffer->CANrx_callback == NULL) {
			continue;
		}

		/* Masked identifier match */
		if (((frame->id ^ buffer->ident) & buffer->mask) == 0U) {
#ifdef CONFIG_CAN_ACCEPT_RTR
			/* If buffer expects RTR, ignore non-RTR frames. */
			if ((buffer->ident & 0x800) && ((frame->flags & CAN_FRAME_RTR) == 0U)) {
				continue;
			}
#endif /* CONFIG_CAN_ACCEPT_RTR */
			rxMsg.ident = frame->id;
			rxMsg.DLC = frame->dlc;
			memcpy(rxMsg.data, frame->data, frame->dlc);
			if (buffer->CANrx_callback != NULL) {
				buffer->CANrx_callback(buffer->object, &rxMsg);
			}
			break; /* first match wins */
		}
	}
}

static inline uint32_t z_co_next_backoff_ms(struct canopen_tx_work_container *c)
{
	if (c->backoff_ms == 0) {
		c->backoff_ms = CONFIG_CANOPENNODE_TX_RETRY_INITIAL_MS;
	} else if (c->backoff_ms < CONFIG_CANOPENNODE_TX_RETRY_MAX_MS) {
		uint32_t next = c->backoff_ms << 1;
		c->backoff_ms = (next > CONFIG_CANOPENNODE_TX_RETRY_MAX_MS)
					? CONFIG_CANOPENNODE_TX_RETRY_MAX_MS
					: next;
	}
	return c->backoff_ms;
}

static inline void z_co_retry_schedule(uint32_t delay_ms)
{
	k_work_schedule_for_queue(&canopen_tx_workq, &canopen_tx_queue.dwork, K_MSEC(delay_ms));
}

static inline void z_co_retry_schedule_backoff(void)
{
	if (canopen_tx_queue.backoff_ms < CONFIG_CANOPENNODE_TX_RETRY_MAX_MS) {
		z_co_retry_schedule(z_co_next_backoff_ms(&canopen_tx_queue));
	}
}

static inline void z_co_retry_schedule_now(void)
{
	canopen_tx_queue.backoff_ms = 0; /* reset on success/explicit wake */
	z_co_retry_schedule(0);
}

/*
 * TX completion callback from the Zephyr CAN driver.
 *
 * On success: marks that the first TX was sent and schedules a work item to
 * retry any queued frames. On error: logs a warning and still schedules retry
 * so buffered frames get another chance.
 */
static void z_co_tx_callback(const struct device *dev, int error, void *arg)
{
	CO_CANmodule_t *CANmodule = arg;

	ARG_UNUSED(dev);

	if (!CANmodule) {
		LOG_ERR("tx callback arg invalid");
		return;
	}

	if (error != 0) {
		LOG_WRN("tx callback error (err %d)", error);
	} else {
		CANmodule->firstCANtxMessage = false;
	}

	/* A slot just freed up—reset backoff and drain now */
	z_co_retry_schedule_now();
}

/*
 * Work-queue handler that retries sending buffered CAN frames.
 *
 * Attempts to flush the software TX ring into the controller. If the driver
 * reports -EAGAIN (mailbox busy), we stop and the next completion will resubmit
 * this work again.
 *
 * Concurrency:
 * - Protected by CO_LOCK_CAN_SEND() to synchronize with CO_CANsend().
 */
static void z_co_tx_retry(struct k_work *item)
{
	struct canopen_tx_work_container *container =
		CONTAINER_OF(item, struct canopen_tx_work_container, dwork.work);
	CO_CANmodule_t *CANmodule = container->CANmodule;
	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);
	struct can_frame frame;
	CO_CANtx_t *buffer;
	int err;
	uint_fast16_t i;

	memset(&frame, 0, sizeof(frame));

	CO_LOCK_CAN_SEND();

	for (i = 0; i < CANmodule->txSize; i++) {
		buffer = &CANmodule->txArray[i];
		if (buffer->bufferFull) {
			frame.id = buffer->ident;
			frame.dlc = buffer->DLC;
			frame.flags = buffer->flags;
			memcpy(frame.data, buffer->data, buffer->DLC);
			err = can_send(dev, &frame, K_NO_WAIT, z_co_tx_callback, CANmodule);
			if (err == 0) {
				/* accepted; reset backoff and clear buffered copy */
				canopen_tx_queue.backoff_ms = 0;
				buffer->bufferFull = false;
				continue;
			}

			switch (err) {
			case -EAGAIN:
				/* busy / no slot right now */
				__fallthrough;
			case -ENETDOWN:
				/* stopped */
				__fallthrough;
			case -ENETUNREACH:
				/* bus-off */
				__fallthrough;
			case -EBUSY:
				/* arbitration lost (no auto-retry) */
				__fallthrough;
			case -EIO:
				/* Keep buffered; back off and try again */
				z_co_retry_schedule_backoff();
				/* Stop early so we don’t spin while saturated */
				CO_UNLOCK_CAN_SEND();
				return;
			case -EINVAL:
				__fallthrough;
			case -ENOTSUP:
				/* Unrecoverable param/config: drop */
				LOG_ERR("retry send invalid/unsupported (rc %d); dropping frame",
					err);
				buffer->bufferFull = false;
				/* Continue to try other slots */
				break;
			default:
				LOG_WRN("retry send unexpected rc=%d; backoff", err);
				z_co_retry_schedule_backoff();
				CO_UNLOCK_CAN_SEND();
				return;
			}
			/* Exit the for-loop on first backpressure to yield CPU and avoid thrash */
			break;
		}
	}

	CO_UNLOCK_CAN_SEND();
}

void CO_CANsetConfigurationMode(void *CANptr)
{
	if (!CANptr) {
		return;
	}

	const struct device *dev = CANPTR_TO_DEV(CANptr);
	int err = can_stop(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("can stop failed (err %d)", err);
	}
}

void CO_CANsetNormalMode(CO_CANmodule_t *CANmodule)
{
	if (!CANmodule || !CANmodule->CANptr) {
		return;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);
	int err = can_start(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("can start failed (err %d)", err);
		return;
	}

	CANmodule->CANnormal = true;
}

CO_ReturnError_t CO_CANmodule_init(CO_CANmodule_t *CANmodule, void *CANptr, CO_CANrx_t rxArray[],
				   uint16_t rxSize, CO_CANtx_t txArray[], uint16_t txSize,
				   uint16_t CANbitRate)
{
	const struct device *dev = CANPTR_TO_DEV(CANptr);
	uint_fast16_t i;
	int err;
	int max_filters;

	LOG_DBG("init: rx_size=%u tx_size=%u", rxSize, txSize);

	/* verify arguments */
	if (CANmodule == NULL || CANptr == NULL || rxArray == NULL || txArray == NULL) {
		LOG_ERR("init failed: invalid args");
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	max_filters = can_get_max_filters(dev, false);
	if (max_filters != -ENOSYS) {
		if (max_filters < 0) {
			LOG_ERR("rx filter count query failed (err %d)", max_filters);
			return CO_ERROR_SYSCALL;
		}

		if (rxSize > max_filters) {
			LOG_ERR("rx filters insufficient: need=%u avail=%d", rxSize, max_filters);
			return CO_ERROR_OUT_OF_MEMORY;
		} else if (rxSize < max_filters) {
			LOG_DBG("rx filters: need=%u avail=%d", rxSize, max_filters);
		}
	}

	z_co_detach_all_rx_filters(CANmodule);
	canopen_tx_queue.CANmodule = CANmodule;

	/* Configure object variables */
	CANmodule->CANptr = CANptr;
	CANmodule->rxArray = rxArray;
	CANmodule->rxSize = rxSize;
	CANmodule->txArray = txArray;
	CANmodule->txSize = txSize;
	CANmodule->CANerrorStatus = 0;
	CANmodule->CANnormal = false;
	CANmodule->useCANrxFilters = (rxSize <= max_filters) ? true : false;
	CANmodule->firstCANtxMessage = true;
	CANmodule->errOld = 0U;

	if (CANmodule->useCANrxFilters) {
		/* Filters will be configured by CO_CANrxBufferInit() */
		for (i = 0U; i < rxSize; i++) {
			rxArray[i].ident = 0U;
			rxArray[i].CANrx_callback = NULL;
			rxArray[i].filter_id = -ENOSPC;
		}
	} else {
		/* If filters aren't used, all 11-bit IDs will be received */
	}

	for (i = 0U; i < txSize; i++) {
		txArray[i].bufferFull = false;
	}

	err = can_set_bitrate(dev, KHZ(CANbitRate));
	if (err) {
		LOG_ERR("bitrate set failed (err %d)", err);
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	err = can_set_mode(dev, CAN_MODE_NORMAL);
	if (err) {
		LOG_ERR("mode set failed (err %d)", err);
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	return CO_ERROR_NO;
}

void CO_CANmodule_disable(CO_CANmodule_t *CANmodule)
{
	if (!CANmodule || !CANmodule->CANptr) {
		return;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);

	/* Clear TX queue */
	if (k_work_cancel_delayable(&canopen_tx_queue.dwork) != 0) {
		while (k_work_delayable_busy_get(&canopen_tx_queue.dwork) != 0) {
			k_yield();
		}
	}

	canopen_tx_queue.backoff_ms = 0;

	/* Remove all RX filters */
	z_co_detach_all_rx_filters(CANmodule);

	/* Stop the CAN bus */
	int err = can_stop(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("can stop failed (err %d)", err);
	}
}

CO_ReturnError_t CO_CANrxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index, uint16_t ident,
				    uint16_t mask, bool_t rtr, void *object,
				    void (*CANrx_callback)(void *object, void *message))
{
	struct can_filter filter;
	CO_ReturnError_t ret = CO_ERROR_NO;

	if ((CANmodule != NULL) && (CANmodule->CANptr != NULL) && (object != NULL) &&
	    (CANrx_callback != NULL) && (index < CANmodule->rxSize)) {

		/* Buffer to configure */
		CO_CANrx_t *buffer = &CANmodule->rxArray[index];
		const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);

		/* Configure object variables */
		buffer->object = object;
		buffer->CANrx_callback = CANrx_callback;

		/* CAN identifier and mask, bit-aligned with CAN module */
		buffer->ident = ident & CAN_STD_ID_MASK;
		buffer->mask = (mask & CAN_STD_ID_MASK) | 0x0800U;

#ifndef CONFIG_CAN_ACCEPT_RTR
		if (rtr) {
			LOG_WRN("rtr requested but disabled");
			return CO_ERROR_ILLEGAL_ARGUMENT;
		}
#else  /* CONFIG_CAN_ACCEPT_RTR */
		if (rtr) {
			buffer->ident |= 0x0800U;
		}
#endif /* CONFIG_CAN_ACCEPT_RTR */

		/* Set CAN hardware module filter and mask */
		if (CANmodule->useCANrxFilters) {
			filter.flags = 0U;
			filter.id = ident;
			filter.mask = mask;

			if (buffer->filter_id != -ENOSPC) {
				can_remove_rx_filter(dev, buffer->filter_id);
			}
			buffer->filter_id =
				can_add_rx_filter(dev, z_co_rx_callback, CANmodule, &filter);
			if (buffer->filter_id == -ENOSPC) {
				LOG_ERR("rx filter add failed: no slots");
				ret = CO_ERROR_OUT_OF_MEMORY;
			}
		}
	} else {
		LOG_ERR("rx buffer init failed: invalid args");
		ret = CO_ERROR_ILLEGAL_ARGUMENT;
	}

	return ret;
}

CO_CANtx_t *CO_CANtxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index, uint16_t ident,
			       bool_t rtr, uint8_t noOfBytes, bool_t syncFlag)
{
	CO_CANtx_t *buffer = NULL;

	if ((CANmodule != NULL) && (index < CANmodule->txSize)) {
		/* specific buffer */
		buffer = &CANmodule->txArray[index];
		buffer->ident = ident & CAN_STD_ID_MASK;
		buffer->DLC = noOfBytes;
		buffer->flags = rtr ? CAN_FRAME_RTR : 0U;
		buffer->bufferFull = false;
		buffer->syncFlag = syncFlag;
	}

	return buffer;
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer)
{
	CO_ReturnError_t err = CO_ERROR_NO;
	struct can_frame frame;
	bool wasFullBefore = false;

	if (!CANmodule || !CANmodule->CANptr || !buffer) {
		LOG_ERR("tx send failed: invalid args");
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);

	memset(&frame, 0, sizeof(frame));
	frame.id = buffer->ident;
	frame.dlc = buffer->DLC;
	frame.flags = buffer->flags;
	memcpy(frame.data, buffer->data, buffer->DLC);

	CO_LOCK_CAN_SEND(CANmodule);
	wasFullBefore = buffer->bufferFull;

	int rc = can_send(dev, &frame, K_NO_WAIT, z_co_tx_callback, CANmodule);
	if (rc == 0) {
		/* sent or accepted by driver; callback will fire later */
		canopen_tx_queue.backoff_ms = 0;
		err = CO_ERROR_NO;
	} else {
		switch (rc) {
		case -EAGAIN:
			/* timeout (mailboxes busy / no immediate slot) */
			__fallthrough;
		case -ENETDOWN:
			/* controller stopped */
			__fallthrough;
		case -ENETUNREACH:
			/* bus-off */
			__fallthrough;
		case -EBUSY:
			/* arbitration lost (auto-retry off) */
			__fallthrough;
		case -EIO:
			/* general TX error (e.g., missing ACK w/ no auto-retry) */
			/* Buffer and ensure retry work runs */
			buffer->bufferFull = true;
			z_co_retry_schedule_backoff();
			/* Only report overflow if we had no software room BEFORE this call */
			err = wasFullBefore ? CO_ERROR_TX_OVERFLOW : CO_ERROR_NO;
			break;
		case -EINVAL:
			__fallthrough;
		case -ENOTSUP:
			/* Programming/config issue: retrying won’t help. Drop and surface. */
			LOG_ERR("can_send invalid/unsupported params (rc %d); dropping frame", rc);
			buffer->bufferFull = false;
			err = CO_ERROR_TX_UNCONFIGURED;
			break;
		default:
			/* Unknown negative errno: keep it and retry */
			LOG_WRN("can_send unexpected err %d; will retry", rc);
			buffer->bufferFull = true;
			z_co_retry_schedule_backoff();
			err = wasFullBefore ? CO_ERROR_TX_OVERFLOW : CO_ERROR_NO;
			break;
		}
	}

	CO_UNLOCK_CAN_SEND(CANmodule);

	return err;
}

void CO_CANclearPendingSyncPDOs(CO_CANmodule_t *CANmodule)
{
	bool_t tpdoDeleted = false;
	CO_CANtx_t *buffer;
	uint_fast16_t i;

	if (!CANmodule || !CANmodule->CANptr || !CANmodule->txArray) {
		return;
	}

	CO_LOCK_CAN_SEND(CANmodule);

	for (i = 0; i < CANmodule->txSize; i++) {
		buffer = &CANmodule->txArray[i];
		if (buffer->bufferFull && buffer->syncFlag) {
			buffer->bufferFull = false;
			tpdoDeleted = true;
		}
	}

	CO_UNLOCK_CAN_SEND(CANmodule);

	if (tpdoDeleted == true) {
		CANmodule->CANerrorStatus |= CO_CAN_ERRTX_PDO_LATE;
	}
}

void CO_CANmodule_process(CO_CANmodule_t *CANmodule)
{
	uint32_t err;

	if (!CANmodule || !CANmodule->CANptr) {
		return;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);

	/* Read controller state and error counters.
	 * If your can_get_state() takes values (not pointers), drop the '&'.
	 */
	enum can_state state;
	struct can_bus_err_cnt err_cnt;
	if (can_get_state(dev, &state, &err_cnt) != 0) {
		/* Keep previous status if state can't be read this cycle. */
		return;
	}

	/* Map driver counters/state into CANopenNode error inputs. */
	uint16_t txErrors = err_cnt.tx_err_cnt; /* 0..255 from controller */
	uint16_t rxErrors = err_cnt.rx_err_cnt; /* 0..255 from controller */
#if CONFIG_CAN_STATS
	uint8_t overflow = (can_stats_get_rx_overruns(dev) > 0);
#else
	uint8_t overflow = 0U;
#endif
	/* Ensure BUS-OFF handling even if counters aren't >= 256 yet. */
	if (state == CAN_STATE_BUS_OFF) {
		txErrors = 256U;
	}

	err = ((uint32_t)txErrors << 16) | ((uint32_t)rxErrors << 8) | overflow;

	if (CANmodule->errOld != err) {
		uint_fast16_t status = CANmodule->CANerrorStatus;

		CANmodule->errOld = err;

		if (txErrors >= 256U) {
			/* bus off */
			status |= CO_CAN_ERRTX_BUS_OFF;
		} else {
			/* recalculate CANerrorStatus, first clear some flags */
			status &= 0xFFFFU ^ (CO_CAN_ERRTX_BUS_OFF | CO_CAN_ERRRX_WARNING |
					     CO_CAN_ERRRX_PASSIVE | CO_CAN_ERRTX_WARNING |
					     CO_CAN_ERRTX_PASSIVE);

			/* rx bus warning or passive */
			if (rxErrors >= 128U) {
				status |= CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE;
			} else if (rxErrors >= 96U) {
				status |= CO_CAN_ERRRX_WARNING;
			}

			/* tx bus warning or passive */
			if (txErrors >= 128U) {
				status |= CO_CAN_ERRTX_WARNING | CO_CAN_ERRTX_PASSIVE;
			} else if (txErrors >= 96U) {
				status |= CO_CAN_ERRTX_WARNING;
			}

			/* if not tx passive clear also overflow */
			if ((status & CO_CAN_ERRTX_PASSIVE) == 0U) {
				status &= 0xFFFFU ^ CO_CAN_ERRTX_OVERFLOW;
			}
		}

		if (overflow != 0U) {
			/* CAN RX bus overflow */
			status |= CO_CAN_ERRRX_OVERFLOW;
		}

		CANmodule->CANerrorStatus = status;
	}
}

/*
 * Module initialization hook.
 *
 * Creates and starts the dedicated work queue used for deferred TX retries,
 * names its thread (handy for debugging), and initializes the work item.
 * Runs at SYS_INIT(APPLICATION) stage.
 */
static int z_co_init(void)
{
	k_work_queue_start(&canopen_tx_workq, canopen_tx_workq_stack,
			   K_KERNEL_STACK_SIZEOF(canopen_tx_workq_stack),
			   CONFIG_CANOPENNODE_TX_WORKQUEUE_PRIORITY, NULL);

	k_thread_name_set(&canopen_tx_workq.thread, "canopen_tx_workq");

	k_work_init_delayable(&canopen_tx_queue.dwork, z_co_tx_retry);
	canopen_tx_queue.backoff_ms = 0;

	return 0;
}

SYS_INIT(z_co_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
