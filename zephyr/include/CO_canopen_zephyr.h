#pragma once

#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>

#include "CANopen.h"

/**
 * @file co_canopen_zephyr.h
 * @brief Zephyr-owned CANopen scheduler:
 *        - Worker: runs CO_process() and reschedules (timerNext_us optional)
 *        - RT thread: event-driven SYNC/RPDO/TPDO via PRE-callbacks (Option B)
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Core scheduler API ---------- */

/** Resolve CAN device (DT chosen or Kconfig) and start the worker. */
int co_canopen_start_auto(void);

/** Start with explicit CAN device and Node-ID. */
int co_canopen_start(const struct device *can_dev, uint8_t node_id);

/** Stop worker and tear down stack. */
void co_canopen_stop(void);

/** Get active CO_t pointer (or NULL). */
CO_t *co_canopen_get(void);

/** Is worker running? */
bool co_canopen_is_running(void);

/** Rebuild RX filters from the port-provided table (safe when running). */
int co_canopen_refresh_filters(void);

/* ---------- Port hooks (weak) you implement in your CANopenNode port ---------- */
/**
 * Allocate/init CANopenNode (CO), bind to @p can_dev, configure OD/COB-IDs, etc.
 * Must set *out_co on success.
 */
int __weak coz_port_create(const struct device *can_dev, uint8_t node_id, CO_t **out_co);

/** Teardown/free CANopenNode created by coz_port_create(). */
void __weak coz_port_destroy(CO_t *co);

/**
 * Provide RX filter table for all COB-IDs the stack should receive.
 * Return count (<= max_filters). Fill @p out (id/mask/flags).
 */
size_t __weak coz_port_get_filter_table(const CO_t *co, struct can_filter *out, size_t max_filters);

/**
 * Dispatch one received CAN frame into CANopenNode (ISR context).
 * Keep this short & non-blocking; do heavy work in CO_process().
 */
void __weak coz_port_rx_dispatch_isr(CO_t *co, const struct can_frame *frame);

/**
 * Enable PRE-callbacks for SYNC and RPDO so the RT thread wakes immediately.
 * Typical implementation:
 *   CO_SYNC_initCallbackPre(CO->SYNC, pre_cb, pre_arg);
 *   for each RPDO: CO_RPDO_initCallbackPre(rpdo, pre_cb, pre_arg);
 *
 * Pass @p pre_cb = coz_rt_signal_cb and @p pre_arg = NULL (or anything).
 */
void __weak coz_port_enable_pre_signals(CO_t *co, void (*pre_cb)(void *object), void *pre_arg);

/* ---------- Provided PRE-callback for ports to use ---------- */
/**
 * PRE-callback you can register in your port to wake the RT thread.
 * Signature matches CANopenNode's *_initCallbackPre() expectations.
 * Safe to call from ISR or thread context.
 */
void coz_rt_signal_cb(void *object);

#ifdef __cplusplus
}
#endif
