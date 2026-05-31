/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZTest suite: CANopenNode Object Dictionary interface (CO_ODinterface).
 *
 * These tests do NOT require CAN hardware or the full CANopen stack to be
 * started. They exercise the OD data access functions directly using the
 * generated OD.h/OD.c from the example DS301 profile.
 *
 * Tested functions
 * ----------------
 *   OD_find()          — look up an OD entry by index
 *   OD_getSub()        — get a sub-entry IO descriptor
 *   OD_get_u32()       — fast read of UDINT variable
 *   OD_get_u16()       — fast read of UINT variable
 *   OD_get_u8()        — fast read of USINT variable
 *   OD_set_u32()       — fast write of UDINT variable (writable OD vars)
 *   Read-only reject   — write to const/read-only entry returns ODR_UNSUPP_ACCESS
 *   Index not found    — OD_find() on missing index returns NULL
 *
 * Platforms: native_sim, qemu_x86, qemu_cortex_m3 (no CAN required)
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include "301/CO_ODinterface.h"
#include "OD.h"

/*
 * Standard DS301 OD indices tested:
 *   0x1000  Device Type          VAR  UDINT   ro / const
 *   0x1017  Producer Heartbeat   VAR  UINT    rw
 *   0x1018  Identity             RECORD
 */

/* ==========================================================================
 * Suite: OD_find()
 * ========================================================================== */
ZTEST_SUITE(od_find, NULL, NULL, NULL, NULL, NULL);

ZTEST(od_find, test_find_mandatory_index_1000)
{
	OD_entry_t *e = OD_find(OD, 0x1000);
	zassert_not_null(e, "OD_find(0x1000) must return non-NULL");
}

ZTEST(od_find, test_find_mandatory_index_1001)
{
	OD_entry_t *e = OD_find(OD, 0x1001);
	zassert_not_null(e, "OD_find(0x1001) must return non-NULL");
}

ZTEST(od_find, test_find_identity_index_1018)
{
	OD_entry_t *e = OD_find(OD, 0x1018);
	zassert_not_null(e, "OD_find(0x1018) must return non-NULL");
}

ZTEST(od_find, test_find_nonexistent_returns_null)
{
	OD_entry_t *e = OD_find(OD, 0x9999);
	zassert_is_null(e,
		"OD_find(0x9999) must return NULL for non-existent index");
}

ZTEST(od_find, test_find_index_zero_returns_null)
{
	OD_entry_t *e = OD_find(OD, 0x0000);
	zassert_is_null(e, "OD_find(0x0000) must return NULL");
}

ZTEST(od_find, test_od_pointer_not_null)
{
	zassert_not_null(OD, "Global OD pointer must not be NULL");
}

ZTEST(od_find, test_od_list_count_nonzero)
{
	zassert_true(OD->size > 0,
		     "OD->size must be > 0, got %u", OD->size);
}


/* ==========================================================================
 * Suite: OD read functions (OD_get_*)
 * ========================================================================== */
ZTEST_SUITE(od_read, NULL, NULL, NULL, NULL, NULL);

ZTEST(od_read, test_get_u32_device_type)
{
	/*
	 * OD 0x1000 (Device Type) is UDINT read via OD_get_u32().
	 * Value is profile-specific; we only care that the read succeeds.
	 */
	OD_entry_t *e = OD_find(OD, 0x1000);
	zassert_not_null(e, "OD 0x1000 not found");

	uint32_t val = 0;
	ODR_t ret = OD_get_u32(e, 0, &val, true);
	zassert_equal(ret, ODR_OK,
		      "OD_get_u32(0x1000) failed: %d", ret);
	/* Value can be 0 for a generic profile — just assert the call succeeded */
}

ZTEST(od_read, test_get_u16_heartbeat_period)
{
	/*
	 * OD 0x1017 (Producer Heartbeat Time) is UINT.
	 * It may be 0 (HB disabled) or a positive period in ms.
	 */
	OD_entry_t *e = OD_find(OD, 0x1017);
	zassert_not_null(e, "OD 0x1017 not found");

	uint16_t period = 0xFFFF;
	ODR_t ret = OD_get_u16(e, 0, &period, true);
	zassert_equal(ret, ODR_OK,
		      "OD_get_u16(0x1017) failed: %d", ret);
}

ZTEST(od_read, test_get_u8_error_register)
{
	/*
	 * OD 0x1001 (Error Register) is USINT.
	 * Must be 0 on a freshly initialized OD (no errors).
	 */
	OD_entry_t *e = OD_find(OD, 0x1001);
	zassert_not_null(e, "OD 0x1001 not found");

	uint8_t reg = 0xFF;
	ODR_t ret = OD_get_u8(e, 0, &reg, true);
	zassert_equal(ret, ODR_OK,
		      "OD_get_u8(0x1001) failed: %d", ret);
	zassert_equal(reg, 0,
		      "Error register must be 0 on fresh OD, got 0x%02X", reg);
}

