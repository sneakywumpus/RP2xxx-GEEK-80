/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024-2025 by Udo Munk & Thomas Eberhardt
 *
 * This is the configuration for a Waveshare RP2040/RP2350-GEEK board
 */

#ifndef SIM_INC
#define SIM_INC

#define DEF_CPU Z80	/* default CPU (Z80 or I8080) */
//#define EXCLUDE_I8080	/* we want both CPU's */
#define CPU_SPEED 4	/* CPU speed 0=unlimited */
/*#define ALT_I8080*/	/* use alt. 8080 sim. primarily optimized for size */
/*#define ALT_Z80*/	/* use alt. Z80 sim. primarily optimized for size */
#define UNDOC_INST	/* compile undocumented instrs. (required by ALT_*) */
#ifndef EXCLUDE_Z80
/*#define FAST_BLOCK*/	/* much faster but not accurate Z80 block instr. */
#endif
#define SIMPLEPANEL	/* this machine has a simple frontpanel */
#define IOPANEL		/* this machine has an I/O port panel */

/*#define WANT_ICE*/	/* attach ICE to headless machine */
#ifdef WANT_ICE
#define BAREMETAL	/* disable ICE commands that require a full OS */
#define WANT_TIM	/* count t-states */
#define HISIZE	100	/* number of entries in history */
#define SBSIZE	4	/* number of software breakpoints */
#define WANT_HB		/* hardware breakpoint */
#endif

#if PICO_RP2040
#define MODEL "RP2040-GEEK"
#else
#define MODEL "RP2350-GEEK"
#endif

#define USR_COM "Waveshare " MODEL " Z80/8080 emulator"
#define USR_REL "1.8"
#define USR_CPR "Copyright (C) 2024-2025 by Udo Munk & Thomas Eberhardt"

#ifndef PRIu64
#define PRIu64 "llu"
#endif

#endif
