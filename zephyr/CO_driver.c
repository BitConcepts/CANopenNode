/*
 * CAN module object for generic microcontroller.
 *
 * This file is a template for other microcontrollers.
 *
 * @file        CO_driver.c
 * @ingroup     CO_driver
 * @author      Janez Paternoster
 * @copyright   2004 - 2020 Janez Paternoster
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

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(canopennode_driver, CONFIG_CANOPEN_LOG_LEVEL);

#define CANPTR_TO_DEV(ptr) ((const struct device *)(ptr))

typedef struct {
	uint32_t ident;
	uint8_t DLC;
	uint8_t data[8];
} CO_CANrxMsg_t;

K_KERNEL_STACK_DEFINE(canopen_tx_workq_stack, CONFIG_CANOPENNODE_TX_WORKQUEUE_STACK_SIZE);

struct k_work_q canopen_tx_workq;

struct canopen_tx_work_container {
	struct k_work work;
	CO_CANmodule_t *CANmodule;
};

struct canopen_tx_work_container canopen_tx_queue;

K_MUTEX_DEFINE(canopen_send_mutex);
K_MUTEX_DEFINE(canopen_emcy_mutex);
K_MUTEX_DEFINE(canopen_co_mutex);

inline void canopen_send_lock(void)
{
	k_mutex_lock(&canopen_send_mutex, K_FOREVER);
}

inline void canopen_send_unlock(void)
{
	k_mutex_unlock(&canopen_send_mutex);
}

inline void canopen_emcy_lock(void)
{
	k_mutex_lock(&canopen_emcy_mutex, K_FOREVER);
}

inline void canopen_emcy_unlock(void)
{
	k_mutex_unlock(&canopen_emcy_mutex);
}

inline void canopen_od_lock(void)
{
	k_mutex_lock(&canopen_co_mutex, K_FOREVER);
}

inline void canopen_od_unlock(void)
{
	k_mutex_unlock(&canopen_co_mutex);
}

static void canopen_detach_all_rx_filters(CO_CANmodule_t *CANmodule)
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

static void canopen_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
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

		if (((frame->id ^ buffer->ident) & buffer->mask) == 0U) {
#ifdef CONFIG_CAN_ACCEPT_RTR
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
			break;
		}
	}
}

static void canopen_tx_callback(const struct device *dev, int error, void *arg)
{
	CO_CANmodule_t *CANmodule = arg;

	ARG_UNUSED(dev);

	if (!CANmodule) {
		LOG_ERR("failed to process CAN tx callback");
		return;
	}

	if (error == 0) {
		CANmodule->firstCANtxMessage = false;
	}

	k_work_submit_to_queue(&canopen_tx_workq, &canopen_tx_queue.work);
}

static void canopen_tx_retry(struct k_work *item)
{
	struct canopen_tx_work_container *container =
		CONTAINER_OF(item, struct canopen_tx_work_container, work);
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
			frame.flags |= ((buffer->ident & 0x800) ? CAN_FRAME_RTR : 0);
			memcpy(frame.data, buffer->data, buffer->DLC);

			err = can_send(dev, &frame, K_NO_WAIT, canopen_tx_callback, CANmodule);
			if (err == -EAGAIN) {
				break;
			} else if (err != 0) {
				LOG_ERR("failed to send CAN frame (err %d)", err);
			}

			buffer->bufferFull = false;
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
	int err;

	err = can_stop(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("failed to stop CAN interface (err %d)", err);
	}
}

void CO_CANsetNormalMode(CO_CANmodule_t *CANmodule)
{
	if (!CANmodule || !CANmodule->CANptr) {
		return;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);
	int err;

	err = can_start(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("failed to start CAN interface (err %d)", err);
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

	LOG_DBG("rxSize = %d, txSize = %d", rxSize, txSize);

	/* verify arguments */
	if (CANmodule == NULL || CANptr == NULL || rxArray == NULL || txArray == NULL) {
		LOG_ERR("failed to initialize CAN module");
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	max_filters = can_get_max_filters(dev, false);
	if (max_filters != -ENOSYS) {
		if (max_filters < 0) {
			LOG_ERR("unable to determine number of CAN RX filters");
			return CO_ERROR_SYSCALL;
		}

		if (rxSize > max_filters) {
			LOG_ERR("insufficient number of concurrent CAN RX filters"
				" (needs %d, %d available)",
				rxSize, max_filters);
			return CO_ERROR_OUT_OF_MEMORY;
		} else if (rxSize < max_filters) {
			LOG_DBG("excessive number of concurrent CAN RX filters enabled"
				" (needs %d, %d available)",
				rxSize, max_filters);
		}
	}

	canopen_detach_all_rx_filters(CANmodule);
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
		/* CAN module filters are used, they will be configured with */
		/* CO_CANrxBufferInit() functions, called by separate CANopen */
		/* init functions. */
		for (i = 0U; i < rxSize; i++) {
			rxArray[i].ident = 0U;
			rxArray[i].CANrx_callback = NULL;
			rxArray[i].filter_id = -ENOSPC;
		}
	} else {
		/* CAN module filters are not used, all messages with standard 11-bit */
		/* identifier will be received */
	}
	for (i = 0U; i < txSize; i++) {
		txArray[i].bufferFull = false;
	}

	err = can_set_bitrate(dev, KHZ(CANbitRate));
	if (err) {
		LOG_ERR("failed to configure CAN bitrate (err %d)", err);
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	err = can_set_mode(dev, CAN_MODE_NORMAL);
	if (err) {
		LOG_ERR("failed to configure CAN interface (err %d)", err);
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	return CO_ERROR_NO;
}

void CO_CANmodule_disable(CO_CANmodule_t *CANmodule)
{
	int err;

	if (!CANmodule || !CANmodule->CANptr) {
		return;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);

	canopen_detach_all_rx_filters(CANmodule);

	err = can_stop(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("failed to disable CAN interface (err %d)", err);
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
		/* buffer, which will be configured */
		CO_CANrx_t *buffer = &CANmodule->rxArray[index];
		const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);
		/* Configure object variables */
		buffer->object = object;
		buffer->CANrx_callback = CANrx_callback;

		/* CAN identifier and CAN mask, bit aligned with CAN module. Different on different
		 * microcontrollers. */
		buffer->ident = ident & CAN_STD_ID_MASK;
		buffer->mask = (mask & CAN_STD_ID_MASK) | 0x0800U;

#ifndef CONFIG_CAN_ACCEPT_RTR
		if (rtr) {
			LOG_ERR("request for RTR frames, but RTR frames are rejected");
			return CO_ERROR_ILLEGAL_ARGUMENT;
		}
#else  /* !CONFIG_CAN_ACCEPT_RTR */
		if (rtr) {
			buffer->ident |= 0x0800U;
		}
#endif /* CONFIG_CAN_ACCEPT_RTR */

		/* Set CAN hardware module filter and mask. */
		if (CANmodule->useCANrxFilters) {
			filter.flags = 0U;
			filter.id = ident;
			filter.mask = mask;

			if (buffer->filter_id != -ENOSPC) {
				can_remove_rx_filter(dev, buffer->filter_id);
			}
			buffer->filter_id =
				can_add_rx_filter(dev, canopen_rx_callback, CANmodule, &filter);
			if (buffer->filter_id == -ENOSPC) {
				LOG_ERR("failed to add CAN rx callback, no free filter");
				ret = CO_ERROR_OUT_OF_MEMORY;
			}
		}
	} else {
		ret = CO_ERROR_ILLEGAL_ARGUMENT;
	}

	return ret;
}

