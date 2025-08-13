/* SPDX-License-Identifier: Apache-2.0 */
/*
 * CANopenNode CRC-16/CCITT shim to Zephyr.
 *
 * Bridges CANopenNode's CRC API to Zephyr's CRC helpers by temporarily
 * renaming Zephyr's crc16_ccitt() symbol during inclusion to avoid a
 * prototype collision. Provides drop-in definitions that match the
 * CANopenNode signatures.
 *
 * @file        CO_zephyr_crc16-ccitt.c
 * @author      BitConcepts, LLC <https://github.com/BitConcepts>
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

#include "301/crc16-ccitt.h"
#include <stddef.h>

/*
 * We need to include Zephyr's CRC API but avoid a symbol/type conflict:
 * Zephyr declares:
 *     uint16_t crc16_ccitt(uint16_t seed, const uint8_t *src, size_t len);
 * while CANopenNode declares:
 *     uint16_t crc16_ccitt(const uint8_t *block, size_t len, uint16_t crc);
 *
 * To prevent conflicting prototypes, we temporarily remap the Zephyr
 * declaration to ZEPHYR_crc16_ccitt before including <zephyr/sys/crc.h>.
 */
#define crc16_ccitt ZEPHYR_crc16_ccitt
#include <zephyr/sys/crc.h>
#undef crc16_ccitt

/*
 * This companion helper matches CANopenNode’s expected single-byte update form.
 * When crc is NULL, the call is ignored.
 */
void crc16_ccitt_single(uint16_t *crc, const uint8_t chr)
{
	if (crc == NULL) {
		return;
	}
	/* Zephyr prototype: uint16_t crc16_ccitt(uint16_t seed, const uint8_t *src, size_t len); */
	*crc = ZEPHYR_crc16_ccitt(*crc, &chr, 1U);
}

/*
 * Compatible wrapper that calls into Zephyr's implementation while keeping
 * CANopenNode's parameter order.
 */
uint16_t crc16_ccitt(const uint8_t block[], size_t blockLength, uint16_t crc)
{
	if ((block == NULL) && (blockLength != 0U)) {
		/* Defensive: if caller passed NULL with non-zero length, just return seed. */
		return crc;
	}
	return ZEPHYR_crc16_ccitt(crc, block, blockLength);
}