ZTEST(od_read, test_get_sub_invalid_subindex_fails)
{
	/*
	 * OD 0x1000 is a VAR — it has only sub-index 0.
	 * Requesting sub-index 1 must return ODR_SUB_NOT_EXIST.
	 */
	OD_entry_t *e = OD_find(OD, 0x1000);
	zassert_not_null(e, "OD 0x1000 not found");

	uint32_t val = 0;
	ODR_t ret = OD_get_u32(e, 1, &val, true);
	zassert_equal(ret, ODR_SUB_NOT_EXIST,
		      "Sub-index 1 on VAR must return ODR_SUB_NOT_EXIST, got %d", ret);
}

ZTEST(od_read, test_get_with_null_variable_fails)
{
	OD_entry_t *e = OD_find(OD, 0x1000);
	zassert_not_null(e, "OD 0x1000 not found");

	ODR_t ret = OD_get_u32(e, 0, NULL, true);
	/* Expect a graceful failure (not a crash) */
	zassert_not_equal(ret, ODR_OK,
			  "OD_get_u32 with NULL variable must fail");
}


/* ==========================================================================
 * Suite: OD write functions (OD_set_*)
 * ========================================================================== */
ZTEST_SUITE(od_write, NULL, NULL, NULL, NULL, NULL);

ZTEST(od_write, test_set_u16_heartbeat_period)
{
	/*
	 * OD 0x1017 is read-write. Write a new period and read it back.
	 */
	OD_entry_t *e = OD_find(OD, 0x1017);
	zassert_not_null(e, "OD 0x1017 not found");

	/* Read original value so we can restore it */
	uint16_t original = 0;
	OD_get_u16(e, 0, &original, true);

	/* Write a new value */
	uint16_t new_val = 500U;
	ODR_t ret = OD_set_u16(e, 0, new_val, true);
	zassert_equal(ret, ODR_OK,
		      "OD_set_u16(0x1017, 500) failed: %d", ret);

	/* Read back and verify */
	uint16_t read_back = 0;
	OD_get_u16(e, 0, &read_back, true);
	zassert_equal(read_back, new_val,
		      "Readback after write: expected %u, got %u",
		      new_val, read_back);

	/* Restore */
	OD_set_u16(e, 0, original, true);
}

ZTEST(od_write, test_write_readonly_entry_fails)
{
	/*
	 * OD 0x1000 (Device Type) has accessType=const/ro.
	 * Writing to it must be rejected.
	 *
	 * OD_set_u32() with orig=false goes via the IO path which checks
	 * write access. With orig=true it bypasses — use false here.
	 */
	OD_entry_t *e = OD_find(OD, 0x1000);
	zassert_not_null(e, "OD 0x1000 not found");

	ODR_t ret = OD_set_u32(e, 0, 0x12345678U, false);
	zassert_not_equal(ret, ODR_OK,
			  "Write to read-only OD 0x1000 must fail");
}

ZTEST(od_write, test_write_nonexistent_index_fails)
{
	OD_entry_t *e = OD_find(OD, 0x9ABC);
	zassert_is_null(e, "0x9ABC should not exist");

	/* Calling OD_set_u32(NULL, ...) must not crash */
	ODR_t ret = OD_set_u32(NULL, 0, 42, true);
	zassert_not_equal(ret, ODR_OK,
			  "OD_set_u32(NULL) must not return ODR_OK");
}


/* ==========================================================================
 * Suite: OD_getSub() — IO descriptor access
 * ========================================================================== */
ZTEST_SUITE(od_getsub, NULL, NULL, NULL, NULL, NULL);

ZTEST(od_getsub, test_getsub_valid_sub_returns_ok)
{
	OD_entry_t *e = OD_find(OD, 0x1000);
	zassert_not_null(e, "0x1000 not found");

	OD_IO_t io;
	ODR_t ret = OD_getSub(e, 0, &io, false);
	zassert_equal(ret, ODR_OK,
		      "OD_getSub(0x1000, 0) failed: %d", ret);
	zassert_true(io.stream.dataLength > 0,
		     "dataLength must be > 0");
}

ZTEST(od_getsub, test_getsub_null_entry_fails)
{
	OD_IO_t io;
	ODR_t ret = OD_getSub(NULL, 0, &io, false);
	zassert_not_equal(ret, ODR_OK,
			  "OD_getSub(NULL) must fail");
}

ZTEST(od_getsub, test_getsub_device_type_data_length_4)
{
	/*
	 * Device Type (0x1000) is UDINT = 4 bytes.
	 */
	OD_entry_t *e = OD_find(OD, 0x1000);
	zassert_not_null(e, "0x1000 not found");

	OD_IO_t io;
	OD_getSub(e, 0, &io, false);
	zassert_equal(io.stream.dataLength, 4U,
		      "Device type data length must be 4, got %u",
		      io.stream.dataLength);
}
