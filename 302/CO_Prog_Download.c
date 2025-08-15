/*
 * CANopen Program Download (CiA 302-3 oriented, streaming + storage-friendly)
 *
 * @file        CO_Prog_Download.c
 * @ingroup     CO_Prog_Download
 * @author      BitConcepts
 * @copyright   2025 BitConcepts
 *
 * This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.
 */

#include "CO_Prog_Download.h"

#if ((CO_CONFIG_PROG_DOWNLOAD) & CO_CONFIG_PROG_DOWNLOAD_ENABLE) != 0

#ifndef CO_PROGDL_EDS_MAX
#ifdef CO_CONFIG_PROG_DOWNLOAD_EDS_MAX_SIZE
#define CO_PROGDL_EDS_MAX ((uint32_t)CO_CONFIG_PROG_DOWNLOAD_EDS_MAX_SIZE)
#else
#define CO_PROGDL_EDS_MAX (2048)
#endif
#endif

#include <string.h>
#include "301/CO_crc16-ccitt.h"

/* If globals are requested, provide a static buffer for EDS data. */
#ifdef CO_USE_GLOBALS
static uint8_t CO_ProgDL_EDS_StaticBuf[CO_PROGDL_EDS_MAX];
#endif

/* ---------- Internal helpers ---------- */

static inline void
pdl_setStatus(CO_ProgDL_t* pdl, uint32_t set, uint32_t clr) {
    pdl->status &= ~clr;
    pdl->status |= set;
}

/* Optionally persist EDS via CO_storage after a completed transfer. */
#if ((CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE) != 0
static ODR_t
pdl_persistEDS(CO_ProgDL_t* pdl) {
    if ((pdl != NULL) && pdl->storage && pdl->edsEntry && pdl->storage->store) {
        return pdl->storage->store(pdl->edsEntry, pdl->storage->CANmodule);
    }
    return ODR_OK;
}
#endif /* (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE */

/* ---------- OD extension read/write hooks ---------- */