CO_CANtx_t *CO_CANtxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index, uint16_t ident,
			       bool_t rtr, uint8_t noOfBytes, bool_t syncFlag)
{
	CO_CANtx_t *buffer = NULL;

	if ((CANmodule != NULL) && (index < CANmodule->txSize)) {
		/* get specific buffer */
		buffer = &CANmodule->txArray[index];

		/* CAN identifier, DLC and rtr, bit aligned with CAN module transmit buffer,
		 * microcontroller specific. */
		buffer->ident = ((uint32_t)ident & CAN_STD_ID_MASK) |
				((uint32_t)(((uint32_t)noOfBytes & 0xFU) << 11U)) |
				((uint32_t)(rtr ? 0x800U : 0U));

		buffer->bufferFull = false;
		buffer->syncFlag = syncFlag;
	}

	return buffer;
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer)
{
	CO_ReturnError_t err = CO_ERROR_NO;
	struct can_frame frame;

	if (!CANmodule || !CANmodule->CANptr || !buffer) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	const struct device *dev = CANPTR_TO_DEV(CANmodule->CANptr);

	memset(&frame, 0, sizeof(frame));
	frame.id = buffer->ident;
	frame.dlc = buffer->DLC;
	frame.flags = ((buffer->ident & 0x800) ? CAN_FRAME_RTR : 0);
	memcpy(frame.data, buffer->data, buffer->DLC);

	CO_LOCK_CAN_SEND(CANmodule);

	err = can_send(dev, &frame, K_NO_WAIT, canopen_tx_callback, CANmodule);
	if (err == -EAGAIN) {
		LOG_ERR("failed to send CAN frame, tx overflow");
		err = CO_ERROR_TX_OVERFLOW;
		buffer->bufferFull = true;
	} else if (err != 0) {
		LOG_ERR("failed to send CAN frame (err %d)", err);
		err = CO_ERROR_TX_UNCONFIGURED;
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

/* Get error counters from the module. If necessary, function may use different way to determine
 * errors. */
static uint_fast16_t rxErrors = 0, txErrors = 0, overflow = 0;

void CO_CANmodule_process(CO_CANmodule_t *CANmodule)
{
	uint32_t err;

	if (!CANmodule || !CANmodule->CANptr) {
		return;
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
			status &= 0xFFFF ^ (CO_CAN_ERRTX_BUS_OFF | CO_CAN_ERRRX_WARNING |
					    CO_CAN_ERRRX_PASSIVE | CO_CAN_ERRTX_WARNING |
					    CO_CAN_ERRTX_PASSIVE);

			/* rx bus warning or passive */
			if (rxErrors >= 128) {
				status |= CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE;
			} else if (rxErrors >= 96) {
				status |= CO_CAN_ERRRX_WARNING;
			}

			/* tx bus warning or passive */
			if (txErrors >= 128) {
				status |= CO_CAN_ERRTX_WARNING | CO_CAN_ERRTX_PASSIVE;
			} else if (txErrors >= 96) {
				status |= CO_CAN_ERRTX_WARNING;
			}

			/* if not tx passive clear also overflow */
			if ((status & CO_CAN_ERRTX_PASSIVE) == 0) {
				status &= 0xFFFF ^ CO_CAN_ERRTX_OVERFLOW;
			}
		}

		if (overflow != 0) {
			/* CAN RX bus overflow */
			status |= CO_CAN_ERRRX_OVERFLOW;
		}

		CANmodule->CANerrorStatus = status;
	}
}

static int canopen_init(void)
{
	k_work_queue_start(&canopen_tx_workq, canopen_tx_workq_stack,
			   K_KERNEL_STACK_SIZEOF(canopen_tx_workq_stack),
			   CONFIG_CANOPENNODE_TX_WORKQUEUE_PRIORITY, NULL);

	k_thread_name_set(&canopen_tx_workq.thread, "canopen_tx_workq");

	k_work_init(&canopen_tx_queue.work, canopen_tx_retry);

	return 0;
}

SYS_INIT(canopen_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
