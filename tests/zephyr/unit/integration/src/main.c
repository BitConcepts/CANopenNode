/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZTest suite: CANopenNode Zephyr integration API lifecycle.
 *
 * What is tested here
 * -------------------
 *   canopen_start()        — argument validation, success path, double-start
 *   canopen_stop()         — idempotency, state after stop
 *   canopen_is_running()   — state transitions before/after start/stop
 *   canopen_is_error()     — safe default when stack not init
 *   canopen_get_error_register() — safe default when stack not init
 *   canopen_error_report() — no-op safety when not init
 *   canopen_error_reset()  — no-op safety when not init
 *   canopen_get_node_id()  — default (Kconfig) and weak-override
 *
 * Platform
 * --------
 * native_sim  — direct host process, fastest CI feedback
 * qemu_x86    — QEMU VM, ISR/timer-accurate, closer to real hardware
 *
 * Both use the Zephyr can_loopback driver (CONFIG_CAN_LOOPBACK=y).
 * TX frames are immediately reflected as RX on the same device.
 *
 * Run:
 *   west twister -T tests/zephyr/unit/integration --platform native_sim -v
 *   west twister -T tests/zephyr/unit/integration --platform qemu_x86   -v
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/device.h>
#include <errno.h>

#include "CO_zephyr_integration.h"

/* CAN loopback device — provided by boards/*.overlay */
#define CAN_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))

/* Node-ID used for all tests */
#define TEST_NODE_ID  5U
#define TEST_BITRATE  500U   /* kbps */

/* --------------------------------------------------------------------------
 * Per-suite setup/teardown: ensure the stack is always stopped before/after
 * each test so tests are fully independent.
 * -------------------------------------------------------------------------- */
static void before_each(void *unused)
{
	ARG_UNUSED(unused);
	/* Unconditional stop — safe even if already stopped (idempotent) */
	canopen_stop();
	k_sleep(K_MSEC(10));
}

static void after_each(void *unused)
{
	ARG_UNUSED(unused);
	canopen_stop();
	k_sleep(K_MSEC(10));
}


/* ==========================================================================
 * Suite 1: is_running() state machine
 * ========================================================================== */
ZTEST_SUITE(integration_state, NULL, NULL, before_each, after_each, NULL);

ZTEST(integration_state, test_not_running_before_start)
{
	zassert_false(canopen_is_running(),
		      "canopen_is_running() must be false before any start");
}

ZTEST(integration_state, test_running_after_valid_start)
{
	const struct device *dev = CAN_DEV;

	zassert_true(device_is_ready(dev), "CAN device not ready");

	int rc = canopen_start(dev, TEST_NODE_ID, TEST_BITRATE);
	zassert_equal(rc, 0, "canopen_start() failed: %d", rc);
	zassert_true(canopen_is_running(),
		     "canopen_is_running() must be true after successful start");
}

ZTEST(integration_state, test_not_running_after_stop)
{
	const struct device *dev = CAN_DEV;

	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0, "start failed");
	zassert_true(canopen_is_running(), "not running after start");

	canopen_stop();
	k_sleep(K_MSEC(20));

	zassert_false(canopen_is_running(),
		      "canopen_is_running() must be false after stop");
}

ZTEST(integration_state, test_stop_idempotent_when_not_running)
{
	/* Call stop() multiple times without any start — must not crash */
	canopen_stop();
	canopen_stop();
	canopen_stop();
	zassert_false(canopen_is_running(), "should be false");
}


/* ==========================================================================
 * Suite 2: canopen_start() argument validation
 * ========================================================================== */
ZTEST_SUITE(integration_start_args, NULL, NULL, before_each, after_each, NULL);

ZTEST(integration_start_args, test_start_node_id_zero_rejected)
{
	const struct device *dev = CAN_DEV;
	int rc = canopen_start(dev, 0, TEST_BITRATE);
	zassert_equal(rc, -EINVAL,
		      "node_id=0 must return -EINVAL, got %d", rc);
	zassert_false(canopen_is_running(), "must not be running after failed start");
}

ZTEST(integration_start_args, test_start_node_id_128_rejected)
{
	const struct device *dev = CAN_DEV;
	int rc = canopen_start(dev, 128, TEST_BITRATE);
	zassert_equal(rc, -EINVAL,
		      "node_id=128 must return -EINVAL (max is 127), got %d", rc);
	zassert_false(canopen_is_running(), "must not be running after failed start");
}

