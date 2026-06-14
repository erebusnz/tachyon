/*---------------------------------------------------------------------------/
/  Configurations of FatFs Module  (ChaN R0.15)
/---------------------------------------------------------------------------/
/  Tachyon configuration: read-only, LFN + exFAT, single 512-byte-sector SD
/  volume on the SDIO/HAL_SD driver. exFAT is required because the stock
/  sample cards ship exFAT-formatted (>=64 GB). See wavetable plan / Phase 1.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80286	/* Revision ID */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	1
/* Read-only: we only stream wavetable/multisample data off the card. Removes
/  all writing API (f_write, f_sync, f_unlink, f_mkdir, f_rename, ...). */

#define FF_FS_MINIMIZE	0   /* keep f_stat/f_opendir/f_readdir/f_lseek */
#define FF_USE_FIND		0
#define FF_USE_MKFS		0
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0

#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	3

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437   /* U.S. — sample/folder names are ASCII */

#define FF_USE_LFN		1     /* LFN with static working buffer on the BSS */
#define FF_MAX_LFN		255
#define FF_LFN_UNICODE	0     /* ANSI/OEM API (TCHAR = char) */
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12

#define FF_FS_RPATH		0

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		1
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION	0

#define FF_MIN_SS		512
#define FF_MAX_SS		512   /* SD cards use 512-byte sectors (fixed) */

#define FF_LBA64		1     /* required for GPT: the stock cards are GPT-partitioned
                               (protective-MBR 0xEE), which FatFs only follows when
                               64-bit LBA is enabled. Needs FF_FS_EXFAT == 1. */
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0
#define FF_FS_EXFAT		1     /* required: stock cards are exFAT-formatted */

#define FF_FS_NORTC		1     /* no RTC; no effect in read-only anyway */
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026

#define FF_FS_NOFSINFO	0
#define FF_FS_LOCK		0
#define FF_FS_REENTRANT	0     /* all FS access from the main loop */
#define FF_FS_TIMEOUT	1000

/*--- End of configuration options ---*/
