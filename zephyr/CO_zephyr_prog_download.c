/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr backend (ops) for CANopen Program Download (CiA 302-3 oriented).
 *
 * See header for details. This file implements CO_ProgDL_StreamOps_t using
 * Zephyr flash areas and MCUBoot upgrade APIs.
 */

#include "CO_zephyr_prog_download.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(canopennode_prog_download, CONFIG_CANOPEN_LOG_LEVEL);

/* -------- Internal helpers -------- */

static bool_t z_pdl_begin(uint32_t size_hint);
static bool_t z_pdl_write(const uint8_t *data, uint32_t len);
static bool_t z_pdl_commit(void);
static void z_pdl_abort(void);
static void z_pdl_jump_to_boot(void);

/* We record a back-pointer from ops to our context. */
typedef struct {
	CO_ProgDL_Zephyr_t *zb;
} z_ctx_holder_t;

static z_ctx_holder_t g_ctx = {0}; /* One instance per app; adjust if you need multiple. */

static inline CO_ProgDL_Zephyr_t *ctx(void)
{
	return g_ctx.zb;
}

/* -------- Ops implementation -------- */

static bool_t z_open_and_erase(CO_ProgDL_Zephyr_t *zb, uint32_t size_hint)
{
	int rc;

	/* Open the flash area for the secondary slot. */
	rc = flash_area_open(zb->area_id, &zb->fa);
	if (rc != 0 || zb->fa == NULL) {
		LOG_ERR("flash_area_open(%u) failed: %d", zb->area_id, rc);
		zb->fa = NULL;
		return false;
	}

	/* Bounds check: ensure image fits in area. */
	if (size_hint != 0U && size_hint > zb->fa->fa_size) {
		LOG_ERR("size_hint (%u) exceeds area size (%u)", size_hint, zb->fa->fa_size);
		flash_area_close(zb->fa);
		zb->fa = NULL;
		return false;
	}

	/* Full erase of the target area. */
	rc = flash_area_erase(zb->fa, 0, zb->fa->fa_size);
	if (rc != 0) {
		LOG_ERR("flash_area_erase failed: %d", rc);
		flash_area_close(zb->fa);
		zb->fa = NULL;
		return false;
	}

	zb->off = 0U;
	zb->expected_size = size_hint;
	return true;
}

static bool_t z_pdl_begin(uint32_t size_hint)
{
	CO_ProgDL_Zephyr_t *zb = ctx();
	if (zb == NULL) {
		return false;
	}

	k_mutex_lock(&zb->lock, K_FOREVER);

	if (zb->active) {
		/* If a previous session is active, abort/close it. */
		if (zb->fa) {
			flash_area_close(zb->fa);
			zb->fa = NULL;
		}
		zb->active = false;
		zb->off = 0;
	}

	bool ok = z_open_and_erase(zb, size_hint);
	if (ok) {
		zb->active = true;
		LOG_INF("BEGIN OK: area=%u size_hint=%u", zb->area_id, size_hint);
	}

	k_mutex_unlock(&zb->lock);
	return ok;
}

static bool_t z_pdl_write(const uint8_t *data, uint32_t len)
{
	CO_ProgDL_Zephyr_t *zb = ctx();
	if (zb == NULL || !zb->active || zb->fa == NULL || data == NULL || len == 0U) {
		return false;
	}

	k_mutex_lock(&zb->lock, K_FOREVER);

	/* Honor device alignment if needed. */
	uint32_t align = flash_area_align(zb->fa);
	if (align > 1U) {
		/* We accept any length; Zephyr will handle internal alignment as long as
		 * controller supports it. If your SoC requires aligned writes, you can
		 * add a small staging buffer here. For most MCUBoot targets, unaligned
		 * chunks are OK.
		 */
		(void)align;
	}

	/* Check bounds if expected_size is known. */
	if (zb->expected_size && (zb->off + len > zb->expected_size)) {
		LOG_ERR("Write would exceed expected_size: off=%u len=%u exp=%u", zb->off, len,
			zb->expected_size);
		k_mutex_unlock(&zb->lock);
		return false;
	}

	int rc = flash_area_write(zb->fa, zb->off, data, len);
	if (rc != 0) {
		LOG_ERR("flash_area_write off=%u len=%u failed: %d", zb->off, len, rc);
		k_mutex_unlock(&zb->lock);
		return false;
	}

	zb->off += len;

	k_mutex_unlock(&zb->lock);
	return true;
}

static bool_t z_pdl_commit(void)
{
	CO_ProgDL_Zephyr_t *zb = ctx();
	if (zb == NULL || !zb->active) {
		return false;
	}

	k_mutex_lock(&zb->lock, K_FOREVER);

	/* Optional: sanity check if expected_size was given. */
	if (zb->expected_size && zb->off != zb->expected_size) {
		LOG_WRN("COMMIT with size mismatch: written=%u expected=%u", zb->off,
			zb->expected_size);
		/* Not fatal — MCUBoot will validate the image anyway. */
	}

	/* Close area before requesting upgrade. */
	if (zb->fa) {
		flash_area_close(zb->fa);
		zb->fa = NULL;
	}

	/* Ask MCUBoot to upgrade on next boot. */
	int rc = boot_request_upgrade(zb->permanent_upgrade);
	if (rc != 0) {
		LOG_ERR("boot_request_upgrade(permanent=%d) failed: %d",
			zb->permanent_upgrade ? 1 : 0, rc);
		zb->active = false;
		k_mutex_unlock(&zb->lock);
		return false;
	}

	/* Mark session closed. */
	zb->active = false;
	LOG_INF("COMMIT OK: bytes=%u, upgrade=%s", zb->off,
		zb->permanent_upgrade ? "permanent" : "test");

	k_mutex_unlock(&zb->lock);
	return true;
}

static void z_pdl_abort(void)
{
	CO_ProgDL_Zephyr_t *zb = ctx();
	if (zb == NULL) {
		return;
	}

	k_mutex_lock(&zb->lock, K_FOREVER);

	if (zb->fa) {
		/* Just close; we keep the area contents (already erased) as-is. */
		flash_area_close(zb->fa);
		zb->fa = NULL;
	}

	zb->active = false;
	zb->off = 0;
	zb->expected_size = 0;

	LOG_INF("ABORT done");

	k_mutex_unlock(&zb->lock);
}

static void z_pdl_jump_to_boot(void)
{
	sys_reboot(SYS_REBOOT_WARM);
}

/* -------- Public binding -------- */

int CO_zephyr_prog_download_bind(CO_ProgDL_t *pdl, CO_ProgDL_Zephyr_t *zb, uint8_t area_id,
				 bool permanent_upgrade)
{
	if (pdl == NULL || zb == NULL) {
		return -EINVAL;
	}

	(void)memset(zb, 0, sizeof(*zb));
	zb->area_id = area_id;
	zb->permanent_upgrade = permanent_upgrade;
	k_mutex_init(&zb->lock);

	/* Publish ops into PDL. */
	CO_ProgDL_StreamOps_t ops = {
		.begin = z_pdl_begin,
		.write = z_pdl_write,
		.commit = z_pdl_commit,
		.abort = z_pdl_abort,
		.jumpToBootloader = z_pdl_jump_to_boot,
	};

	g_ctx.zb = zb;

	if (CO_Prog_Download_registerStreamOps(pdl, &ops) != 0) {
		g_ctx.zb = NULL;
		return -EIO;
	}

	LOG_INF("CO_Prog_Download Zephyr backend bound: area=%u, %s", zb->area_id,
		zb->permanent_upgrade ? "permanent" : "test");
	return 0;
}
