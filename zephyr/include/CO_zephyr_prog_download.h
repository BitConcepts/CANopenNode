/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr backend (ops) for CANopen Program Download (CiA 302-3 oriented).
 *
 * Streams incoming program bytes to the MCUBoot secondary slot via Zephyr's
 * flash area API, then marks the image for upgrade on COMMIT.
 *
 * Requirements:
 *   - CONFIG_BOOTLOADER_MCUBOOT=y
 *   - CONFIG_FLASH=y
 *   - CONFIG_FLASH_MAP=y
 *   - CONFIG_IMG_MANAGER=y (recommended)
 *
 * Optional:
 *   - CONFIG_CO_PROGDL_ZEPHYR_PERMANENT=y to make upgrade permanent immediately
 *     (otherwise marks image as "test" and lets app confirm after self-test).
 */

/**
 * @section CO_ProgDL_ZephyrPartition Zephyr partition selection via Devicetree alias
 *
 * To select which flash partition CO_Prog_Download programs, define a Devicetree
 * **alias** and reference it from code. This avoids fragile Kconfig strings and allows
 * board- or app-specific DTS overlays to choose the target cleanly.
 *
 * ### Devicetree (board or application overlay)
 * @code{.dts}
 * / {
 *     aliases {
 *         // Point Program Download at the partition to be programmed
 *         can-progdl-partition = &image_1;   // or &image_0, &slot1, etc.
 *     };
 * };
 * @endcode
 *
 * ### C usage
 * In your Zephyr integration (e.g. the port glue that implements begin/write/commit),
 * use the alias to resolve the partition node:
 * @code{.c}
 * #include <zephyr/devicetree.h>
 *
 * #if DT_NODE_HAS_STATUS(DT_ALIAS(can_progdl_partition), okay)
 *   #define CO_PROGDL_PART_NODE  DT_ALIAS(can_progdl_partition)
 * #else
 *   #error "DT alias 'can-progdl-partition' is not defined or not okay"
 * #endif
 *
 * // Example: get partition ID for flash_area_open() or FIXED_PARTITION_ID()
 * #define CO_PROGDL_PARTITION_ID FIXED_PARTITION_ID(CO_PROGDL_PART_NODE)
 * @endcode
 *
 * With	this approach, changing the programmed area is a one - line change in DTS.
 * No Kconfig strings are required, and the C preprocessor receives a proper
 * Devicetree **token** (not a string), which is compatible with DT macros.
 */

#ifndef ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_PROG_DOWNLOAD_H
#define ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_PROG_DOWNLOAD_H

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include <stddef.h>

#include "302/CO_Prog_Download.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_CO_PROGDL_ZEPHYR_FLASH_AREA_ID
#define CONFIG_CO_PROGDL_ZEPHYR_FLASH_AREA_ID FLASH_AREA_IMAGE_SECONDARY
#endif

/* Backend context; one instance per PDL. */
typedef struct {
	/* Target flash area for the new image (normally image_1 / secondary). */
	uint8_t area_id;

	/* Flash area handle (opened on BEGIN, closed on ABORT/COMMIT). */
	const struct flash_area *fa;

	/* Running byte offset within the area. */
	uint32_t off;

	/* Expected total image size (hint from PDL). */
	uint32_t expected_size;

	/* True if BEGIN has successfully opened/erased. */
	bool active;

	/* If true, use boot_request_upgrade(permanent=1); else test upgrade. */
	bool permanent_upgrade;

	/* Optional: guard concurrent access if your SDO path is multithreaded. */
	struct k_mutex lock;
} CO_ProgDL_Zephyr_t;

/**
 * Initialize a Zephyr backend and register the stream ops into CO_ProgDL.
 *
 * @param pdl                The CO_ProgDL instance (already initialized).
 * @param zb                 Backend storage (caller supplies memory).
 * @param area_id            Flash area id (e.g., FLASH_AREA_ID(image_1)).
 * @param permanent_upgrade  If true, request a permanent upgrade at COMMIT.
 * @return 0 on success, negative on error.
 */
int CO_zephyr_prog_download_bind(CO_ProgDL_t *pdl, CO_ProgDL_Zephyr_t *zb, uint8_t area_id,
				 bool permanent_upgrade);

/* Convenience: bind with defaults from Kconfig. */
static inline int CO_Prog_Download_zephyr_bind_default(CO_ProgDL_t *pdl, CO_ProgDL_Zephyr_t *zb)
{
	return CO_zephyr_prog_download_bind(pdl, zb, CONFIG_CO_PROGDL_ZEPHYR_FLASH_AREA_ID,
					    CONFIG_CANOPENNODE_PROG_DOWNLOAD_PERMANENT ? true
										       : false);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_PROG_DOWNLOAD_H */
