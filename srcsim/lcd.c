/*
 * Functions for using the RP2040/RP2350-GEEK LCD from the emulation
 *
 * Copyright (C) 2024 by Udo Munk & Thomas Eberhardt
 */

#include <stdio.h>
#include <string.h>
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/time.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"

#include "lcd.h"
#include "draw.h"
#include "disks.h"
#include "picosim.h"

#if COLOR_DEPTH == 12
#define STRIDE (((WAVESHARE_GEEK_LCD_WIDTH + 1) / 2) * 3)
#else
#define STRIDE (WAVESHARE_GEEK_LCD_WIDTH * 2)
#endif
static uint8_t pixmap_bits[WAVESHARE_GEEK_LCD_HEIGHT * STRIDE];

/*
 *	pixmap for drawing into.
 */
static draw_pixmap_t lcd_pixmap = {
	.bits = pixmap_bits,
	.depth = COLOR_DEPTH,
	.width = WAVESHARE_GEEK_LCD_WIDTH,
	.height = WAVESHARE_GEEK_LCD_HEIGHT,
	.stride = STRIDE
};

/* core 0 & 1 (R0 means read by core 0 etc. after multicore_launch_core1() */
static volatile lcd_func_t lcd_draw_func; /* current LCD draw func (W0 R1) */
static volatile uint8_t lcd_backlight;	/* LCD backlight intensity (W0 R1) */
static volatile bool lcd_rotated;	/* LCD rotation status (W0 R1) */
static volatile bool lcd_task_done;	/* core 1 LCD task finished (R0 W1) */
static volatile uint16_t lcd_led_color;	/* RGB LED color (W0, R1) */

static lcd_func_t lcd_status_func;	/* current LCD status panel */
static bool lcd_shows_status;		/* LCD shows status panel */

static uint32_t lcd_frame_cnt;		/* Frame counter (>2 yrs @ 60 Hz) */

static void lcd_task(void);
static void lcd_draw_empty(bool first);
static void lcd_draw_cpu_reg(bool first);
#ifdef SIMPLEPANEL
static void lcd_draw_panel(bool first);
#endif
static void lcd_draw_memory(bool first);
static void lcd_draw_drives(bool first);
#ifdef IOPANEL
static void lcd_draw_ports(bool first);
#endif

uint16_t led_color;			/* RGB LED color (core 0) */

void lcd_init(void)
{
	lcd_draw_func = lcd_draw_empty;
	lcd_backlight = 90;
	lcd_rotated = false;
	lcd_led_color = C_BLACK;
	lcd_task_done = false;

	lcd_status_func = lcd_draw_cpu_reg;
	lcd_shows_status = false;

	lcd_frame_cnt = 0;

	led_color = lcd_led_color;

	draw_set_pixmap(&lcd_pixmap);

	/* launch LCD task on other core */
	multicore_launch_core1(lcd_task);
}

void lcd_exit(void)
{
	/* tell LCD refresh task to finish */
	lcd_custom_disp(NULL);

	/* wait until it stopped */
	while (!lcd_task_done)
		sleep_ms(20);

	/* kill LCD refresh task and reset core 1 */
	multicore_reset_core1();
}

#define LCD_REFRESH_US (1000000 / LCD_REFRESH)

static void __not_in_flash_func(lcd_task)(void)
{
	absolute_time_t t;
	int64_t d;
	bool first, rotated, new_rotated;
	uint8_t backlight, new_backlight;
	lcd_func_t draw_func, new_draw_func;

	/* initialize the LCD controller */
	backlight = lcd_backlight;
	lcd_dev_init(backlight);

	rotated = false;
	draw_func = NULL;
	first = true;

	while (true) {
		/* loops every LCD_REFRESH_US */

		t = get_absolute_time();

		/* check for request to exit task */
		if (lcd_draw_func == NULL)
			break;

		/* check if backlight changed */
		new_backlight = lcd_backlight;
		if (new_backlight != backlight) {
			backlight = new_backlight;
			lcd_dev_backlight(backlight);
		}

		/* check if rotation changed */
		new_rotated = lcd_rotated;
		if (new_rotated != rotated) {
			rotated = new_rotated;
			lcd_dev_rotation(rotated);
		}

		/* check if drawing function changed */
		new_draw_func = lcd_draw_func;
		if (new_draw_func != draw_func) {
			draw_func = new_draw_func;
			first = true;
		}

		/* call drawing function and send pixmap to LCD */
		(*draw_func)(first);
		first = false;
		lcd_dev_send_pixmap(draw_pixmap);

		lcd_frame_cnt++;

		d = absolute_time_diff_us(t, get_absolute_time());
		// printf("SLEEP %lld\n", LCD_REFRESH_US - d);
		if (d < LCD_REFRESH_US)
			sleep_us(LCD_REFRESH_US - d);
#if 0
		else
			puts("REFRESH!");
#endif
	}

	/* deinitialize the LCD controller */
	lcd_dev_exit();
	lcd_task_done = true;

	while (true)
		__wfi();
}

