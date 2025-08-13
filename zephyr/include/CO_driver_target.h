/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr target configuration for CANopenNode driver.
 *
 * Device- and application-specific definitions used by CANopenNode’s
 * driver/porting layer on Zephyr. Provides target constants and macros
 * (locking, timing, endian helpers, limits, etc.) tailored to the SoC/board
 * and build configuration.
 *
 * @file        CO_driver_target.h
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

#ifndef ZEPHYR_MODULES_CANOPENNODE_CO_DRIVER_TARGET_H
#define ZEPHYR_MODULES_CANOPENNODE_CO_DRIVER_TARGET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/device.h>
#include <zephyr/dsp/types.h> /* float32_t, float64_t */
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/toolchain.h>
#include <zephyr/types.h>

/* Stack configuration override default values. For more information see file CO_config.h. */

/* Basic definitions. If big endian, CO_SWAP_xx macros must swap bytes. */
#ifdef CONFIG_LITTLE_ENDIAN
#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) x
#define CO_SWAP_32(x) x
#define CO_SWAP_64(x) x
#else
#define CO_BIG_ENDIAN
#define CO_SWAP_16(x) sys_cpu_to_be16(x)
#define CO_SWAP_32(x) sys_cpu_to_be32(x)
#define CO_SWAP_64(x) sys_cpu_to_be64(x)
#endif

#define CO_alloc(num, size) k_calloc((num), (size))
#define CO_free(ptr)        k_free((ptr))

/* NULL is defined in stddef.h */
/* true and false are defined in stdbool.h */
/* int8_t to uint64_t are defined in stdint.h */
typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

// typedef char          char_t;
// typedef unsigned char oChar_t;
// typedef unsigned char domain_t;

/* Access to received CAN message */
#define CO_CANrxMsg_readIdent(msg) ((uint16_t)0)
#define CO_CANrxMsg_readDLC(msg)   ((uint8_t)0)
#define CO_CANrxMsg_readData(msg)  ((const uint8_t *)NULL)

/* Received message object */
typedef struct {
	int filter_id;
	uint16_t ident;
	uint16_t mask;
	void *object;
	void (*CANrx_callback)(void *object, void *message);
} CO_CANrx_t;

/* Transmit message object */
typedef struct {
	uint32_t ident;
	uint8_t DLC;
	uint8_t data[8];
	volatile bool_t bufferFull;
	volatile bool_t syncFlag;
} CO_CANtx_t;

/* CAN module object */
typedef struct {
	void *CANptr;
	CO_CANrx_t *rxArray;
	uint16_t rxSize;
	CO_CANtx_t *txArray;
	uint16_t txSize;
	uint16_t CANerrorStatus;
	volatile bool_t CANnormal;
	volatile bool_t useCANrxFilters;
	volatile bool_t firstCANtxMessage;
	uint32_t errOld;
} CO_CANmodule_t;

/* Data storage object for one entry */
typedef struct {
	void *addr;
	size_t len;
	uint8_t subIndexOD;
	uint8_t attr;
	/* Additional variables (target specific) */
	void *addrNV;
	void *storageModule;
	uint8_t *data;
	size_t eepromAddr;
	// size_t len;

	// entry->eepromAddrSignature = signaturesAddress + (sizeof(uint32_t) * i);
	// entry->eepromAddr = CO_eeprom_getAddr(storageModule, isAuto, entry->len, &eepromOvf);
	// entry->offset = 0;
	// entry->storageModule, entry->addr, entry->eepromAddr, entry->len);
} CO_storage_entry_t;

// bool_t CO_eeprom_writeBlock(void* storageModule, uint8_t* data, size_t eepromAddr, size_t len);

/* (un)lock critical section in CO_CANsend() */
void canopen_send_lock(void);
void canopen_send_unlock(void);
#define CO_LOCK_CAN_SEND(CAN_MODULE)   canopen_send_lock()
#define CO_UNLOCK_CAN_SEND(CAN_MODULE) canopen_send_unlock()

/* (un)lock critical section in CO_errorReport() or CO_errorReset() */
void canopen_emcy_lock(void);
void canopen_emcy_unlock(void);
#define CO_LOCK_EMCY(CAN_MODULE)   canopen_emcy_lock()
#define CO_UNLOCK_EMCY(CAN_MODULE) canopen_emcy_unlock()

/* (un)lock critical section when accessing Object Dictionary */
void canopen_od_lock(void);
void canopen_od_unlock(void);
#define CO_LOCK_OD(CAN_MODULE)   canopen_od_lock()
#define CO_UNLOCK_OD(CAN_MODULE) canopen_od_unlock()

/* Synchronization between CAN receive and message processing threads. */
#define CO_MemoryBarrier()
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)                                                                         \
	{                                                                                          \
		CO_MemoryBarrier();                                                                \
		rxNew = (void *)1L;                                                                \
	}
#define CO_FLAG_CLEAR(rxNew)                                                                       \
	{                                                                                          \
		CO_MemoryBarrier();                                                                \
		rxNew = NULL;                                                                      \
	}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_DRIVER_TARGET_H */
