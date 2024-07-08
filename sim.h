/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk
 *
 * This is the configuration for a Waveshare RP2040-GEEK board
 */

#ifndef SIM_INC
#define SIM_INC

#define DEF_CPU Z80	/* default CPU (Z80 or I8080) */
//#define EXCLUDE_I8080	/* we want both CPU's */
#define CPU_SPEED 4	/* CPU speed 0=unlimited */
/*#define ALT_I8080*/	/* use alt. 8080 sim. primarily optimized for size */
/*#define ALT_Z80*/	/* use alt. Z80 sim. primarily optimized for size */
#define UNDOC_INST	/* compile undocumented instructions */
/*#define FAST_BLOCK*/	/* much faster but not accurate Z80 block instr. */
#define BAREMETAL	/* set up the simulator core for bare metal use */

/*#define WANT_ICE*/	/* attach ICE to headless machine */
#ifdef WANT_ICE
#define WANT_TIM	/* count t-states */
#define HISIZE	100	/* number of entries in history */
#define SBSIZE	4	/* number of software breakpoints */
#endif

#define USR_COM "Waveshare RP2040-GEEK Z80/8080 emulator"
#define USR_REL "1.2"
#define USR_CPR "Copyright (C) 2024 by Udo Munk & Thomas Eberhardt"

#ifndef PRIu64
#define PRIu64 "llu"
#endif

#endif