void lcd_brightness(int brightness)
{
	lcd_backlight = brightness;
}

void lcd_set_rotation(bool rotated)
{
	lcd_rotated = rotated;
}

void lcd_update_led(void)
{
	lcd_led_color = led_color;
}

void lcd_custom_disp(lcd_func_t draw_func)
{
	lcd_draw_func = draw_func;
	lcd_shows_status = false;
}

void lcd_status_disp(int which)
{
	switch (which) {
	case LCD_STATUS_REGISTERS:
		lcd_status_func = lcd_draw_cpu_reg;
		break;
#ifdef SIMPLEPANEL
	case LCD_STATUS_PANEL:
		lcd_status_func = lcd_draw_panel;
		break;
#endif
	case LCD_STATUS_DRIVES:
		lcd_status_func = lcd_draw_drives;
		break;
#ifdef IOPANEL
	case LCD_STATUS_PORTS:
		lcd_status_func = lcd_draw_ports;
		break;
#endif
	case LCD_STATUS_MEMORY:
		lcd_status_func = lcd_draw_memory;
		break;
	case LCD_STATUS_CURRENT:
	default:
		break;
	}
	lcd_draw_func = lcd_status_func;
	lcd_shows_status = true;
}

void lcd_status_next(void)
{
	if (lcd_status_func == lcd_draw_cpu_reg)
#ifdef SIMPLEPANEL
		lcd_status_func = lcd_draw_panel;
	else if (lcd_status_func == lcd_draw_panel)
#endif
		lcd_status_func = lcd_draw_drives;
	else if (lcd_status_func == lcd_draw_drives)
#ifdef IOPANEL
		lcd_status_func = lcd_draw_ports;
	else if (lcd_status_func == lcd_draw_ports)
#endif
		lcd_status_func = lcd_draw_memory;
	else
		lcd_status_func = lcd_draw_cpu_reg;
	if (lcd_shows_status)
		lcd_draw_func = lcd_status_func;
}

static void __not_in_flash_func(lcd_draw_empty)(bool first)
{
	if (first)
		draw_clear(C_BLACK);
}

/*
 *	Info line at the bottom of the LCD, used by all status
 *	displays except memory:
 *
 *	xx.xx °C   o    xxx.xx MHz
 */

static void __not_in_flash_func(lcd_draw_info)(font_t *font, bool first)
{
	char c;
	int i, f, temp, digit;
	bool onlyz;
	const uint16_t w = font->width;
	const uint16_t n = draw_pixmap->width / w;
	const uint16_t x = (draw_pixmap->width - n * w) / 2;
	const uint16_t y = draw_pixmap->height - font->height;
	static uint32_t last_upd;

	if (first) {
		/* draw static content */

		/* draw temperature text */
		draw_char(2 * w + x, y, '.', font, C_ORANGE, C_DKBLUE);
		draw_char(6 * w + x, y, '\007', font, C_ORANGE, C_DKBLUE);
		draw_char(7 * w + x, y, 'C', font, C_ORANGE, C_DKBLUE);

		/* draw frequency text */
		draw_char((n - 7) * w + x, y, '.', font, C_ORANGE, C_DKBLUE);
		draw_char((n - 3) * w + x, y, 'M', font, C_ORANGE, C_DKBLUE);
		draw_char((n - 2) * w + x, y, 'H', font, C_ORANGE, C_DKBLUE);
		draw_char((n - 1) * w + x, y, 'z', font, C_ORANGE, C_DKBLUE);

		/* draw the RGB LED bracket */
		draw_led_bracket(11 * w + x, y + (font->height - 10) / 2);

		/* force update */
		last_upd = lcd_frame_cnt - LCD_REFRESH + 1;
	} else {
		/* draw dynamic content */

		/* update temperature and frequency every second */
		if (lcd_frame_cnt - last_upd >= LCD_REFRESH) {
			last_upd = lcd_frame_cnt;

			/* read the onboard temperature sensor */
			temp = (int) (read_onboard_temp() * 100.0f + 0.5f);

			/* draw temperature value */
			for (i = 0; i < 5; i++) {
				draw_char((4 - i) * w + x, y, '0' + temp % 10,
					  font, C_ORANGE, C_DKBLUE);
				if (i < 4)
					temp /= 10;
				if (i == 1)
					i++; /* skip decimal point */
			}

			/* draw frequency value */
			f = (unsigned) (cpu_freq / 10000ULL);
			digit = 10000;
			onlyz = true;
			for (i = 0; i < 6; i++) {
				c = '0';
				while (f > digit) {
					f -= digit;
					c++;
				}
				if (onlyz && i < 2 && c == '0')
					c = ' ';
				else
					onlyz = false;
				draw_char((n - 10 + i) * w + x, y, c,
					  font, C_ORANGE, C_DKBLUE);
				if (i < 5)
					digit /= 10;
				if (i == 2)
					i++; /* skip decimal point */
			}
		}

		/* update the RGB LED */
		draw_led(11 * w + x, y + (font->height - 10) / 2,
			 lcd_led_color);
	}
}

