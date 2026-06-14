/* FatFs low-level disk glue for the SDIO MicroSD slot via STM32 HAL_SD.
 *
 * Single physical drive (pdrv 0) = the SD card. HAL_SD is brought up in main.c
 * (MX_SDIO_SD_Init, gated on card-detect) before the filesystem is mounted, so
 * disk_initialize here only validates the card is present and in transfer
 * state — it does not re-run HAL_SD_Init.
 *
 * Polling (non-DMA) block transfers: the data volume is tiny (whole wavetable
 * folder < 10 KB) and polling sidesteps cache/alignment concerns. Reads land in
 * an aligned bounce buffer when the caller's buffer is not 32-bit aligned, since
 * the HAL FIFO path does word-wide stores. Read-only build: disk_write is
 * compiled out by FF_FS_READONLY. */

#include "ff.h"
#include "diskio.h"
#include "main.h"
#include <string.h>
#include <stdint.h>

extern SD_HandleTypeDef hsd;   /* defined in main.c */

#define SD_PDRV        0
#define SD_TIMEOUT_MS  1000U

static volatile DSTATUS s_stat = STA_NOINIT;

/* Diagnostics for bring-up: last disk_read failure detail. stage: 1=wait before
 * read, 2=HAL read call, 3=wait after read. */
volatile int      sd_diskio_last_stage = 0;
volatile uint32_t sd_diskio_last_hal   = 0;
volatile uint32_t sd_diskio_last_err   = 0;

/* 512-byte, word-aligned bounce buffer for unaligned caller buffers. */
static uint32_t s_bounce[FF_MAX_SS / 4];

static int sd_present(void)
{
    return HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) == GPIO_PIN_SET;
}

/* Spin until the card returns to transfer state (or timeout). */
static int sd_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - start) >= timeout_ms) return 0;
    }
    return 1;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != SD_PDRV) return STA_NOINIT;
    if (!sd_present())   return STA_NOINIT | STA_NODISK;
    return s_stat;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != SD_PDRV) return STA_NOINIT;
    if (!sd_present())   return STA_NOINIT | STA_NODISK;

    /* HAL_SD_Init already ran in main.c; just confirm the card is usable. */
    if (!sd_wait_ready(SD_TIMEOUT_MS)) return STA_NOINIT;

    s_stat = 0;
    return s_stat;
}

/* Read blocks with the audio I2S-DMA refill IRQ masked. The SDIO read is
 * polling-mode in thread context; the audio render IRQ (DMA1_Stream5) would
 * otherwise preempt the FIFO-drain loop long enough to RX-overrun at the slow
 * polling clock, corrupting reads (e.g. garbled directory listings). The DMA
 * keeps clocking the DAC during the sub-millisecond transfer; only the CPU-side
 * refill pauses, so audio just isn't topped up for that brief window. */
static HAL_StatusTypeDef sd_read_blocks(uint8_t *buf, uint32_t sector, uint32_t count)
{
    HAL_NVIC_DisableIRQ(DMA1_Stream5_IRQn);
    HAL_StatusTypeDef hs = HAL_SD_ReadBlocks(&hsd, buf, sector, count, SD_TIMEOUT_MS);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    return hs;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SD_PDRV)   return RES_PARERR;
    if (s_stat & STA_NOINIT) return RES_NOTRDY;
    if (count == 0)        return RES_PARERR;

    if (((uintptr_t)buff & 3U) == 0U) {
        /* Aligned: read all blocks in one HAL call. */
        HAL_StatusTypeDef hs;
        if (!sd_wait_ready(SD_TIMEOUT_MS)) {
            sd_diskio_last_stage = 1; sd_diskio_last_err = hsd.ErrorCode;
            return RES_NOTRDY;
        }
        hs = sd_read_blocks(buff, (uint32_t)sector, count);
        if (hs != HAL_OK) {
            sd_diskio_last_stage = 2; sd_diskio_last_hal = hs;
            sd_diskio_last_err = hsd.ErrorCode;
            return RES_ERROR;
        }
        if (!sd_wait_ready(SD_TIMEOUT_MS)) {
            sd_diskio_last_stage = 3; sd_diskio_last_err = hsd.ErrorCode;
            return RES_ERROR;
        }
        return RES_OK;
    }

    /* Unaligned caller buffer: stage one block at a time through the bounce. */
    for (UINT i = 0; i < count; i++) {
        if (!sd_wait_ready(SD_TIMEOUT_MS)) return RES_NOTRDY;
        if (sd_read_blocks((uint8_t *)s_bounce, (uint32_t)sector + i, 1) != HAL_OK) {
            return RES_ERROR;
        }
        if (!sd_wait_ready(SD_TIMEOUT_MS)) return RES_ERROR;
        memcpy(buff + (size_t)i * FF_MAX_SS, s_bounce, FF_MAX_SS);
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != SD_PDRV) return RES_PARERR;
    if (s_stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;   /* read-only: nothing buffered */

    case GET_SECTOR_COUNT: {
        HAL_SD_CardInfoTypeDef info;
        if (HAL_SD_GetCardInfo(&hsd, &info) != HAL_OK) return RES_ERROR;
        *(LBA_t *)buff = info.LogBlockNbr;
        return RES_OK;
    }

    case GET_SECTOR_SIZE:
        *(WORD *)buff = FF_MAX_SS;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;   /* erase-block unit unknown; mkfs disabled */
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