/* 0x1F21 Store format (RO) */
static ODR_t
OD_read_1F21(OD_stream_t* stream, void* buf, OD_size_t count, OD_size_t* countRead) {
    (void)buf;
    (void)count;

    if ((buf == NULL) || (stream == NULL) || (countRead == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    uint32_t fmt = CO_PROGDL_STORE_FMT_PROG;
    return OD_readOriginal(stream, &fmt, sizeof(fmt), countRead);
}

/* 0x1F24 Store format EDS NMT slave (RO) */
static ODR_t
OD_read_1F24(OD_stream_t* stream, void* buf, OD_size_t count, OD_size_t* countRead) {
    (void)buf;
    (void)count;

    if ((buf == NULL) || (stream == NULL) || (countRead == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    uint32_t fmt = CO_PROGDL_STORE_FMT_EDS;
    return OD_readOriginal(stream, &fmt, sizeof(fmt), countRead);
}

/* 0x1F56 Program software identification (RW VISIBLE_STRING) */
static ODR_t
OD_read_1F56(OD_stream_t* stream, void* buf, OD_size_t count, OD_size_t* countRead) {
    if ((buf == NULL) || (stream == NULL) || (countRead == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    CO_ProgDL_t* pdl = (CO_ProgDL_t*)stream->object;
    size_t len = strnlen(pdl->swId, sizeof(pdl->swId));
    return OD_readOriginal(stream, pdl->swId, (OD_size_t)len, countRead);
}

static ODR_t
OD_write_1F56(OD_stream_t* stream, const void* buf, OD_size_t count, OD_size_t* countWritten) {
    if ((stream == NULL) || (buf == NULL) || (countWritten == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    CO_ProgDL_t* pdl = (CO_ProgDL_t*)stream->object;
    size_t n = (size_t)((count < sizeof(pdl->swId) - 1) ? count : (sizeof(pdl->swId) - 1));
    (void)memset(pdl->swId, 0, sizeof(pdl->swId));
    (void)memcpy(pdl->swId, buf, n);
    if (countWritten) {
        *countWritten = (OD_size_t)n;
    }
    return ODR_OK;
}

/* 0x1F57 Flash status identification (RO U32) */
static ODR_t
OD_read_1F57(OD_stream_t* stream, void* buf, OD_size_t count, OD_size_t* countRead) {
    if ((buf == NULL) || (stream == NULL) || (countRead == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    CO_ProgDL_t* pdl = (CO_ProgDL_t*)stream->object;
    return OD_readOriginal(stream, &pdl->status, sizeof(pdl->status), countRead);
}

/* 0x1F50 Program data – forward chunks to streaming backend and update CRC16-CCITT */
static ODR_t
OD_write_1F50(OD_stream_t* stream, const void* buf, OD_size_t count, OD_size_t* countWritten) {
    if ((stream == NULL) || (buf == NULL) || (countWritten == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    CO_ProgDL_t* pdl = (CO_ProgDL_t*)stream->object;
    if (!pdl->sessionOpen) {
        return ODR_NO_RESOURCE; /* require BEGIN */
    }
    if (!pdl->ops.write) {
        return ODR_GENERAL;
    }

    if (!pdl->ops.write((const uint8_t*)buf, (uint32_t)count)) {
        pdl_setStatus(pdl, CO_PROGDL_STATUS_ERROR, 0);
        return ODR_HW;
    }

    pdl->bytesReceived += (uint32_t)count;
    pdl->runningCRC16 = CO_crc16_ccitt((const uint8_t*)buf, (size_t)count, pdl->runningCRC16);
    pdl_setStatus(pdl, CO_PROGDL_STATUS_RECEIVING | CO_PROGDL_STATUS_WRITING, 0);

    if (countWritten) {
        *countWritten = count;
    }
    return ODR_OK;
}

/* 0x1F51 Program control (U32) */
static ODR_t
OD_write_1F51(OD_stream_t* stream, const void* buf, OD_size_t count, OD_size_t* countWritten) {
    (void)count;

    if ((stream == NULL) || (buf == NULL) || (countWritten == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    CO_ProgDL_t* pdl = (CO_ProgDL_t*)stream->object;
    uint32_t cmd = 0;
    (void)memcpy(&cmd, buf, sizeof(uint32_t));

    switch (cmd) {
        case CO_PROGDL_CTRL_ABORT:
            if (pdl->ops.abort) {
                pdl->ops.abort();
            }
            pdl->sessionOpen = false;
            pdl->bytesReceived = 0;
            pdl->runningCRC16 = 0;
            pdl->imageSizeHint = 0;
            pdl_setStatus(pdl, 0,
                          CO_PROGDL_STATUS_RECEIVING | CO_PROGDL_STATUS_WRITING | CO_PROGDL_STATUS_ERASING
                              | CO_PROGDL_STATUS_VERIFYING | CO_PROGDL_STATUS_DONE_OK | CO_PROGDL_STATUS_ERROR);
            break;

        case CO_PROGDL_CTRL_BEGIN:
            if (!pdl->ops.begin) {
                return ODR_GENERAL;
            }
            pdl_setStatus(pdl, CO_PROGDL_STATUS_ERASING, CO_PROGDL_STATUS_DONE_OK | CO_PROGDL_STATUS_ERROR);
            if (!pdl->ops.begin(pdl->imageSizeHint)) {
                pdl_setStatus(pdl, CO_PROGDL_STATUS_ERROR, CO_PROGDL_STATUS_ERASING);
                return ODR_HW;
            }
            pdl->sessionOpen = true;
            pdl->bytesReceived = 0;
            pdl->runningCRC16 = 0;
            pdl_setStatus(pdl, CO_PROGDL_STATUS_RECEIVING, CO_PROGDL_STATUS_ERASING);
            break;

        case CO_PROGDL_CTRL_COMMIT:
            if (!pdl->sessionOpen) {
                return ODR_INVALID_VALUE;
            }
            if (!pdl->ops.commit) {
                return ODR_GENERAL;
            }
            pdl_setStatus(pdl, CO_PROGDL_STATUS_VERIFYING, CO_PROGDL_STATUS_RECEIVING | CO_PROGDL_STATUS_WRITING);
            if (!pdl->ops.commit()) {
                pdl_setStatus(pdl, CO_PROGDL_STATUS_ERROR, CO_PROGDL_STATUS_VERIFYING);
                return ODR_HW;
            }
            /* Optionally persist via CO_storage (e.g. mark/program metadata) */
#if ((CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE) != 0
            if (pdl->storage && pdl->fwEntry && pdl->storage->store) {
                ODR_t r = pdl->storage->store(pdl->fwEntry, pdl->storage->CANmodule);
                if (r != ODR_OK) {
                    pdl_setStatus(pdl, CO_PROGDL_STATUS_ERROR, CO_PROGDL_STATUS_DONE_OK);
                    return ODR_HW;
                }
            }
#endif
            pdl->sessionOpen = false;
            pdl_setStatus(pdl, CO_PROGDL_STATUS_DONE_OK, CO_PROGDL_STATUS_VERIFYING);
            break;

        case CO_PROGDL_CTRL_JUMP_BOOT:
            if (pdl->ops.jumpToBootloader) {
                pdl->ops.jumpToBootloader();
            }
            return ODR_GENERAL; /* Should not return if jump was successful */

        default: return ODR_INVALID_VALUE;
    }

    if (countWritten) {
        *countWritten = sizeof(uint32_t);
    }
    return ODR_OK;
}

/* 0x1F23 Store EDS NMT slave – buffered here; app can persist via CO_storage */
static ODR_t
OD_write_1F23(OD_stream_t* stream, const void* buf, OD_size_t count, OD_size_t* countWritten) {
    if ((stream == NULL) || (buf == NULL) || (countWritten == NULL)) {
        return ODR_DEV_INCOMPAT;
    }

    CO_ProgDL_t* pdl = (CO_ProgDL_t*)stream->object;

#ifdef CO_USE_GLOBALS
    /* Use statically defined buffer; initialize it lazily if needed */
    if (pdl->edsBuf == NULL) {
        pdl->edsBuf = CO_ProgDL_EDS_StaticBuf;
        pdl->edsLen = 0;
    }
#else
    /* Allocate from heap on first use */
    if (pdl->edsBuf == NULL) {
        pdl->edsBuf = (uint8_t*)CO_alloc((size_t)CO_PROGDL_EDS_MAX);
        if (pdl->edsBuf == NULL) {
            return ODR_OUT_OF_MEM;
        }
        pdl->edsLen = 0;
    }
#endif

    if (pdl->edsLen + count > CO_PROGDL_EDS_MAX) {
        return ODR_DATA_LONG;
    }
    (void)memcpy(&pdl->edsBuf[pdl->edsLen], buf, count);
    pdl->edsLen += (uint32_t)count;
    if (countWritten) {
        *countWritten = count;
    }
    return ODR_OK;
}

/* ---------- Initialization / API ---------- */

static void
attach_ext(CO_ProgDL_t* pdl, OD_extension_t* ext, uint16_t idx, void* rd, void* wr) {
    OD_entry_t* entry = OD_find(pdl->co->SDOserver->OD, idx);
    if (entry) {
        ext->read = rd;
        ext->write = wr;
        (void)OD_extension_init(entry, ext);
    }
}

CO_ReturnError_t
CO_Prog_Download_init(CO_ProgDL_t* pdl, CO_t* co) {
    if ((pdl == NULL) || (co == NULL)) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    (void)memset(pdl, 0, sizeof(*pdl));
    pdl->co = co;
    pdl->status = CO_PROGDL_STATUS_IDLE;

#ifdef CO_USE_GLOBALS
    /* Pre-bind the static buffer for EDS. */
    pdl->edsBuf = CO_ProgDL_EDS_StaticBuf;
    pdl->edsLen = 0;
#endif

    attach_ext(pdl, &pdl->ext_1F21, 0x1F21, OD_read_1F21, NULL);
    attach_ext(pdl, &pdl->ext_1F24, 0x1F24, OD_read_1F24, NULL);
    attach_ext(pdl, &pdl->ext_1F56, 0x1F56, OD_read_1F56, OD_write_1F56);
    attach_ext(pdl, &pdl->ext_1F57, 0x1F57, OD_read_1F57, NULL);
    attach_ext(pdl, &pdl->ext_1F50, 0x1F50, NULL, OD_write_1F50);
    attach_ext(pdl, &pdl->ext_1F51, 0x1F51, NULL, OD_write_1F51);
    attach_ext(pdl, &pdl->ext_1F23, 0x1F23, NULL, OD_write_1F23);

    return CO_ERROR_NO;
}

void
CO_Prog_Download_deinit(CO_ProgDL_t* pdl) {
    if (pdl == NULL) {
        return;
    }
#ifndef CO_USE_GLOBALS
    if (pdl->edsBuf) {
        CO_free(pdl->edsBuf);
        pdl->edsBuf = NULL;
    }
#else
    /* Using static storage: nothing to free, just clear length. */
    pdl->edsLen = 0;
#endif
}

int
CO_Prog_Download_registerStreamOps(CO_ProgDL_t* pdl, const CO_ProgDL_StreamOps_t* ops) {
    if ((pdl == NULL) || (ops == NULL)) {
        return -1;
    }
    pdl->ops = *ops;
    return 0;
}

void
CO_Prog_Download_setImageSizeHint(CO_ProgDL_t* pdl, uint32_t size_hint) {
    if (pdl == NULL) {
        return;
    }
    pdl->imageSizeHint = size_hint;
}

#if ((CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE) != 0
void
CO_Prog_Download_bindStorage(CO_ProgDL_t* pdl, CO_storage_t* storage, CO_storage_entry_t* fwEntry,
                             CO_storage_entry_t* edsEntry) {
    if (pdl == NULL) {
        return;
    }
    pdl->storage = storage;
    pdl->fwEntry = fwEntry;
    pdl->edsEntry = edsEntry;
}

ODR_t
CO_Prog_Download_persistEDS(CO_ProgDL_t* pdl) {
    return pdl_persistEDS(pdl);
}
#endif /* (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE */
#endif /* (CO_CONFIG_PROG_DOWNLOAD) & CO_CONFIG_PROG_DOWNLOAD_ENABLE */