/*
 *	CPU status displays:
 *
 *	Z80 CPU using font20 (10 x 20 pixels):
 *
 *	  01234567890123456789012
 *	0 AF xxxx BC xxxx DE xxxx
 *	1 HL xxxx SP xxxx PC xxxx
 *	2 AF'xxxx BC'xxxx DE'xxxx
 *	3 HL'xxxx IX xxxx IY xxxx
 *	4 F  SZHPNC  IF12 IR xxxx
 *
 *	8080 CPU using font28 (14 x 28 pixels):
 *
 *	  0123456789012345
 *	0 AF xxxx  BC xxxx
 *	1 DE xxxx  HL xxxx
 *	2 SP xxxx  PC xxxx
 *	3 F  SZHPC    IF 1
 */

typedef struct reg {
	uint8_t x;
	uint8_t y;
	enum { RB, RW, RJ, RF, RI, RR } type;
	const char *l;
	union {
		struct {
			const BYTE *p;
		} b;
		struct {
			const WORD *p;
		} w;
		struct {
			const int *p;
		} i;
		struct {
			char c;
			uint8_t m;
		} f;
	};
} reg_t;

#ifndef EXCLUDE_Z80

#define XOFF20	5	/* x pixel offset of text grid for font20 */
#define YOFF20	0	/* y pixel offset of text grid for font20 */
#define SPC20	3	/* vertical text spacing for font20 */

static const reg_t __not_in_flash("lcd_tables") regs_z80[] = {
	{  4, 0, RB, "AF",   .b.p = &A },
	{  6, 0, RJ, NULL,   .i.p = &F },
	{ 12, 0, RB, "BC",   .b.p = &B },
	{ 14, 0, RB, NULL,   .b.p = &C },
	{ 20, 0, RB, "DE",   .b.p = &D },
	{ 22, 0, RB, NULL,   .b.p = &E },
	{  4, 1, RB, "HL",   .b.p = &H },
	{  6, 1, RB, NULL,   .b.p = &L },
	{ 14, 1, RW, "SP",   .w.p = &SP },
	{ 22, 1, RW, "PC",   .w.p = &PC },
	{  4, 2, RB, "AF\'", .b.p = &A_ },
	{  6, 2, RJ, NULL,   .i.p = &F_ },
	{ 12, 2, RB, "BC\'", .b.p = &B_ },
	{ 14, 2, RB, NULL,   .b.p = &C_ },
	{ 20, 2, RB, "DE\'", .b.p = &D_ },
	{ 22, 2, RB, NULL,   .b.p = &E_ },
	{  4, 3, RB, "HL\'", .b.p = &H_ },
	{  6, 3, RB, NULL,   .b.p = &L_ },
	{ 14, 3, RW, "IX",   .w.p = &IX },
	{ 22, 3, RW, "IY",   .w.p = &IY },
	{  3, 4, RF, NULL,   .f.c = 'S', .f.m = S_FLAG },
	{  4, 4, RF, "F",    .f.c = 'Z', .f.m = Z_FLAG },
	{  5, 4, RF, NULL,   .f.c = 'H', .f.m = H_FLAG },
	{  6, 4, RF, NULL,   .f.c = 'P', .f.m = P_FLAG },
	{  7, 4, RF, NULL,   .f.c = 'N', .f.m = N_FLAG },
	{  8, 4, RF, NULL,   .f.c = 'C', .f.m = C_FLAG },
	{ 13, 4, RI, NULL,   .f.c = '1', .f.m = 1 },
	{ 14, 4, RI, "IF",   .f.c = '2', .f.m = 2 },
	{ 20, 4, RB, "IR",   .b.p = &I },
	{ 22, 4, RR, NULL,   .b.p = NULL }
};
static const int num_regs_z80 = sizeof(regs_z80) / sizeof(reg_t);

#endif /* !EXCLUDE_Z80 */

#ifndef EXCLUDE_I8080

#define XOFF28	8	/* x pixel offset of text grid for font28 */
#define YOFF28	0	/* y pixel offset of text grid for font28 */
#define SPC28	1	/* vertical text spacing for font28 */

static const reg_t __not_in_flash("lcd_tables") regs_8080[] = {
	{  4, 0, RB, "AF", .b.p = &A },
	{  6, 0, RJ, NULL, .i.p = &F },
	{ 13, 0, RB, "BC", .b.p = &B },
	{ 15, 0, RB, NULL, .b.p = &C },
	{  4, 1, RB, "DE", .b.p = &D },
	{  6, 1, RB, NULL, .b.p = &E },
	{ 13, 1, RB, "HL", .b.p = &H },
	{ 15, 1, RB, NULL, .b.p = &L },
	{  6, 2, RW, "SP", .w.p = &SP },
	{ 15, 2, RW, "PC", .w.p = &PC },
	{  3, 3, RF, NULL, .f.c = 'S', .f.m = S_FLAG },
	{  4, 3, RF, "F",  .f.c = 'Z', .f.m = Z_FLAG },
	{  5, 3, RF, NULL, .f.c = 'H', .f.m = H_FLAG },
	{  6, 3, RF, NULL, .f.c = 'P', .f.m = P_FLAG },
	{  7, 3, RF, NULL, .f.c = 'C', .f.m = C_FLAG },
	{ 15, 3, RI, "IF", .f.c = '1', .f.m = 3 }
};
static const int num_regs_8080 = sizeof(regs_8080) / sizeof(reg_t);

