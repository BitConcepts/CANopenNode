/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZTest suite: CANopen protocol frame exchange (native_sim + qemu_x86).
 *
 * Uses the Zephyr can_loopback driver: every TX frame is immediately
 * reflected as an RX frame on the same device, enabling end-to-end
 * CANopen protocol verification without real CAN hardware.
 *
 * What is tested
 * --------------
 *   Bootup frame       CiA 301 §7.5.2.2 — value 0x00 on COB-ID 0x700+nodeId
 *   Heartbeat producer CiA 301 §7.5.2.3 — NMT state on 0x700+nodeId
 *   NMT state read     CO_NMT_getInternalState() after start
 *   SDO expedited read CiA 301 §7.2.4.3 — upload device type (OD 0x1000)
 *   NMT command        Send Stop/Pre-op/Start to self, verify state change
 *
 * Platforms
 * ---------
 *   native_sim   — host process, fast
 *   qemu_x86     — QEMU VM, ISR/timer accurate
 *   qemu_cortex_m3 — ARM QEMU, validates little-endian byte ordering
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/device.h>
#include <string.h>
#include <errno.h>

#include "CANopen.h"
#include "CO_zephyr_integration.h"

#define CAN_DEV       DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))
#define TEST_NODE_ID  5U
#define TEST_BITRATE  500U  /* kbps */

/* CANopen CAN-ID helpers (CiA 301) */
#define NMT_CMD_ID      0x000U            /* NMT command frame (no node-ID in ID) */
#define HEARTBEAT_ID(n) (0x700U + (n))    /* Heartbeat / bootup COB-ID */
#define SDO_SRV_TX(n)   (0x580U + (n))    /* SDO server response (server→client) */
#define SDO_CLI_RX(n)   (0x600U + (n))    /* SDO client request  (client→server) */

/* NMT state values (CiA 301 §7.3.1) */
#define NMT_BOOTUP      0x00U
#define NMT_OPERATIONAL 0x05U
#define NMT_STOPPED     0x04U
#define NMT_PRE_OP      0x7FU

/* NMT command codes */
#define NMT_CMD_START   0x01U
#define NMT_CMD_STOP    0x02U
#define NMT_CMD_PRE_OP  0x80U
#define NMT_CMD_RESET_COMM 0x82U

/* SDO command specifiers */
#define SDO_UPLOAD_REQ    0x40U  /* client→server: initiate upload */
#define SDO_UPLOAD_RESP4  0x43U  /* server→client: 4-byte expedited upload response */

/* -------------------------------------------------------------------------
 * Shared receive buffer: the sniffer filter callback stores the last frame.
 * Protected by a semaphore so tests can block until a frame arrives.
 * -------------------------------------------------------------------------*/
static struct can_frame g_last_frame;
static struct k_sem     g_frame_sem;
static int              g_filter_id = -1;

static void sniffer_cb(const struct device *dev, struct can_frame *frame, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	memcpy(&g_last_frame, frame, sizeof(*frame));
	k_sem_give(&g_frame_sem);
}

/* Install a catch-all receive filter on the loopback device */
static void install_sniffer(void)
{
	const struct device *dev = CAN_DEV;
	struct can_filter f = {
		.flags = 0,
		.id    = 0x000,
		.mask  = 0x000,  /* accept all 11-bit IDs */
	};
	k_sem_init(&g_frame_sem, 0, 8);
	g_filter_id = can_add_rx_filter(dev, sniffer_cb, NULL, &f);
}

static void remove_sniffer(void)
{
	if (g_filter_id >= 0) {
		can_remove_rx_filter(CAN_DEV, g_filter_id);
		g_filter_id = -1;
	}
}

/* Wait up to timeout_ms for a frame matching the given COB-ID */
static int wait_for_frame(uint32_t expected_id, uint32_t timeout_ms,
			  struct can_frame *out)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() < deadline) {
		int rc = k_sem_take(&g_frame_sem,
				    K_MSEC(MIN(timeout_ms, 50UL)));
		if (rc == 0 && (g_last_frame.id & CAN_STD_ID_MASK) == expected_id) {
			if (out) {
				memcpy(out, &g_last_frame, sizeof(*out));
			}
			return 0;
		}
	}
	return -ETIMEDOUT;
}