ZTEST(integration_start_args, test_start_node_id_127_accepted)
{
	/* 127 is the maximum valid CANopen node-ID */
	const struct device *dev = CAN_DEV;
	int rc = canopen_start(dev, 127, TEST_BITRATE);
	zassert_equal(rc, 0, "node_id=127 must succeed, got %d", rc);
}

ZTEST(integration_start_args, test_start_node_id_1_accepted)
{
	const struct device *dev = CAN_DEV;
	int rc = canopen_start(dev, 1, TEST_BITRATE);
	zassert_equal(rc, 0, "node_id=1 (minimum) must succeed, got %d", rc);
}

ZTEST(integration_start_args, test_double_start_returns_ealready)
{
	const struct device *dev = CAN_DEV;

	int rc1 = canopen_start(dev, TEST_NODE_ID, TEST_BITRATE);
	zassert_equal(rc1, 0, "first start must succeed");

	int rc2 = canopen_start(dev, TEST_NODE_ID, TEST_BITRATE);
	zassert_equal(rc2, -EALREADY,
		      "second start while running must return -EALREADY, got %d", rc2);
}

ZTEST(integration_start_args, test_start_with_null_uses_default_device)
{
	/*
	 * Passing NULL for can_dev must use DT_CHOSEN(zephyr_canbus).
	 * On our test board overlay, zephyr,canbus = &can_loopback0, so this
	 * should succeed identically to passing the device explicitly.
	 */
	int rc = canopen_start(NULL, TEST_NODE_ID, TEST_BITRATE);
	zassert_equal(rc, 0, "NULL can_dev (use DT default) must succeed, got %d", rc);
}

ZTEST(integration_start_args, test_start_zero_bitrate_uses_dt_default)
{
	/*
	 * bitrate_kbps=0 must fall back to the DT bitrate property.
	 * The overlay sets bitrate=<500000> so this should succeed.
	 */
	const struct device *dev = CAN_DEV;
	int rc = canopen_start(dev, TEST_NODE_ID, 0);
	zassert_equal(rc, 0, "bitrate=0 (use DT default) must succeed, got %d", rc);
}


/* ==========================================================================
 * Suite 3: error helper safe defaults (pre-init and post-stop)
 * ========================================================================== */
ZTEST_SUITE(integration_error_helpers, NULL, NULL, before_each, after_each, NULL);

ZTEST(integration_error_helpers, test_is_error_returns_false_pre_init)
{
	/* Stack not started — must not crash and must return false */
	bool err = canopen_is_error(CO_EM_GENERIC_ERROR);
	zassert_false(err,
		      "canopen_is_error() must return false when stack not init");
}

ZTEST(integration_error_helpers, test_get_error_register_returns_zero_pre_init)
{
	uint8_t reg = canopen_get_error_register();
	zassert_equal(reg, 0,
		      "canopen_get_error_register() must return 0 when not init");
}

ZTEST(integration_error_helpers, test_error_report_noop_pre_init)
{
	/* Must not crash when called before canopen_start() */
	canopen_error_report(CO_EM_GENERIC_ERROR, CO_EMC_NO_ERROR, 0xDEADBEEF);
	zassert_true(true, "canopen_error_report() pre-init must not crash");
}

ZTEST(integration_error_helpers, test_error_reset_noop_pre_init)
{
	/* Must not crash when called before canopen_start() */
	canopen_error_reset(CO_EM_GENERIC_ERROR, 0);
	zassert_true(true, "canopen_error_reset() pre-init must not crash");
}

ZTEST(integration_error_helpers, test_is_error_false_after_clean_start)
{
	const struct device *dev = CAN_DEV;
	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0, "start failed");

	/* Fresh stack: no errors should be active */
	bool err = canopen_is_error(CO_EM_GENERIC_ERROR);
	zassert_false(err, "no error should be active after clean start");
}

ZTEST(integration_error_helpers, test_error_register_zero_after_clean_start)
{
	const struct device *dev = CAN_DEV;
	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0, "start failed");

	uint8_t reg = canopen_get_error_register();
	zassert_equal(reg, 0,
		      "error register must be 0 after clean start, got 0x%02X", reg);
}


