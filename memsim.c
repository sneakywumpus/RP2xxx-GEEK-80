/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk
 *
 * This module implements the memory for the Z80/8080 CPU.
 *
 * History:
 * 23-APR-2024 derived from z80sim
 * 09-JUN-2024 implemented boot ROM
 * 28-JUN-2024 added second memory bank
 */

#include <stdint.h>
#include <stdlib.h>
#include "sim.h"
#include "simglb.h"
#include "memsim.h"

/* 64KB bank 0 + common segment */
unsigned char bnk0[65536];
/* 48KB bank 1 */
unsigned char bnk1[49152];

/* boot ROM code */
#define MEMSIZE 256
#include "bootrom.c"

void init_memory(void)
{
	register int i;

	/* copy boot ROM into write protected top memory page */
	for (i = 0; i < 256; i++)
		bnk0[0xff00 + i] = code[i];

	/* trash memory like in a real machine after power on */
	for (i = 0; i < 0xff00; i++)
		bnk0[i] = rand() % 256;
	for (i = 0; i < 0xc000; i++)
		bnk1[i] = rand() % 256;
}
