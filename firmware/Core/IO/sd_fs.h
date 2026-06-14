#ifndef SD_FS_H
#define SD_FS_H

#include <stdbool.h>

/* SD-card filesystem access (FatFs over SDIO/HAL_SD).
 *
 * The card is brought up by MX_SDIO_SD_Init() in main.c; call sd_fs_mount()
 * once after that to mount the single FAT/exFAT volume. All FatFs access must
 * happen from the main loop (the driver is not re-entrant). */

/* Mount the SD volume ("0:"). Returns true on success. Safe to call when no
 * card is present (returns false). */
bool sd_fs_mount(void);

/* True once a volume is mounted. */
bool sd_fs_mounted(void);

/* Phase-1 bring-up aid: enumerate the card root over USB-CDC printf, listing
 * each sub-directory (one wavetable folder per line) and a folder/file tally. */
void sd_fs_dump_root(void);

/* Max stored length of a wavetable folder name (longest stock name ~62 chars). */
#define SD_FOLDER_NAME_LEN  64

/* List the card root's sub-directories (candidate wavetable folders) into
 * `names`, up to `max`. Skips hidden/system folders. Returns the count. */
int sd_fs_list_folders(char (*names)[SD_FOLDER_NAME_LEN], int max);

#endif
