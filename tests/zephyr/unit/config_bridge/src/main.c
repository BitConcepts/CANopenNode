/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZTest suite: CO_zephyr_config.h — config bridge bitmask correctness.
 *
 * Purpose
 * -------
 * Verifies that the ZBIT() helper macro in CO_zephyr_config.h correctly
 * composes CO_CONFIG_* bitmasks from Kconfig symbols. This tests the
 * mapping layer that was the source of BUG-002 (wrong Kconfig symbol) and
 * BUG-003 (wrong prefix) found during the 2026-05-31 audit.
 *
 * Tests run on native_sim (no CAN hardware required).
 *
 * Run:
 *   west twister -T tests/zephyr/unit/config_bridge --platform native_sim
 *   west build -b native_sim tests/zephyr/unit/config_bridge && ./build/zephyr/zephyr.exe
 */

#include <zephyr/ztest.h>

/*
 * Pull in the config bridge directly. This is the header that translates
 * CONFIG_CANOPENNODE_* Kconfig symbols into CO_CONFIG_* bitmasks.
 */
#include "CO_zephyr_config.h"
#include "301/CO_config.h"

/* --------------------------------------------------------------------------
 * Suite: PDO bitmask composition
 *
 * These tests verify that the CO_CONFIG_PDO aggregate (defined in
 * CO_zephyr_config.h) reflects the Kconfig options selected in prj.conf.
 * -------------------------------------------------------------------------- */
ZTEST_SUITE(config_bridge_pdo, NULL, NULL, NULL, NULL, NULL);

ZTEST(config_bridge_pdo, test_pdo_rpdo_bit)
{
#if IS_ENABLED(CONFIG_CANOPENNODE_RPDO_ENABLE)
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_RPDO_ENABLE) != 0,
		     "CO_CONFIG_RPDO_ENABLE (0x01) must be set when "
		     "CONFIG_CANOPENNODE_RPDO_ENABLE=y");
#else
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_RPDO_ENABLE) == 0,
		     "CO_CONFIG_RPDO_ENABLE must be clear when RPDO disabled");
#endif
}

ZTEST(config_bridge_pdo, test_pdo_tpdo_bit)
{
#if IS_ENABLED(CONFIG_CANOPENNODE_TPDO_ENABLE)
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_TPDO_ENABLE) != 0,
		     "CO_CONFIG_TPDO_ENABLE (0x02) must be set");
#else
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_TPDO_ENABLE) == 0,
		     "CO_CONFIG_TPDO_ENABLE must be clear");
#endif
}

ZTEST(config_bridge_pdo, test_pdo_od_io_access_bit)
{
#if IS_ENABLED(CONFIG_CANOPENNODE_PDO_OD_IO_ACCESS)
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_PDO_OD_IO_ACCESS) != 0,
		     "CO_CONFIG_PDO_OD_IO_ACCESS (0x20) must be set when "
		     "CONFIG_CANOPENNODE_PDO_OD_IO_ACCESS=y");
#else
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_PDO_OD_IO_ACCESS) == 0,
		     "CO_CONFIG_PDO_OD_IO_ACCESS must be clear");
#endif
}

ZTEST(config_bridge_pdo, test_pdo_bitwise_mapping_bit)
{
	/*
	 * Regression test for the upstream PR #572 bitwise PDO mapping feature.
	 * CO_CONFIG_PDO_BITWISE_MAPPING (0x40) must be set iff Kconfig enables it
	 * AND CO_CONFIG_PDO_OD_IO_ACCESS is also set (it is a dependency).
	 */
#if IS_ENABLED(CONFIG_CANOPENNODE_PDO_BITWISE_MAPPING)
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_PDO_BITWISE_MAPPING) != 0,
		     "CO_CONFIG_PDO_BITWISE_MAPPING (0x40) must be set when "
		     "CONFIG_CANOPENNODE_PDO_BITWISE_MAPPING=y");
	/* OD_IO_ACCESS is a hard dependency of BITWISE_MAPPING */
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_PDO_OD_IO_ACCESS) != 0,
		     "CO_CONFIG_PDO_OD_IO_ACCESS must also be set when "
		     "BITWISE_MAPPING is enabled");
#else
	zassert_true((CO_CONFIG_PDO & CO_CONFIG_PDO_BITWISE_MAPPING) == 0,
		     "CO_CONFIG_PDO_BITWISE_MAPPING must be clear when disabled");