#endif /* !EXCLUDE_I8080 */

static void __not_in_flash_func(lcd_draw_cpu_reg)(bool first)
{
	char c;
	int i, j, n = 0;
	uint16_t x;
	WORD w;
	const char *s;
	const reg_t *rp = NULL;
	static int cpu_type;
	static draw_grid_t grid;

	/* redraw static content if new CPU type */
	if (cpu_type != cpu) {
		cpu_type = cpu;
		first = true;
	}

	/* use cpu_type in the rest of this function, since cpu can change */

#ifndef EXCLUDE_Z80
	if (cpu_type == Z80) {
		rp = regs_z80;
		n = num_regs_z80;
	}
#endif
#ifndef EXCLUDE_I8080
	if (cpu_type == I8080) {
		rp = regs_8080;
		n = num_regs_8080;
	}
#endif

	if (first) {
		/* draw static content */

		draw_clear(C_DKBLUE);

		/* setup text grid and draw grid lines */
#ifndef EXCLUDE_Z80
		if (cpu_type == Z80) {
			draw_setup_grid(&grid, XOFF20, YOFF20, -1, 5, &font20,
					SPC20);

			/* draw vertical grid lines */
			draw_grid_vline(7, 0, 4, &grid, C_DKYELLOW);
			draw_grid_vline(10, 4, 1, &grid, C_DKYELLOW);
			draw_grid_vline(15, 0, 5, &grid, C_DKYELLOW);
			/* draw horizontal grid lines */
			for (i = 1; i < 5; i++)
				draw_grid_hline(0, i, grid.cols, &grid,
						C_DKYELLOW);
		}
#endif
#ifndef EXCLUDE_I8080
		if (cpu_type == I8080) {
			draw_setup_grid(&grid, XOFF28, YOFF28, -1, 4, &font28,
					SPC28);

			/* draw vertical grid line */
			draw_grid_vline(8, 0, 4, &grid, C_DKYELLOW);
			/* draw horizontal grid lines */
			for (i = 1; i < 4; i++)
				draw_grid_hline(0, i, grid.cols, &grid,
						C_DKYELLOW);
		}
#endif
		/* draw register labels */
		for (i = 0; i < n; rp++, i++)
			if ((s = rp->l) != NULL) {
				x = rp->x - (rp->type == RW ? 6 : 4);
				if (rp->type == RI)
					x++;
				while (*s)
					draw_grid_char(x++, rp->y, *s++, &grid,
						       C_WHITE, C_DKBLUE);
			}
	} else {
		/* draw dynamic content */

		/* draw register contents */
		for (i = 0; i < n; rp++, i++) {
			switch (rp->type) {
			case RB: /* byte sized register */
				w = *(rp->b.p);
				j = 2;
				break;
			case RW: /* word sized register */
				w = *(rp->w.p);
				j = 4;
				break;
			case RJ: /* F or F_ integer register */
				w = *(rp->i.p);
				j = 2;
				break;
			case RF: /* flags */
				draw_grid_char(rp->x, rp->y, rp->f.c, &grid,
					       (F & rp->f.m) ? C_GREEN : C_RED,
					       C_DKBLUE);
				continue;
			case RI: /* interrupt register */
				draw_grid_char(rp->x, rp->y, rp->f.c, &grid,
					       (IFF & rp->f.m) == rp->f.m ?
					       C_GREEN : C_RED, C_DKBLUE);
				continue;
#ifndef EXCLUDE_Z80
			case RR: /* refresh register */
				w = (R_ & 0x80) | (R & 0x7f);
				j = 2;
				break;
#endif
			default:
				continue;
			}
			x = rp->x;
			while (j--) {
				c = w & 0xf;
				c += (c < 10 ? '0' : 'A' - 10);
				draw_grid_char(x--, rp->y, c, &grid, C_GREEN,
					       C_DKBLUE);
				w >>= 4;
			}
		}
	}

	/* draw info line */
	lcd_draw_info(&font20, first);
}

/*
 *	Memory contents display:
 *
 *	Displays the contents of the 64K and 48K memory banks in
 *	one 128x128 and one 128x96 pixel block by combining 4 memory
 *	bytes into one 12/16-bit color pixel with a magic formula.
 */

#define MEM_XOFF 3	/* memory display x offset */
#define MEM_YOFF 0	/* memory display y offset */
#define MEM_BRDR 3	/* free space around and between pixel blocks */

