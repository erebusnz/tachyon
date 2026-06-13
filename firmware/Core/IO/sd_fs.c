#include "sd_fs.h"
#include "ff.h"
#include "main.h"
#include <stdio.h>

extern SD_HandleTypeDef hsd;                 /* main.c */
extern volatile int      sd_diskio_last_stage;  /* sd_diskio.c */
extern volatile uint32_t sd_diskio_last_hal;
extern volatile uint32_t sd_diskio_last_err;

/* Concise SD/HAL state report, printed when a mount fails. */
static void sd_diag(void)
{
    int present = (HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) == GPIO_PIN_SET);
    printf("  diag: cd_present=%d card_state=%lu\r\n",
           present, (unsigned long)HAL_SD_GetCardState(&hsd));

    HAL_SD_CardInfoTypeDef ci;
    if (HAL_SD_GetCardInfo(&hsd, &ci) == HAL_OK) {
        printf("  diag: type=%lu blocks=%lu blksz=%lu\r\n",
               (unsigned long)ci.CardType, (unsigned long)ci.BlockNbr,
               (unsigned long)ci.BlockSize);
    } else {
        printf("  diag: GetCardInfo failed\r\n");
    }
    printf("  diag: last disk_read stage=%d hal=%lu err=0x%08lx\r\n",
           sd_diskio_last_stage, (unsigned long)sd_diskio_last_hal,
           (unsigned long)sd_diskio_last_err);
}

/* Volume work area. Word-aligned so the driver's aligned-fast path is taken
 * for FatFs' internal window/FAT reads. */
static FATFS s_fs __attribute__((aligned(4)));
static bool  s_mounted;

static const char *fs_type_name(BYTE t)
{
    switch (t) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
    case FS_EXFAT: return "exFAT";
    default:       return "none";
    }
}

bool sd_fs_mount(void)
{
    FRESULT fr = f_mount(&s_fs, "0:", 1 /* mount immediately */);
    s_mounted = (fr == FR_OK);
    if (s_mounted) {
        printf("SD mounted: %s\r\n", fs_type_name(s_fs.fs_type));
    } else {
        printf("SD mount failed (fr=%d)\r\n", (int)fr);
        sd_diag();
    }
    return s_mounted;
}

bool sd_fs_mounted(void)
{
    return s_mounted;
}

void sd_fs_dump_root(void)
{
    if (!s_mounted) {
        printf("SD not mounted\r\n");
        return;
    }

    DIR     dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK) {
        printf("opendir failed (fr=%d)\r\n", (int)fr);
        return;
    }

    int folders = 0, files = 0;
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;   /* error or end */
        if (fno.fattrib & AM_DIR) {
            folders++;
            printf("[DIR] %s\r\n", fno.fname);
        } else {
            files++;
        }
    }
    f_closedir(&dir);
    printf("root: %d folders, %d files\r\n", folders, files);
}