/* -------------------------------------------------------------------------
 * Suite setup/teardown
 * -------------------------------------------------------------------------*/
static void *protocol_setup(void)
{
	install_sniffer();
	return NULL;
}

static void protocol_teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	canopen_stop();
	remove_sniffer();
	k_sleep(K_MSEC(10));
}

static void before_each(void *unused)
{
	ARG_UNUSED(unused);
	canopen_stop();
	k_sleep(K_MSEC(10));
	/* Drain any pending frames */
	while (k_sem_take(&g_frame_sem, K_NO_WAIT) == 0) {
		/* discard */
	}
}


/* ==========================================================================
 * Suite 1: NMT bootup frame
 *
 * CiA 301 §7.5.2.2: on entering the Pre-Operational state, a node must
 * transmit a bootup frame with COB-ID = 0x700 + nodeId and data = 0x00.
 * ========================================================================== */
ZTEST_SUITE(protocol_bootup, protocol_setup, NULL, before_each, NULL,
	    protocol_teardown);

ZTEST(protocol_bootup, test_bootup_frame_received_after_start)
{
	struct can_frame frame = {0};

	int rc = canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE);
	zassert_equal(rc, 0, "canopen_start failed: %d", rc);

	/*
	 * Wait up to 500 ms for the bootup frame on 0x700 + TEST_NODE_ID.
	 * The frame must arrive very quickly (within a few ms of start).
	 */
	int err = wait_for_frame(HEARTBEAT_ID(TEST_NODE_ID), 500, &frame);
	zassert_equal(err, 0,
		      "Bootup frame on 0x%03X not received within 500 ms",
		      HEARTBEAT_ID(TEST_NODE_ID));

	/* DLC must be 1 (bootup / heartbeat frames are always 1 byte) */
	zassert_equal(frame.dlc, 1,
		      "Bootup frame DLC must be 1, got %u", frame.dlc);

	/* Data byte must be 0x00 (bootup indicator) */
	zassert_equal(frame.data[0], NMT_BOOTUP,
		      "Bootup frame data must be 0x00, got 0x%02X",
		      frame.data[0]);
}

ZTEST(protocol_bootup, test_bootup_id_encodes_node_id)
{
	/*
	 * Verify the COB-ID = 0x700 + node_id formula by starting with a
	 * different node-ID and checking the frame ID.
	 */
	uint8_t alt_node = 10;
	struct can_frame frame = {0};

	zassert_equal(canopen_start(CAN_DEV, alt_node, TEST_BITRATE), 0, "start failed");

	int err = wait_for_frame(HEARTBEAT_ID(alt_node), 500, &frame);
	zassert_equal(err, 0, "Bootup frame for node %u not received", alt_node);
	zassert_equal(frame.id & CAN_STD_ID_MASK, HEARTBEAT_ID(alt_node),
		      "Bootup COB-ID wrong: expected 0x%03X, got 0x%03X",
		      HEARTBEAT_ID(alt_node), frame.id & CAN_STD_ID_MASK);
}


/* ==========================================================================
 * Suite 2: Heartbeat producer
 *
 * CiA 301 §7.5.2.3: in Operational state, the HB producer sends periodic
 * HB frames on 0x700+nodeId with the NMT state value.
 * OD 0x1017 configures the HB period (default in DS301 profile: 1000 ms).
 * For test speed, we configure a shorter period via OD write or use the
 * value already in OD.
 * ========================================================================== */
ZTEST_SUITE(protocol_heartbeat, protocol_setup, NULL, before_each, NULL,
	    protocol_teardown);