static void __not_in_flash_func(lcd_draw_memory)(bool first)
{
	int x, y;
	const uint32_t *p;

	if (first) {
		/* draw static content */

		draw_clear(C_DKBLUE);

		draw_hline(MEM_XOFF, MEM_YOFF, 128 + 96 + 4 * MEM_BRDR - 1,
			   C_GREEN);
		draw_hline(MEM_XOFF, MEM_YOFF + 128 + 2 * MEM_BRDR - 1,
			   128 + 96 + 4 * MEM_BRDR - 1, C_GREEN);
		draw_vline(MEM_XOFF, MEM_YOFF, 128 + 2 * MEM_BRDR, C_GREEN);
		draw_vline(MEM_XOFF + 128 + 2 * MEM_BRDR - 1, 0,
			   128 + 2 * MEM_BRDR, C_GREEN);
		draw_vline(MEM_XOFF + 128 + 96 + 4 * MEM_BRDR - 2, 0,
			   128 + 2 * MEM_BRDR, C_GREEN);
	} else {
		/* draw dynamic content */

		p = (uint32_t *) bnk0;
		for (x = MEM_XOFF + MEM_BRDR;
		     x < MEM_XOFF + MEM_BRDR + 128; x++) {
			for (y = MEM_YOFF + MEM_BRDR;
			     y < MEM_YOFF + MEM_BRDR + 128; y++) {
				/* constant = 2^32 / ((1 + sqrt(5)) / 2) */
#if COLOR_DEPTH == 12
				draw_pixel(x, y, (*p++ * 2654435769U) >> 20);
#else
				draw_pixel(x, y, (*p++ * 2654435769U) >> 16);
#endif
			}
		}

		p = (uint32_t *) bnk1;
		for (x = MEM_XOFF + 3 * MEM_BRDR - 1 + 128;
		     x < MEM_XOFF + 3 * MEM_BRDR - 1 + 128 + 96; x++) {
			for (y = MEM_YOFF + MEM_BRDR;
			     y < MEM_YOFF + MEM_BRDR + 128; y++) {
#if COLOR_DEPTH == 12
				draw_pixel(x, y, (*p++ * 2654435769U) >> 20);
#else
				draw_pixel(x, y, (*p++ * 2654435769U) >> 16);
#endif
			}
		}
	}
}

#ifdef SIMPLEPANEL

/*
 *	Classic front panel display:
 *
 *	P0 P1 P2 P3 P4 P5 P6 P7              IE RU WA HO
 *      () () () () () () () ()              () () () ()
 *                        __
 *	MR IP M1 OP HA ST WO IA  D7 D6 D5 D4 D3 D2 D1 D0
 *	() () () () () () () ()  () () () () () () () ()
 *
 *	15 14 13 12 11 10 A9 A8  A7 A6 A5 A4 A3 A2 A1 A0
 *	() () () () () () () ()  () () () () () () () ()
 *
 *
 *	Doesn't work as nicely as the Z80pack desktop frontpanel,
 *	which calculates a light intensity based on the percentage
 *	a LED is on during a panel refresh cycle, but computing
 *	resources are more limited on microcontrollers.
 */

#define PXOFF	6				/* panel x offset */
#define PYOFF	6				/* panel y offset */

#define PFNTH	12				/* font12 height */
#define PFNTW	6				/* font12 width */
#define PFNTS	1				/* font12 letter spacing */

#define PLBLW	(2 * PFNTW - PFNTS)		/* Label width */
#define PLBLS	2				/* Label vertical spacing */
#define PLEDS	3 				/* LED spacing */
#define PLEDBS	6				/* LED bank of 8 spacing */

#define PLEDD	10				/* LED diameter */
#define PLEDXO	((PLBLW - PLEDD + 1) / 2)	/* LED x off from label left */
#define PLEDYO	(PFNTH + PLBLS)			/* LED y off from label top */
#define PLEDHO	(PLBLW + PLEDS)			/* horiz. offset to next LED */
#define PLEDVO	(3 * PFNTH)			/* vert. offset to next row */

#define LX(x)	(PXOFF + PLEDXO + PLEDBS * ((x) / 8) + PLEDHO * (x))
#define LY(y)	(PYOFF + PLEDYO + PLEDVO * (y))

static BYTE fp_led_wait;			/* dummy */

typedef struct led {
	uint16_t x;
	uint16_t y;
	char c1;
	char c2;
	enum { LB, LW } type;
	union {
		struct {
			BYTE i;
			BYTE m;
			const BYTE *p;
		} b;
		struct {
			WORD m;
			const WORD *p;
		} w;
	};
} led_t;