/* ==========================================================================
 * Suite 4: canopen_get_node_id() weak hook
 * ========================================================================== */
ZTEST_SUITE(integration_node_id, NULL, NULL, before_each, after_each, NULL);

ZTEST(integration_node_id, test_default_node_id_from_kconfig)
{
	/*
	 * The default weak implementation returns CONFIG_CANOPENNODE_INIT_NODE_ID.
	 * prj.conf sets CONFIG_CANOPENNODE_INIT_NODE_ID=5, so we expect 5.
	 */
	uint8_t id = canopen_get_node_id();
	zassert_equal(id, CONFIG_CANOPENNODE_INIT_NODE_ID,
		      "Default node_id must match CONFIG_CANOPENNODE_INIT_NODE_ID "
		      "(expected %u, got %u)",
		      CONFIG_CANOPENNODE_INIT_NODE_ID, id);
}

ZTEST(integration_node_id, test_node_id_is_valid_range)
{
	uint8_t id = canopen_get_node_id();
	zassert_true(id >= 1 && id <= 127,
		     "canopen_get_node_id() must return 1..127, got %u", id);
}


/* ==========================================================================
 * Suite 5: start → stop → start cycle (restart)
 * ========================================================================== */
ZTEST_SUITE(integration_restart, NULL, NULL, before_each, after_each, NULL);

ZTEST(integration_restart, test_restart_after_stop_succeeds)
{
	const struct device *dev = CAN_DEV;

	/* First start */
	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0,
		      "first start failed");
	zassert_true(canopen_is_running(), "not running after first start");

	/* Stop */
	canopen_stop();
	k_sleep(K_MSEC(20));
	zassert_false(canopen_is_running(), "still running after stop");

	/* Second start (restart) — must succeed */
	int rc = canopen_start(dev, TEST_NODE_ID + 1, TEST_BITRATE);
	zassert_equal(rc, 0, "restart (second start) failed: %d", rc);
	zassert_true(canopen_is_running(), "not running after restart");
}

ZTEST(integration_restart, test_multiple_stop_start_cycles)
{
	const struct device *dev = CAN_DEV;

	for (int i = 0; i < 3; i++) {
		int rc = canopen_start(dev, TEST_NODE_ID, TEST_BITRATE);
		zassert_equal(rc, 0, "start failed on cycle %d: %d", i, rc);
		zassert_true(canopen_is_running(), "not running on cycle %d", i);

		canopen_stop();
		k_sleep(K_MSEC(15));
		zassert_false(canopen_is_running(), "still running after stop on cycle %d", i);
	}
}


/* ==========================================================================
 * Suite 6: CO pointer safety (global CO_t *CO)
 * ========================================================================== */
ZTEST_SUITE(integration_co_pointer, NULL, NULL, before_each, after_each, NULL);

ZTEST(integration_co_pointer, test_co_null_before_start)
{
	/* The global CO pointer must be NULL before any start */
	zassert_is_null(CO, "CO must be NULL before canopen_start()");
}

ZTEST(integration_co_pointer, test_co_valid_after_start)
{
	const struct device *dev = CAN_DEV;
	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0, "start failed");
	zassert_not_null(CO, "CO must be non-NULL after successful start");
}

ZTEST(integration_co_pointer, test_co_null_after_stop)
{
	const struct device *dev = CAN_DEV;
	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0, "start failed");
	zassert_not_null(CO, "CO must be valid after start");

	canopen_stop();
	k_sleep(K_MSEC(20));

	zassert_is_null(CO, "CO must be NULL after canopen_stop()");
}

ZTEST(integration_co_pointer, test_co_em_accessible_after_start)
{
	const struct device *dev = CAN_DEV;
	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0, "start failed");
	zassert_not_null(CO, "CO not set");
	zassert_not_null(CO->em, "CO->em must be non-NULL after start");
}

ZTEST(integration_co_pointer, test_co_nmt_accessible_after_start)
{
	const struct device *dev = CAN_DEV;
	zassert_equal(canopen_start(dev, TEST_NODE_ID, TEST_BITRATE), 0, "start failed");
	zassert_not_null(CO->NMT, "CO->NMT must be non-NULL after start");
}
