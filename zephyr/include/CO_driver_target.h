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

#include <zephyr/device.h>
#include <zephyr/dsp/types.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/toolchain.h>
#include <zephyr/types.h>

#include "CO_zephyr_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup co_driver_target Zephyr driver target (porting layer)
 * @brief Zephyr-specific targets, types, and primitives for CANopenNode.
 *
 * This header supplies the minimal target abstraction used by the CANopenNode
 * driver core when running on Zephyr. It defines endianness helpers, memory
 * allocation hooks, fundamental types, CAN message/container structures, and a
 * few synchronization/locking primitives the stack expects.
 *
 * Include this header in the target-specific CAN driver and any integration
 * units that call directly into CANopenNode’s low-level driver API.
 *
 * @note Some items here reflect CANopenNode’s historic target template and are
 *       retained for source compatibility even if they are no-ops on Zephyr.
 * @{
 */

/* -------------------------------------------------------------------------- */
/*                               Configuration                                */
/* -------------------------------------------------------------------------- */

/**
 * @name Endianness selection and byte-swap helpers
 * @brief Configure byte order and provide swap macros required by CANopenNode.
 *
 * If the build is little-endian (@c CONFIG_LITTLE_ENDIAN), no swapping is
 * performed. Otherwise, Zephyr byte-order helpers are used to convert to big
 * endian.
 * @{
 */
#ifdef CONFIG_LITTLE_ENDIAN
/** @brief Defined when the target CPU is little-endian. */
#define CO_LITTLE_ENDIAN
/** @brief 16-bit no-op swap on little-endian targets. */
#define CO_SWAP_16(x) x
/** @brief 32-bit no-op swap on little-endian targets. */
#define CO_SWAP_32(x) x
/** @brief 64-bit no-op swap on little-endian targets. */
#define CO_SWAP_64(x) x
#else
/** @brief Defined when the target CPU is big-endian. */
#define CO_BIG_ENDIAN
/** @brief 16-bit host→big-endian conversion using Zephyr helper. */
#define CO_SWAP_16(x) sys_cpu_to_be16(x)
/** @brief 32-bit host→big-endian conversion using Zephyr helper. */
#define CO_SWAP_32(x) sys_cpu_to_be32(x)
/** @brief 64-bit host→big-endian conversion using Zephyr helper. */
#define CO_SWAP_64(x) sys_cpu_to_be64(x)
#endif
/** @} */

/**
 * @name Memory allocation hooks
 * @brief Thin wrappers around Zephyr heap allocation used by CANopenNode.
 * @{
 */
/** @brief Allocate @p num objects of size @p size bytes using Zephyr heap. */
#define CO_alloc(num, size) k_calloc((num), (size))
/** @brief Free memory previously returned by CO_alloc(). */
#define CO_free(ptr)        k_free((ptr))
/** @} */

/**
 * @name Debug log hook
 * @brief Thin wrapper around Zephyr debug logging used by CANopenNode.
 * @{
 */
#ifdef CO_CONFIG_DEBUG
extern void z_canopen_log(const char *msg);
#define CO_DEBUG_COMMON(msg) canopen_log(msg)
#endif
/** @} */

/* -------------------------------------------------------------------------- */
/*                             Fundamental types                               */
/* -------------------------------------------------------------------------- */

/**
 * @name Base typedefs expected by CANopenNode
 * @brief Provide canonical types with the names used by the stack.
 *
 * @note Zephyr already defines @c float32_t and @c float64_t in
 *       @c <zephyr/dsp/types.h>. These aliases are kept for compatibility.
 * @{
 */
/** @brief Boolean type used by CANopenNode (fast unsigned 8-bit). */
typedef uint_fast8_t bool_t;
/** @brief 32-bit floating point (alias). */
typedef float float32_t;
/** @brief 64-bit floating point (alias). */
typedef double float64_t;
/** @brief Character type used by the stack. */
typedef char char_t;
/** @brief Octet-string character type (unsigned 8-bit). */
typedef unsigned char oChar_t;
/** @brief DOMAIN data type alias (raw byte). */
typedef unsigned char domain_t;
/** @} */

/* -------------------------------------------------------------------------- */
/*                          CAN message accessors                              */
/* -------------------------------------------------------------------------- */

/**
 * @name Receive message accessors
 */
/** @brief Return CAN identifier from RX message. */
#define CO_CANrxMsg_readIdent(msg) (((CO_CANrxMsg_t *)msg)->ident)
/** @brief Return ID from packed RX message CAN identifier. */
#define CO_CANrxMsg_readID(msg)    (CO_CANrxMsg_readIdent(msg) & CAN_STD_ID_MASK)
/** @brief Return DLC from packed RX message CAN identifier. */
#define CO_CANrxMsg_readDLC(msg)   (((CO_CANrxMsg_t *)msg)->DLC)
/** @brief Return data pointer from RX message. */
#define CO_CANrxMsg_readData(msg)  (((CO_CANrxMsg_t *)msg)->data)
/** @} */

/* -------------------------------------------------------------------------- */
/*                              Driver objects                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Receive message object (driver-side).
 *
 * Describes a CAN receive object thunk used by the stack.
 */
typedef struct {
	uint32_t ident;
	uint8_t DLC;
	uint8_t data[8];
} CO_CANrxMsg_t;

/**
 * @brief Receive message object (driver-side).
 *
 * Describes a CAN receive filter and its callback thunk used by the stack.
 */
typedef struct {
	/** Filter identifier (driver-specific). */
	int filter_id;
	/** 11/29-bit identifier to match (masked by @ref mask). */
	uint16_t ident;
	/** Identifier mask: set bits are compared. */
	uint16_t mask;
	/** User object passed to the callback. */
	void *object;
	/**
	 * @brief RX callback invoked on a matching frame.
	 * @param object  User object as registered in @ref object.
	 * @param message Driver-specific frame pointer/handle.
	 */
	void (*CANrx_callback)(void *object, void *message);
} CO_CANrx_t;

/**
 * @brief Transmit message object (driver-side).
 *
 * Represents a queued CAN frame and its state flags as used by the stack.
 */
typedef struct {
	/** Standard (11-bit) or extended (29-bit) CAN identifier. */
	uint32_t ident;
	/** Data length code (0–8). */
	uint8_t DLC;
	/** Payload buffer (0–8 bytes valid per @ref DLC). */
	uint8_t data[8];
	/** Flags. @see @ref CAN_FRAME_FLAGS. */
	uint8_t flags;
	/** Set when the buffer holds a pending frame. */
	volatile bool_t bufferFull;
	/** Set for frames transmitted during SYNC window. */
	volatile bool_t syncFlag;
} CO_CANtx_t;

/**
 * @brief CAN module container used by CANopenNode.
 *
 * Holds RX/TX arrays, sizes, error state, and runtime flags.
 */
typedef struct {
	/** Opaque driver CAN pointer (e.g., @c const struct device*). */
	void *CANptr;
	/** Array of RX filter objects. */
	CO_CANrx_t *rxArray;
	/** Number of RX elements in @ref rxArray. */
	uint16_t rxSize;
	/** Array of TX buffer objects. */
	CO_CANtx_t *txArray;
	/** Number of TX elements in @ref txArray. */
	uint16_t txSize;
	/** Last CAN error/status flags (implementation-defined). */
	uint16_t CANerrorStatus;
	/** Module is in normal operating mode when true. */
	volatile bool_t CANnormal;
	/** Whether hardware RX filters are used. */
	volatile bool_t useCANrxFilters;
	/** Set until the first TX frame is sent after init. */
	volatile bool_t firstCANtxMessage;
	/** Cached copy of last error flags for change detection. */
	uint32_t errOld;
} CO_CANmodule_t;

/**
 * @brief Storage entry descriptor (target-specific extension).
 *
 * Describes a single non-volatile storage mapping for an OD entry or
 * application variable.
 *
 * @note Fields beyond the common subset are target-specific and may be used
 *       by the Zephyr storage glue (e.g., EEPROM/flash helpers).
 */
typedef struct {
	void *addr;          /**< Address of data to store, always required. */
	size_t len;          /**< Length of data to store, always required. */
	uint8_t subIndexOD;  /**< Sub index in OD objects 1010 and 1011, from 2 to 127. Writing
				0x65766173 to 1010,subIndexOD  will store data to non-volatile memory
				Writing 0x64616F6C to 1011,subIndexOD will restore  default data,
				always required. */
	uint8_t attr;        /**< Attribute from @ref CO_storage_attributes_t, always required. */
	void *storageModule; /**< Pointer to storage module, target system specific usage, required
				with @ref CO_storage_eeprom. */
	uint16_t crc; /**< CRC checksum of the data stored in eeprom, set on store, required with
			 @ref CO_storage_eeprom. */
	size_t eepromAddrSignature; /**< Address of entry signature inside eeprom, set by init,
				       required with @ref CO_storage_eeprom. */
	size_t eepromAddr; /**< Address of data inside eeprom, set by init, required with @ref
			      CO_storage_eeprom. */
	size_t offset; /**< Offset of next byte being updated by automatic storage, required with
			  @ref CO_storage_eeprom. */
	void *additionalParameters; /**< Additional target specific parameters, optional. */
} CO_storage_entry_t;

/* -------------------------------------------------------------------------- */
/*                              Locking primitives                             */
/* -------------------------------------------------------------------------- */

/**
 * @name Critical-section helpers
 * @brief Locks used by CANopenNode around specific critical regions.
 *
 * These symbols must be provided by the integration layer. They may map to
 * mutexes, IRQ locks, or other Zephyr primitives as appropriate.
 * @{
 */

/**
 * @brief Lock critical section around CO_CANsend().
 *
 * Blocks concurrent access to the TX path while the stack composes and enqueues
 * a frame.
 */
void z_co_send_lock(void);
/** @brief Unlock critical section started by @ref z_co_send_lock. */
void z_co_send_unlock(void);

/** @brief Enter critical section for EMCY reporting/reset. */
void z_co_emcy_lock(void);
/** @brief Exit critical section for EMCY reporting/reset. */
void z_co_emcy_unlock(void);

/** @brief Enter critical section for Object Dictionary access. */
void z_co_od_lock(void);
/** @brief Exit critical section for Object Dictionary access. */
void z_co_od_unlock(void);

/** @brief Macro used by CANopenNode to guard TX critical section. */
#define CO_LOCK_CAN_SEND(CAN_MODULE)   z_co_send_lock()
/** @brief Macro used by CANopenNode to release TX critical section. */
#define CO_UNLOCK_CAN_SEND(CAN_MODULE) z_co_send_unlock()
/** @brief Macro used by CANopenNode to guard EMCY critical section. */
#define CO_LOCK_EMCY(CAN_MODULE)       z_co_emcy_lock()
/** @brief Macro used by CANopenNode to release EMCY critical section. */
#define CO_UNLOCK_EMCY(CAN_MODULE)     z_co_emcy_unlock()
/** @brief Macro used by CANopenNode to guard OD critical section. */
#define CO_LOCK_OD(CAN_MODULE)         z_co_od_lock()
/** @brief Macro used by CANopenNode to release OD critical section. */
#define CO_UNLOCK_OD(CAN_MODULE)       z_co_od_unlock()
/** @} */

/* -------------------------------------------------------------------------- */
/*                         Inter-thread synchronization                        */
/* -------------------------------------------------------------------------- */

/**
 * @name RX/TX synchronization flags
 * @brief Simple flag protocol between ISR/worker contexts.
 *
 * These macros implement a trivial "new data" flag with a memory barrier
 * placeholder. On Zephyr, @ref CO_MemoryBarrier resolves to an empty macro by
 * default; define it to an architecture-appropriate barrier if needed.
 * @{
 */
/** @brief Memory barrier used by flag setters/getters (no-op by default). */
#define CO_MemoryBarrier()

/**
 * @brief Test the "new data" flag.
 * @param rxNew An lvalue flag variable (pointer-sized).
 * @retval true  New data is available.
 * @retval false No new data.
 */
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)

/**
 * @brief Set the "new data" flag (with barrier).
 * @param rxNew An lvalue flag variable (pointer-sized).
 */
#define CO_FLAG_SET(rxNew)                                                                         \
	{                                                                                          \
		CO_MemoryBarrier();                                                                \
		rxNew = (void *)1L;                                                                \
	}

/**
 * @brief Clear the "new data" flag (with barrier).
 * @param rxNew An lvalue flag variable (pointer-sized).
 */
#define CO_FLAG_CLEAR(rxNew)                                                                       \
	{                                                                                          \
		CO_MemoryBarrier();                                                                \
		rxNew = NULL;                                                                      \
	}
/** @} */

/** @} */ /* end of co_driver_target */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_DRIVER_TARGET_H */