static const led_t __not_in_flash("lcd_tables") leds[] = {
	{ LX( 0), LY(0), 'P', '7', LB, .b.i = 0xff, .b.m = 0x80, .b.p = &fp_led_output },
	{ LX( 1), LY(0), 'P', '6', LB, .b.i = 0xff, .b.m = 0x40, .b.p = &fp_led_output },
	{ LX( 2), LY(0), 'P', '5', LB, .b.i = 0xff, .b.m = 0x20, .b.p = &fp_led_output },
	{ LX( 3), LY(0), 'P', '4', LB, .b.i = 0xff, .b.m = 0x10, .b.p = &fp_led_output },
	{ LX( 4), LY(0), 'P', '3', LB, .b.i = 0xff, .b.m = 0x08, .b.p = &fp_led_output },
	{ LX( 5), LY(0), 'P', '2', LB, .b.i = 0xff, .b.m = 0x04, .b.p = &fp_led_output },
	{ LX( 6), LY(0), 'P', '1', LB, .b.i = 0xff, .b.m = 0x02, .b.p = &fp_led_output },
	{ LX( 7), LY(0), 'P', '0', LB, .b.i = 0xff, .b.m = 0x01, .b.p = &fp_led_output },
	{ LX(12), LY(0), 'I', 'E', LB, .b.i = 0x00, .b.m = 0x01, .b.p = &IFF },
	{ LX(13), LY(0), 'R', 'U', LB, .b.i = 0x00, .b.m = 0x01, .b.p = &cpu_state },
	{ LX(14), LY(0), 'W', 'A', LB, .b.i = 0x00, .b.m = 0x01, .b.p = &fp_led_wait },
	{ LX(15), LY(0), 'H', 'O', LB, .b.i = 0x00, .b.m = 0x01, .b.p = &bus_request },
	{ LX( 0), LY(1), 'M', 'R', LB, .b.i = 0x00, .b.m = 0x80, .b.p = &cpu_bus },
	{ LX( 1), LY(1), 'I', 'P', LB, .b.i = 0x00, .b.m = 0x40, .b.p = &cpu_bus },
	{ LX( 2), LY(1), 'M', '1', LB, .b.i = 0x00, .b.m = 0x20, .b.p = &cpu_bus },
	{ LX( 3), LY(1), 'O', 'P', LB, .b.i = 0x00, .b.m = 0x10, .b.p = &cpu_bus },
	{ LX( 4), LY(1), 'H', 'A', LB, .b.i = 0x00, .b.m = 0x08, .b.p = &cpu_bus },
	{ LX( 5), LY(1), 'S', 'T', LB, .b.i = 0x00, .b.m = 0x04, .b.p = &cpu_bus },
	{ LX( 6), LY(1), 'W', 'O', LB, .b.i = 0x00, .b.m = 0x02, .b.p = &cpu_bus },
	{ LX( 7), LY(1), 'I', 'A', LB, .b.i = 0x00, .b.m = 0x01, .b.p = &cpu_bus },
	{ LX( 8), LY(1), 'D', '7', LB, .b.i = 0x00, .b.m = 0x80, .b.p = &fp_led_data },
	{ LX( 9), LY(1), 'D', '6', LB, .b.i = 0x00, .b.m = 0x40, .b.p = &fp_led_data },
	{ LX(10), LY(1), 'D', '5', LB, .b.i = 0x00, .b.m = 0x20, .b.p = &fp_led_data },
	{ LX(11), LY(1), 'D', '4', LB, .b.i = 0x00, .b.m = 0x10, .b.p = &fp_led_data },
	{ LX(12), LY(1), 'D', '3', LB, .b.i = 0x00, .b.m = 0x08, .b.p = &fp_led_data },
	{ LX(13), LY(1), 'D', '2', LB, .b.i = 0x00, .b.m = 0x04, .b.p = &fp_led_data },
	{ LX(14), LY(1), 'D', '1', LB, .b.i = 0x00, .b.m = 0x02, .b.p = &fp_led_data },
	{ LX(15), LY(1), 'D', '0', LB, .b.i = 0x00, .b.m = 0x01, .b.p = &fp_led_data },
	{ LX( 0), LY(2), '1', '5', LW, .w.m = 0x8000, .w.p = &fp_led_address },
	{ LX( 1), LY(2), '1', '4', LW, .w.m = 0x4000, .w.p = &fp_led_address },
	{ LX( 2), LY(2), '1', '3', LW, .w.m = 0x2000, .w.p = &fp_led_address },
	{ LX( 3), LY(2), '1', '2', LW, .w.m = 0x1000, .w.p = &fp_led_address },
	{ LX( 4), LY(2), '1', '1', LW, .w.m = 0x0800, .w.p = &fp_led_address },
	{ LX( 5), LY(2), '1', '0', LW, .w.m = 0x0400, .w.p = &fp_led_address },
	{ LX( 6), LY(2), 'A', '9', LW, .w.m = 0x0200, .w.p = &fp_led_address },
	{ LX( 7), LY(2), 'A', '8', LW, .w.m = 0x0100, .w.p = &fp_led_address },
	{ LX( 8), LY(2), 'A', '7', LW, .w.m = 0x0080, .w.p = &fp_led_address },
	{ LX( 9), LY(2), 'A', '6', LW, .w.m = 0x0040, .w.p = &fp_led_address },
	{ LX(10), LY(2), 'A', '5', LW, .w.m = 0x0020, .w.p = &fp_led_address },
	{ LX(11), LY(2), 'A', '4', LW, .w.m = 0x0010, .w.p = &fp_led_address },
	{ LX(12), LY(2), 'A', '3', LW, .w.m = 0x0008, .w.p = &fp_led_address },
	{ LX(13), LY(2), 'A', '2', LW, .w.m = 0x0004, .w.p = &fp_led_address },
	{ LX(14), LY(2), 'A', '1', LW, .w.m = 0x0002, .w.p = &fp_led_address },
	{ LX(15), LY(2), 'A', '0', LW, .w.m = 0x0001, .w.p = &fp_led_address }
};
static const int num_leds = sizeof(leds) / sizeof(led_t);