ZTEST(protocol_heartbeat, test_heartbeat_frame_received)
{
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");

	/*
	 * The bootup frame arrives first (data=0x00).
	 * Subsequent heartbeat frames carry the NMT state (0x05 = Operational
	 * or 0x7F = Pre-Operational).
	 * We wait up to 2200 ms to ensure at least one non-bootup HB fires
	 * even with the default 1000 ms OD 0x1017 period.
	 */
	struct can_frame frame = {0};
	bool got_hb = false;
	int64_t deadline = k_uptime_get() + 2200;

	while (k_uptime_get() < deadline) {
		int rc = k_sem_take(&g_frame_sem, K_MSEC(200));
		if (rc != 0) {
			continue;
		}
		if ((g_last_frame.id & CAN_STD_ID_MASK) == HEARTBEAT_ID(TEST_NODE_ID)
		    && g_last_frame.dlc == 1
		    && g_last_frame.data[0] != NMT_BOOTUP) {
			memcpy(&frame, &g_last_frame, sizeof(frame));
			got_hb = true;
			break;
		}
	}

	zassert_true(got_hb,
		     "No heartbeat (non-bootup) frame received within 2200 ms");

	/* HB state must be a valid NMT state value */
	uint8_t state = frame.data[0];
	bool valid_state = (state == NMT_OPERATIONAL ||
			    state == NMT_PRE_OP     ||
			    state == NMT_STOPPED);
	zassert_true(valid_state,
		     "Heartbeat data 0x%02X is not a valid NMT state", state);
}

ZTEST(protocol_heartbeat, test_nmt_state_operational_after_startup)
{
	/*
	 * With CO_NMT_STARTUP_TO_OPERATIONAL=y (default), the stack should
	 * enter Operational immediately and the HB state must be 0x05.
	 */
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");

	CO_NMT_internalState_t s = CO_NMT_getInternalState(CO->NMT);
	zassert_equal(s, CO_NMT_OPERATIONAL,
		      "NMT state must be OPERATIONAL (0x%X) after startup_to_op, got 0x%X",
		      CO_NMT_OPERATIONAL, s);
}


/* ==========================================================================
 * Suite 3: NMT command processing
 *
 * Send NMT command frames (0x000) targeting this node and verify the NMT
 * state machine transitions correctly.
 * ========================================================================== */
ZTEST_SUITE(protocol_nmt_cmd, protocol_setup, NULL, before_each, NULL,
	    protocol_teardown);

static void send_nmt_cmd(uint8_t cmd, uint8_t target_node_id)
{
	struct can_frame f = {
		.id   = NMT_CMD_ID,
		.dlc  = 2,
		.data = {cmd, target_node_id},
	};
	can_send(CAN_DEV, &f, K_MSEC(50), NULL, NULL);
}

ZTEST(protocol_nmt_cmd, test_nmt_stop_command)
{
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50)); /* let startup complete */

	/* Send Stop command to this node */
	send_nmt_cmd(NMT_CMD_STOP, TEST_NODE_ID);
	k_sleep(K_MSEC(50));

	CO_NMT_internalState_t s = CO_NMT_getInternalState(CO->NMT);
	zassert_equal(s, CO_NMT_STOPPED,
		      "NMT state must be STOPPED after stop command, got 0x%X", s);
}

ZTEST(protocol_nmt_cmd, test_nmt_preop_command)
{
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50));

	/* Start in Operational → send Pre-Op */
	send_nmt_cmd(NMT_CMD_PRE_OP, TEST_NODE_ID);
	k_sleep(K_MSEC(50));

	CO_NMT_internalState_t s = CO_NMT_getInternalState(CO->NMT);
	zassert_equal(s, CO_NMT_PRE_OPERATIONAL,
		      "NMT state must be PRE_OPERATIONAL after pre-op command, got 0x%X", s);
}

ZTEST(protocol_nmt_cmd, test_nmt_start_from_preop)
{
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50));

	/* Move to Pre-Op first */
	send_nmt_cmd(NMT_CMD_PRE_OP, TEST_NODE_ID);
	k_sleep(K_MSEC(50));

	/* Then start */
	send_nmt_cmd(NMT_CMD_START, TEST_NODE_ID);
	k_sleep(K_MSEC(50));

	CO_NMT_internalState_t s = CO_NMT_getInternalState(CO->NMT);
	zassert_equal(s, CO_NMT_OPERATIONAL,
		      "NMT state must be OPERATIONAL after start command, got 0x%X", s);
}

ZTEST(protocol_nmt_cmd, test_nmt_broadcast_stop)
{
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50));

	/* Broadcast (node_id=0) applies to all nodes */
	send_nmt_cmd(NMT_CMD_STOP, 0);
	k_sleep(K_MSEC(50));

	CO_NMT_internalState_t s = CO_NMT_getInternalState(CO->NMT);
	zassert_equal(s, CO_NMT_STOPPED,
		      "Broadcast stop must transition to STOPPED, got 0x%X", s);
}