#endif
}

ZTEST(config_bridge_pdo, test_pdo_bitmask_no_spurious_bits)
{
	/*
	 * The CO_CONFIG_PDO value must only contain bits defined in CO_config.h.
	 * This catches any future typo that introduces an unrecognised bit.
	 */
	uint32_t known_pdo_bits =
		CO_CONFIG_RPDO_ENABLE        |
		CO_CONFIG_TPDO_ENABLE        |
		CO_CONFIG_RPDO_TIMERS_ENABLE |
		CO_CONFIG_TPDO_TIMERS_ENABLE |
		CO_CONFIG_PDO_SYNC_ENABLE    |
		CO_CONFIG_PDO_OD_IO_ACCESS   |
		CO_CONFIG_PDO_BITWISE_MAPPING|
		CO_CONFIG_FLAG_CALLBACK_PRE  |
		CO_CONFIG_FLAG_TIMERNEXT     |
		CO_CONFIG_FLAG_OD_DYNAMIC;

	zassert_true((CO_CONFIG_PDO & ~known_pdo_bits) == 0,
		     "CO_CONFIG_PDO contains unknown bits: 0x%08X",
		     CO_CONFIG_PDO & ~known_pdo_bits);
}

/* --------------------------------------------------------------------------
 * Suite: NMT bitmask composition
 * -------------------------------------------------------------------------- */
ZTEST_SUITE(config_bridge_nmt, NULL, NULL, NULL, NULL, NULL);

ZTEST(config_bridge_nmt, test_nmt_callback_change_bit)
{
#if IS_ENABLED(CONFIG_CANOPENNODE_NMT_CALLBACK_CHANGE)
	zassert_true((CO_CONFIG_NMT & CO_CONFIG_NMT_CALLBACK_CHANGE) != 0,
		     "CO_CONFIG_NMT_CALLBACK_CHANGE must be set");
#else
	zassert_true((CO_CONFIG_NMT & CO_CONFIG_NMT_CALLBACK_CHANGE) == 0,
		     "CO_CONFIG_NMT_CALLBACK_CHANGE must be clear");
#endif
}

ZTEST(config_bridge_nmt, test_nmt_bitmask_non_zero)
{
	/*
	 * Prj.conf enables NMT_CALLBACK_CHANGE=y and NMT_TIMERNEXT=y,
	 * so the aggregate must not be 0.
	 */
	zassert_true(CO_CONFIG_NMT != 0,
		     "CO_CONFIG_NMT must not be 0 with default prj.conf");
}

/* --------------------------------------------------------------------------
 * Suite: Storage bitmask
 *
 * Regression: BUG-003 caused the storage backend symbols to use the wrong
 * Kconfig prefix (CONFIG_CANOPEN_STORAGE_BACKEND_* instead of
 * CONFIG_CANOPENNODE_STORAGE_BACKEND_*). This test exercises the correct
 * symbol names at the Kconfig level.
 * -------------------------------------------------------------------------- */
ZTEST_SUITE(config_bridge_storage, NULL, NULL, NULL, NULL, NULL);

ZTEST(config_bridge_storage, test_storage_kconfig_symbols_defined)
{
	/*
	 * We cannot directly test the #ifdef branches in CO_zephyr_storage.c
	 * without linking it, but we can confirm that the Kconfig symbols the
	 * .c file SHOULD use are actually defined when storage is enabled.
	 *
	 * prj.conf for this test sets:
	 *   CONFIG_CANOPENNODE_STORAGE_ENABLE=y
	 *   CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM=y
	 */
#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_ENABLE)
	/*
	 * At least one backend must be selected. The wrong-prefix bug
	 * (BUG-003) caused no backend to be active even when one was chosen.
	 * After the fix, IS_ENABLED() on the correct symbol returns 1.
	 */
#if IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS)
	zassert_true(true, "Settings backend selected");
#elif IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM)
	zassert_true(true, "RAM backend selected");
#else
	zassert_true(false,
		     "CONFIG_CANOPENNODE_STORAGE_ENABLE=y but no backend "
		     "resolved — possible prefix mismatch (BUG-003 regression?)");
#endif
#else
	ztest_test_skip(); /* storage not enabled for this config */
#endif
}