static void __not_in_flash_func(lcd_draw_panel)(bool first)
{
	const led_t *p = leds;
	int i;
	uint16_t col;

	if (first) {
		/* draw static content */

		draw_clear(C_DKBLUE);

		for (i = 0; i < num_leds; i++) {
			draw_char(p->x - PLEDXO, p->y - PLEDYO,
				  p->c1, &font12, C_WHITE, C_DKBLUE);
			draw_char(p->x - PLEDXO + PFNTW, p->y - PLEDYO,
				  p->c2, &font12, C_WHITE, C_DKBLUE);
			if (p->c1 == 'W' && p->c2 == 'O')
				draw_hline(p->x - PLEDXO, p->y - PLEDYO - 2,
					   PLBLW, C_WHITE);
			draw_led_bracket(p->x, p->y);
			p++;
		}
	} else {
		/* draw dynamic content */

		for (i = 0; i < num_leds; i++) {
			col = C_DKRED;
			if (p->type == LB) {
				if ((*(p->b.p) ^ p->b.i) & p->b.m)
					col = C_RED;
			} else {
				if (*(p->w.p) & p->w.m)
					col = C_RED;
			}
			draw_led(p->x, p->y, col);
			p++;
		}
	}

	/* draw info line */
	lcd_draw_info(&font20, first);
}

#endif /* SIMPLEPANEL */

/*
 *	Diskette drives display using font28 (14 x 28 pixels):
 *
 *	  0123456789012345
 *	0 A oTxx Sxx Axxxx
 *	1 B oTxx Sxx Axxxx
 *	2 C oTxx Sxx Axxxx
 *	3 D oTxx Sxx Axxxx
 *
 *	Shows last access type LED, track, sector, and DMA address
 *	of disk drive operations. Clears after 10 seconds of no access.
 */

#define DXOFF	8	/* x pixel offset of text grid */
#define DYOFF	0	/* y pixel offset of text grid */
#define DSPC	1	/* vertical text spacing */

typedef struct lcd_drive {
	uint8_t track;	/* track */
	uint8_t sector;	/* sector, non-zero indicates drive in use */
	WORD addr;	/* DMA address */
	bool rdwr;	/* false = READ, true = WRITE */
	bool active;	/* used for access LED in info line */
	uint32_t lastacc;
} lcd_drive_t;

static lcd_drive_t lcd_drives[NUMDISK];

/*
 *	Called from core 0 to update disk drive status
 */
void lcd_update_drive(int drive, int track, int sector, WORD addr, bool rdwr,
		      bool active)
{
	lcd_drive_t *p = &lcd_drives[drive];

	p->track = track;
	p->sector = sector;
	p->addr = addr;
	p->rdwr = rdwr;
	p->active = active;
	p->lastacc = lcd_frame_cnt;

	if (p->active) {
		if (p->rdwr)
			led_color = (led_color & ~C_RED) | C_RED;
		else
			led_color = (led_color & ~C_GREEN) | C_GREEN;
	} else
		led_color &= ~(C_RED | C_GREEN);
	lcd_update_led();
}