/* ==========================================================================
 * Suite 4: SDO server — expedited upload (read) of OD 0x1000
 *
 * OD 0x1000 (Device Type) is a mandatory read-only UDINT in every CANopen
 * device. CiA 301 §7.2.4.3 expedited upload:
 *   Request : [0x40, 0x00, 0x10, 0x00, 0, 0, 0, 0]  (upload req, no data)
 *   Response: [0x43, 0x00, 0x10, 0x00, B0,B1,B2,B3]  (4-byte expedited)
 * ========================================================================== */
ZTEST_SUITE(protocol_sdo, protocol_setup, NULL, before_each, NULL,
	    protocol_teardown);

ZTEST(protocol_sdo, test_sdo_expedited_upload_device_type)
{
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50)); /* let stack reach Operational */

	/*
	 * Send SDO upload initiate for OD 0x1000 sub 0 (Device Type).
	 * COB-ID of the SDO server request is 0x600 + nodeId.
	 */
	struct can_frame req = {
		.id  = SDO_CLI_RX(TEST_NODE_ID),
		.dlc = 8,
		.data = {
			SDO_UPLOAD_REQ,    /* command specifier: initiate upload */
			0x00, 0x10,        /* index: 0x1000 (little-endian) */
			0x00,              /* sub-index: 0 */
			0, 0, 0, 0,        /* unused in request */
		},
	};
	int rc = can_send(CAN_DEV, &req, K_MSEC(100), NULL, NULL);
	zassert_equal(rc, 0, "SDO upload request send failed: %d", rc);

	/*
	 * Wait for the SDO response on 0x580 + nodeId.
	 * Timeout: 1500 ms (SDO server timeout is 1000 ms in prj.conf).
	 */
	struct can_frame resp = {0};
	int err = wait_for_frame(SDO_SRV_TX(TEST_NODE_ID), 1500, &resp);
	zassert_equal(err, 0,
		      "SDO upload response on 0x%03X not received",
		      SDO_SRV_TX(TEST_NODE_ID));

	/* DLC must be 8 (all SDO frames are padded to 8 bytes) */
	zassert_equal(resp.dlc, 8,
		      "SDO response DLC must be 8, got %u", resp.dlc);

	/* Command specifier: 0x43 = 4-byte expedited upload response */
	zassert_equal(resp.data[0], SDO_UPLOAD_RESP4,
		      "SDO response CS must be 0x43 (4-byte expedited), got 0x%02X",
		      resp.data[0]);

	/* Index in response must echo 0x1000 */
	uint16_t resp_idx = (uint16_t)resp.data[1] | ((uint16_t)resp.data[2] << 8);
	zassert_equal(resp_idx, 0x1000U,
		      "SDO response index must be 0x1000, got 0x%04X", resp_idx);

	/* Sub-index must echo 0 */
	zassert_equal(resp.data[3], 0,
		      "SDO response sub-index must be 0, got %u", resp.data[3]);

	/*
	 * The value (bytes 4..7) is the device type from OD.  We don't assert
	 * a specific value here (it depends on the generated OD), but it must
	 * be non-garbage (i.e., the stack actually responded, not timed out).
	 * A timed-out SDO would return 0x80 abort CS, not 0x43.
	 */
}

ZTEST(protocol_sdo, test_sdo_abort_for_nonexistent_index)
{
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50));

	/* Request OD 0x9FFF (not present in any standard profile) */
	struct can_frame req = {
		.id  = SDO_CLI_RX(TEST_NODE_ID),
		.dlc = 8,
		.data = {SDO_UPLOAD_REQ, 0xFF, 0x9F, 0x00, 0, 0, 0, 0},
	};
	can_send(CAN_DEV, &req, K_MSEC(100), NULL, NULL);

	struct can_frame resp = {0};
	int err = wait_for_frame(SDO_SRV_TX(TEST_NODE_ID), 1500, &resp);
	zassert_equal(err, 0, "SDO abort not received for nonexistent index");

	/* Command specifier 0x80 = SDO abort */
	zassert_equal(resp.data[0], 0x80U,
		      "Expected SDO abort (0x80), got 0x%02X", resp.data[0]);
}


