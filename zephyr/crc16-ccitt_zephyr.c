/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 BitConcepts, LLC
 *
 * CANopenNode CRC-16/CCITT shim to Zephyr
 */

#include "301/crc16-ccitt.h"
#include <stddef.h>

/* We need to include Zephyr's crc API but avoid a symbol/type conflict:
 * Zephyr declares crc16_ccitt(seed, src, len), while CANopenNode declares
 * crc16_ccitt(block, len, crc). To prevent conflicting prototypes, we
 * temporarily remap the Zephyr declaration to ZEPHYR_crc16_ccitt.
 */
#define crc16_ccitt ZEPHYR_crc16_ccitt
#include <zephyr/sys/crc.h>
#undef crc16_ccitt

void crc16_ccitt_single(uint16_t *crc, const uint8_t chr)
{
	if (crc == NULL) {
		return;
	}
	/* Zephyr prototype: uint16_t crc16_ccitt(uint16_t seed, const uint8_t *src, size_t len); */
	*crc = ZEPHYR_crc16_ccitt(*crc, &chr, 1U);
}

uint16_t crc16_ccitt(const uint8_t block[], size_t blockLength, uint16_t crc)
{
	if ((block == NULL) && (blockLength != 0U)) {
		/* Defensive: if caller passed NULL with non-zero length, just return seed. */
		return crc;
	}
	return ZEPHYR_crc16_ccitt(crc, block, blockLength);
}