static void __not_in_flash_func(lcd_draw_drives)(bool first)
{
	char c;
	int i, j;
	WORD w;
	bool clr;
	lcd_drive_t *p = lcd_drives;
	static draw_grid_t grid;

	if (first) {
		/* draw static content */

		draw_clear(C_DKBLUE);

		draw_setup_grid(&grid, DXOFF, DYOFF, -1, 4, &font28, DSPC);

		for (i = 0; i < NUMDISK; i++) {
			draw_grid_char(0, i, 'A' + i, &grid, C_CYAN,
				       C_DKBLUE);
			draw_led_bracket(grid.cwidth +
					 (2 * grid.cwidth - 10) / 2 +
					 grid.xoff,
					 i * grid.cheight +
					 (grid.cheight - grid.spc - 10) / 2 +
					 grid.yoff);
			draw_char(3 * grid.cwidth + grid.xoff,
				  i * grid.cheight + grid.yoff +
				  font28.height - font20.height - 2,
				  'T', &font20, C_WHEAT, C_DKBLUE);
			draw_char(7 * grid.cwidth + grid.xoff,
				  i * grid.cheight + grid.yoff +
				  font28.height - font20.height - 2,
				  'S', &font20, C_WHEAT, C_DKBLUE);
			draw_char(11 * grid.cwidth + grid.xoff,
				  i * grid.cheight + grid.yoff +
				  font28.height - font20.height - 2,
				  'A', &font20, C_WHEAT, C_DKBLUE);
			if (i)
				draw_grid_hline(0, i, grid.cols, &grid,
						C_DKYELLOW);
			p++;
		}
     } else {
		/* draw dynamic content */

		for (i = 0; i < NUMDISK; i++) {
			/* clear drive 10 seconds after last access */
			clr = false;
			if (lcd_frame_cnt - p->lastacc >= 10 * LCD_REFRESH) {
				p->sector = 0;
				clr = true;
			}

			if (p->sector || clr) {
				draw_led(grid.cwidth +
					 (2 * grid.cwidth - 10) / 2 +
					 grid.xoff,
					 i * grid.cheight +
					 (grid.cheight - grid.spc - 10) / 2 +
					 grid.yoff,
					 clr ? C_DKBLUE
					     : (p->rdwr ? C_RED : C_GREEN));
				draw_grid_char(4, i,
					       clr ? ' ' : '0' + p->track / 10,
					       &grid, C_YELLOW, C_DKBLUE);
				draw_grid_char(5, i,
					       clr ? ' ' : '0' + p->track % 10,
					       &grid, C_YELLOW, C_DKBLUE);
				draw_grid_char(8, i,
					       clr ? ' ' : '0' + p->sector / 10,
					       &grid, C_YELLOW, C_DKBLUE);
				draw_grid_char(9, i,
					       clr ? ' ' : '0' + p->sector % 10,
					       &grid, C_YELLOW, C_DKBLUE);
				w = p->addr;
				for (j = 0; j < 4; j++) {
					c = w & 0xf;
					c += (c < 10 ? '0' : 'A' - 10);
					draw_grid_char(15 - j, i, clr ? ' ' : c,
						       &grid, C_YELLOW,
						       C_DKBLUE);
					w >>= 4;
				}
			}
			p++;
		}
	}

	/* draw info line */
	lcd_draw_info(&font20, first);
}

#ifdef IOPANEL

/*
 *	I/O ports access display:
 *
 *	00 88888888888888888888888888888888
 *	20 88888888888888888888888888888888
 *	40 88888888888888888888888888888888
 *	60 88888888888888888888888888888888
 *	80 88888888888888888888888888888888
 *	A0 88888888888888888888888888888888
 *	C0 88888888888888888888888888888888
 *	E0 88888888888888888888888888888888
 *
 *	Shows read/write accesses to I/O ports in the last refresh cycle.
 */

#define IOXOFF	0			/* I/O ports panel x offset */
#define IOYOFF	0			/* I/O ports panel y offset */

#define IOLEDW	6			/* I/O port LED width */
#define IOLEDXS	1			/* I/O port LED x spacing */
#define IOLEDGW	(IOLEDW + IOLEDXS)	/* I/O port LED grid cell width */
#define IOLEDH	7			/* I/O port LED height */
#define IOLEDYS	1			/* I/O port LED y spacing */
#define IOLEDGH	(2 * IOLEDH + IOLEDYS)	/* I/O port LED grid cell height */

static void __not_in_flash_func(lcd_draw_ports)(bool first)
{
	port_flags_t *p = port_flags;
	int i, j, k;
	uint16_t col;

	if (first) {
		/* draw static content */

		draw_clear(C_DKBLUE);
		for (j = 0; j < 8; j++) {
			draw_char(IOXOFF, j * IOLEDGH + IOYOFF,
				  "02468ACE"[j], &font14, C_WHITE, C_DKBLUE);
			draw_char(font14.width + IOXOFF, j * IOLEDGH + IOYOFF,
				  '0', &font14, C_WHITE, C_DKBLUE);
			if (j)
				draw_hline(2 * font14.width + 1 + IOXOFF,
					   j * IOLEDGH - IOLEDYS + IOYOFF,
					   32 * IOLEDGW - IOLEDXS,
					   C_DKYELLOW);
		}
		for (i = 1; i < 32; i++)
			draw_vline(2 * font14.width + 1 +
				   i * IOLEDGW - IOLEDXS + IOXOFF,
				   IOYOFF, 8 * IOLEDGH - IOLEDYS, C_DKYELLOW);
	} else {
		/* draw dynamic content */

		for (j = 0; j < 8; j++) {
			for (i = 0; i < 32; i++) {
				col = (p->in ? C_GREEN : C_DKBLUE);
				for (k = 0; k < IOLEDH; k++)
					draw_hline(2 * font14.width + 1 +
						   i * IOLEDGW + IOXOFF,
						   k + j * IOLEDGH + IOYOFF,
						   IOLEDW, col);
				col = (p->out ? C_RED : C_DKBLUE);
				for (k = 0; k < IOLEDH; k++)
					draw_hline(2 * font14.width + 1 +
						   i * IOLEDGW + IOXOFF,
						   k + j * IOLEDGH +
						   IOLEDH + IOYOFF,
						   IOLEDW, col);
				p++;
			}
		}

		/* clear access flags */
		memset(port_flags, 0, sizeof(port_flags));
	}

	/* draw info line */
	lcd_draw_info(&font16, first);
}

#endif /* IOPANEL */