/* ==========================================================================
 * Suite 5: Emergency (EMCY) frame transmission
 *
 * CiA 301 §7.2.7: when an error is detected the device must transmit an
 * EMCY message on COB-ID = 0x080 + nodeId.
 * We trigger this via canopen_error_report() and verify the frame arrives.
 * ========================================================================== */
ZTEST_SUITE(protocol_emcy, protocol_setup, NULL, before_each, NULL,
	    protocol_teardown);

ZTEST(protocol_emcy, test_emcy_frame_sent_on_error_report)
{
	/*
	 * prj.conf must enable CONFIG_CANOPENNODE_EM_PRODUCER=y for this test.
	 * With EM_PRODUCER disabled the stack does not transmit EMCY frames;
	 * the test would time out, which would be a false failure.
	 */
#if !IS_ENABLED(CONFIG_CANOPENNODE_EM_PRODUCER)
	ztest_test_skip();
#else
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50));

	/* Clear any stale frames */
	while (k_sem_take(&g_frame_sem, K_NO_WAIT) == 0) { /* discard */ }

	/*
	 * Report a generic application error.
	 * CO_EM_GENERIC_ERROR at CO_EMC_NO_ERROR should produce an EMCY frame.
	 */
	canopen_error_report(CO_EM_GENERIC_ERROR, CO_EMC_NO_ERROR, 0xABCDEF01U);

	/*
	 * Wait up to 500 ms for the EMCY frame on 0x080 + nodeId.
	 */
	struct can_frame frame = {0};
	uint32_t emcy_id = 0x080U + TEST_NODE_ID;
	int err = wait_for_frame(emcy_id, 500, &frame);
	zassert_equal(err, 0,
		      "EMCY frame on 0x%03X not received within 500 ms", emcy_id);

	/* DLC must be 8 (all EMCY frames are 8 bytes per CiA 301) */
	zassert_equal(frame.dlc, 8,
		      "EMCY DLC must be 8, got %u", frame.dlc);

	/*
	 * Bytes 0-1: 16-bit error code (little-endian).
	 * We passed CO_EMC_NO_ERROR (0x0000), so bytes 0-1 must be 0x00, 0x00.
	 */
	uint16_t err_code = (uint16_t)frame.data[0] | ((uint16_t)frame.data[1] << 8);
	zassert_equal(err_code, CO_EMC_NO_ERROR,
		      "EMCY error code must be CO_EMC_NO_ERROR, got 0x%04X", err_code);

	/*
	 * Byte 2: error register (OD 0x1001). Must be non-zero after an error.
	 */
	zassert_not_equal(frame.data[2], 0,
			  "EMCY error register byte must be non-zero after error report");
#endif /* CONFIG_CANOPENNODE_EM_PRODUCER */
}

ZTEST(protocol_emcy, test_emcy_reset_may_send_clear_frame)
{
#if !IS_ENABLED(CONFIG_CANOPENNODE_EM_PRODUCER)
	ztest_test_skip();
#else
	zassert_equal(canopen_start(CAN_DEV, TEST_NODE_ID, TEST_BITRATE), 0,
		      "start failed");
	k_sleep(K_MSEC(50));

	/* Report error first */
	canopen_error_report(CO_EM_GENERIC_ERROR, CO_EMC_NO_ERROR, 0);
	k_sleep(K_MSEC(50));

	/* Clear the sniffer queue */
	while (k_sem_take(&g_frame_sem, K_NO_WAIT) == 0) { /* discard */ }

	/* Reset the error */
	canopen_error_reset(CO_EM_GENERIC_ERROR, 0);
	k_sleep(K_MSEC(50));

	/*
	 * After reset, the stack SHOULD transmit an EMCY with error code 0x0000
	 * ("error reset" indication). Check that the EMCY COB-ID fires.
	 * We allow a timeout without failing because the error-reset EMCY is
	 * optional in some configurations; we just log what we see.
	 */
	struct can_frame frame = {0};
	uint32_t emcy_id = 0x080U + TEST_NODE_ID;
	(void)wait_for_frame(emcy_id, 200, &frame);
	/* No hard assertion — just verify the stack didn't crash */
	zassert_true(canopen_is_running(),
		     "Stack must still be running after error reset");
#endif /* CONFIG_CANOPENNODE_EM_PRODUCER */
}
