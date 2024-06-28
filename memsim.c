/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk
 *
 * This module implements the memory for the Z80/8080 CPU
 * and the low level access functions for MicroSD, needed
 * by the FDC.
 *
 * History:
 * 23-APR-2024 derived from z80sim
 * 27-MAY-2024 implemented load file
 * 28-MAY-2024 implemented sector I/O to disk images
 * 03-JUN-2024 added directory list for code files and disk images
 * 09-JUN-2024 implemented boot ROM
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "f_util.h"
#include "ff.h"
#include "sim.h"
#include "simglb.h"
#include "memsim.h"
#include "sd-fdc.h"

extern FIL sd_file;
extern FRESULT sd_res;
extern char disks[4][22];

/* 64KB non banked memory */
#define MEMSIZE 65536
unsigned char memory[MEMSIZE];

/* boot ROM code */
#undef MEMSIZE
#define MEMSIZE 256
#include "bootrom.c"

/* buffer for disk/memory transfers */
static unsigned char dsk_buf[SEC_SZ];

void init_memory(void)
{
	register int i;

	/* copy boot ROM into write protected top memory page */
	for (i = 0; i < 256; i++)
		memory[0xff00 + i] = code[i];

	/* trash memory like in a real machine after power on */
	for (i = 0; i < 0xff00; i++)
		memory[i] = rand() % 256;
}

static void complain(void)
{
	puts("File not found\n");
}

/*
 * list files with pattern 'ext' in directory 'dir'
 */
void my_ls(const char *dir, const char *ext)
{
	DIR dp;
	FILINFO fno;
	FRESULT res;
	register int i = 0;

	res = f_findfirst(&dp, &fno, dir, ext);
	if (res == FR_OK) {
		while (1) {
			printf("%s\t", fno.fname);
			if (strlen(fno.fname) < 8)
				putchar('\t');
			i++;
			if (i > 4) {
				putchar('\n');
				i = 0;
			}
			res = f_findnext(&dp, &fno);
			if (!strlen(fno.fname))
				break;
		}
	}
}

/*
 * load a file 'name' into memory
 */
void load_file(char *name)
{
	int i = 0;
	register unsigned int j;
	unsigned int br;
	char SFN[25];

	strcpy(SFN, "/CODE80/");
	strcat(SFN, name);
	strcat(SFN, ".BIN");

	/* try to open file */
	sd_res = f_open(&sd_file, SFN, FA_READ);
	if (sd_res != FR_OK) {
		complain();
		return;
	}

	/* read file into memory */
	while ((sd_res = f_read(&sd_file, &dsk_buf[0], SEC_SZ, &br)) == FR_OK) {
		for (j = 0; j < br; j++)
			dma_write(i + j, dsk_buf[j]);
		if (br < SEC_SZ)	/* last record reached */
			break;
		i += SEC_SZ;
	}
	if (sd_res != FR_OK)
		printf("f_read error: %s (%d)\n", FRESULT_str(sd_res), sd_res);
	else
		printf("loaded file \"%s\" (%d bytes)\n", SFN, i + br);

	f_close(&sd_file);
}

/*
 * mount a disk image 'name' on disk 'drive'
 */
void mount_disk(int drive, char *name)
{
	char SFN[22];
	int i;

	strcpy(SFN, "/DISKS80/");
	strcat(SFN, name);
	strcat(SFN, ".DSK");

	for (i = 0; i < 4; i++) {
		if (i != drive && strcmp(disks[i], SFN) == 0) {
			puts("Disk already mounted\n");
			return;
		}
	}

	/* try to open file */
	sd_res = f_open(&sd_file, SFN, FA_READ);
	if (sd_res != FR_OK) {
		complain();
		return;
	}

	f_close(&sd_file);
	strcpy(disks[drive], SFN);
	putchar('\n');
}

/*
 * prepare I/O for sector read and write routines
 */
static BYTE prep_io(int drive, int track, int sector, WORD addr)
{
	FSIZE_t pos;

	/* check if drive in range */
	if ((drive < 0) || (drive > 3))
		return FDC_STAT_DISK;

	/* check if track and sector in range */
	if (track > TRK)
		return FDC_STAT_TRACK;
	if ((sector < 1) || (sector > SPT))
		return FDC_STAT_SEC;

	/* check if DMA address in range */
	if (addr > 0xff7f)
		return FDC_STAT_DMAADR;

	/* check if disk in drive */
	if (!strlen(disks[drive])) {
		return FDC_STAT_NODISK;
	}

	/* open file with the disk image */
	sd_res = f_open(&sd_file, disks[drive], FA_READ | FA_WRITE);
	if (sd_res != FR_OK)
		return FDC_STAT_NODISK;

	/* seek to track/sector */
	pos = (((FSIZE_t) track * (FSIZE_t) SPT) + sector - 1) * SEC_SZ;
	if (f_lseek(&sd_file, pos) != FR_OK) {
		f_close(&sd_file);
		return FDC_STAT_SEEK;
	}
	return FDC_STAT_OK;
}

/*
 * read from drive a sector on track into memory @ addr
 */
BYTE read_sec(int drive, int track, int sector, WORD addr)
{
	BYTE stat;
	unsigned int br;
	register int i;

	/* prepare for sector read */
	if ((stat = prep_io(drive, track, sector, addr)) != FDC_STAT_OK)
		return stat;

	/* read sector into memory */
	sd_res = f_read(&sd_file, &dsk_buf[0], SEC_SZ, &br);
	if (sd_res == FR_OK) {
		if (br < SEC_SZ) {	/* UH OH */
			f_close(&sd_file);
			return FDC_STAT_READ;
		} else {
			f_close(&sd_file);
			for (i = 0; i < SEC_SZ; i++)
				dma_write(addr + i, dsk_buf[i]);
			return FDC_STAT_OK;
		}
	} else {
		f_close(&sd_file);
		return FDC_STAT_READ;
	}
}

/*
 * write to drive a sector on track from memory @ addr
 */
BYTE write_sec(int drive, int track, int sector, WORD addr)
{
	BYTE stat;
	unsigned int br;

	/* prepare for sector write */
	if ((stat = prep_io(drive, track, sector, addr)) != FDC_STAT_OK)
		return stat;

	/* write sector to disk image */
	sd_res = f_write(&sd_file, &memory[addr], SEC_SZ, &br);
	if (sd_res == FR_OK) {
		if (br < SEC_SZ) {	/* UH OH */
			f_close(&sd_file);
			return FDC_STAT_WRITE;
		} else {
			f_close(&sd_file);
			return FDC_STAT_OK;
		}
	} else {
		f_close(&sd_file);
		return FDC_STAT_WRITE;
	}
}

/*
 * get FDC command from CPU memory
 */
void get_fdccmd(BYTE *cmd, WORD addr)
{
	register int i;

	for (i = 0; i < 4; i++)
		cmd[i] = dma_read(addr + i);
}
