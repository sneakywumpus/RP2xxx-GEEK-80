;
;	8080 CBIOS for z80pack picosim machines using SD-FDC
;
;	Copyright (C) 2024-2025 by Udo Munk
;
MSIZE	EQU	64		;CP/M memory size in kilobytes
;
;	"bias" is address offset from 3400H for memory systems
;	than 16K (referred to as "b" throughout the text).
;
BIAS	EQU	(MSIZE-20)*1024
CCP	EQU	3400H+BIAS	;base of ccp
BDOS	EQU	CCP+806H	;base of bdos
BIOS	EQU	CCP+1600H	;base of bios
NSECTS	EQU	(BIOS-CCP)/128	;warm start sector count
CDISK	EQU	0004H		;current disk number 0=A,...,15=P
IOBYTE	EQU	0003H		;Intel I/O byte
INITIO	EQU	10010100B	;CON:=TTY, RDR:=PTR:, PUN:=PTP, LST:=LPT:
FDCCMD	EQU	0040H		;FDC command bytes
DDTRK	EQU	0		;offset for track
DDSEC	EQU	1		;offset for sector
DDLDMA	EQU	2		;offset for DMA address low
DDHDMA	EQU	3		;offset for DMA address high
;
;	I/O ports
;
TTY1ST	EQU	0		;tty 1 status
TTY1DA	EQU	1		;tty 1 data
TTY3ST	EQU	7		;tty 3 status
TTY3DA	EQU	8		;tty 3 data
PRTSTA	EQU	5		;printer status port
PRTDAT	EQU	6		;printer data port
FDC	EQU	4		;port for the FDC
LEDS	EQU	0FFH		;frontpanel LED's
;
	ORG	BIOS		;origin of BIOS
;
;	jump vector for individual subroutines
;
	JMP	BOOT		;cold boot
WBE	JMP	WBOOT		;warm start
	JMP	CONST		;console status
	JMP	CONIN		;console character in
	JMP	CONOUT		;console character out
	JMP	LIST		;list character out
	JMP	PUNCH		;punch character out
	JMP	READER		;reader character in
	JMP	HOME		;move disk head to home position
	JMP	SELDSK		;select disk drive
	JMP	SETTRK		;set track number
	JMP	SETSEC		;set sector number
	JMP	SETDMA		;set dma address
	JMP	READ		;read disk sector
	JMP	WRITE		;write disk sector
	JMP	LISTST		;list status
	JMP	SECTRAN		;sector translate
;
;	data tables
;
SIGNON	DB	MSIZE / 10 + '0',MSIZE MOD 10 + '0'
	DB	'K CP/M 2.2 VERS B03',13,10,0
BOOTERR	DB	13,10,'BOOT ERROR',13,10,0
;
;	disk parameter header for disk 0
DPBASE	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK00,ALL00
;	disk parameter header for disk 1
	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK01,ALL01
;	disk parameter header for disk 2
	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK02,ALL02
;	disk parameter header for disk 3
	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK03,ALL03
;
;	sector translate table for IBM 8" SD disks
TRANS	DB	1,7,13,19	;sectors 1,2,3,4
	DB	25,5,11,17	;sectors 5,6,7,8
	DB	23,3,9,15	;sectors 9,10,11,12
	DB	21,2,8,14	;sectors 13,14,15,16
	DB	20,26,6,12	;sectors 17,18,19,20
	DB	18,24,4,10	;sectors 21,22,23,24
	DB	16,22		;sectors 25,26
;
;	disk parameter block for IBM 8" SD disks
DPBLK	DW	26		;sectors per track
	DB	3		;block shift factor
	DB	7		;block mask
	DB	0		;extent mask
	DW	242		;disk size-1
	DW	63		;directory max
	DB	192		;alloc 0
	DB	0		;alloc 1
	DW	16		;check size
	DW	2		;track offset
;
;	print a message to the console
;	pointer to string in hl
;
PRTMSG	MOV	A,M		;get next message byte
	ORA	A		;is it zero?
	RZ			;yes, done
	MOV	C,A		;no, print character on console
	CALL	CONOUT
	INX	H		;and do next
	JMP	PRTMSG
;
;	cold start
;
BOOT	LXI	SP,80H		;use space below buffer for stack
	LXI	H,SIGNON	;print signon
	CALL	PRTMSG
	XRA	A		;zero in the accumulator
	STA	CDISK		;select disk drive 0
	STA	DSKNO
	MVI	A,INITIO	;initialize iobyte
	STA	IOBYTE		;setup IOBYTE
	MVI	A,10H		;setup FDC command
	OUT	FDC
	MVI	A,FDCCMD AND 0FFH
	OUT	FDC
	MVI	A,FDCCMD SHR 8
	OUT	FDC
	STC			;flag for cold start
	CMC
	JMP	GOCPM		;initialize and go to CP/M
;
;	warm start
;
WBOOT	LXI	SP,80H		;use space below buffer for stack
	MVI	C,0		;select disk 0
	CALL	SELDSK
	CALL	HOME		;go to track 0
	MVI	B,NSECTS	;B counts # of sectors to load
	MVI	C,0		;C has the current track #
	MVI	D,2		;D has the next sector to load
	LXI	H,CCP		;base of CP/M
LOAD1	PUSH	B		;save sector count and current track
	PUSH	D		;save next sector to read
	PUSH	H		;save DMA address
	MOV	C,D		;get sector address to C
	CALL	SETSEC		;set sector address
	POP	B		;recall DMA address to BC
	PUSH	B		;and replace on stack for later recall
	CALL	SETDMA		;set DMA address from BC
	CALL	READ		;read sector
	ORA	A		;any errors?
	JZ	LOAD2		;no, continue
	LXI	H,BOOTERR	;otherwise print message
	CALL	PRTMSG
	HLT			;and halt the machine
LOAD2	POP	H		;recall DMA address
	LXI	D,128		;DMA = DMA + 128
	DAD	D		;next DMA address now in HL
	POP	D		;recall sector address
	POP	B		;recall # of sectors remaining
	DCR	B		;sectors = sectors - 1
	STC			;flag for warm start
	JZ	GOCPM		;transfer to CP/M if all loaded
	INR	D		;next sector
	MOV	A,D		;sector = 27 ?
	CPI	27
	JC	LOAD1		;no, continue
	MVI	D,1		;else begin with sector 1 on next track
	INR	C
	CALL	SETTRK
	JMP	LOAD1		;for another sector
GOCPM	MVI	A,0C3H		;C3 is a JMP instruction
	STA	0		;for jmp to wboot
	LXI	H,WBE		;WBOOT entry point
	SHLD	1		;set address for JMP at 0
	STA	5		;for JMP to BDOS
	LXI	H,BDOS		;BDOS entry point
	SHLD	6		;set address for JMP at 5
	LXI	B,80H		;default dma address is 80H
	CALL	SETDMA
	LDA	CDISK		;get current disk number
	MOV	C,A		;send to the CCP
	JC	CCP+3		;go to CCP warm start
	JMP	CCP		;go to CCP cold start
;
;***************************************************************************
;	Logical device routines
;
;	These routines use the physical device routines
;	depending on contents of iobyte.
;***************************************************************************
;
;	Dispatch to one of 4 following addresses
;	depending on contents of two bits of iobyte.
;	Specific bits of iobyte are specified by
;	shift count following the subroutine call.
;
DISPATCH
	XTHL			;save callers HL, get table addr
	MOV	D,M		;get shift count into D
	INX	H		;point to table
	LDA	IOBYTE		;get iobyte
DSHIFT	RLC			;shift to position bits
	DCR	D
	JNZ	DSHIFT
	ANI	06H		;mask bits
	MOV	E,A		;D already clear
	DAD	D		;index into table
	MOV 	A,M		;get table word into HL
	INX	H
	MOV 	H,M
	MOV	L,A
	XTHL			;put addr of routine, get callers HL
	RET			;go to routine
;
;	console status, return 0FFH if character ready, 00H if not
;
CONST	CALL	DISPATCH	;go to one of the physical device routines
	DB	1		;use bits 1-0 of iobyte
	DW	TTY1IS		;00 - TTY:
	DW	DEVNST		;01 - CRT:
	DW	TTY3IS		;10 - BAT:
	DW	TTY3IS		;11 - UC1:
;
;	console input character into register A
;
CONIN	CALL	DISPATCH	;go to one of the physical device routines
	DB	1		;use bits 1-0 of iobyte
	DW	TTY1IN		;00 - TTY:
	DW	DEVNIN		;01 - CRT:
	DW	TTY3IN		;10 - BAT:
	DW	TTY3IN		;11 - UC1:
;
;	console output from character in register C
;
CONOUT	CALL	DISPATCH	;go to one of the physical device routines
	DB	1		;use bits 1-0 of iobyte
	DW	TTY1OU		;00 - TTY:
	DW	DEVNOU		;01 - CRT:
	DW	LPTOUT		;10 - BAT:
	DW	TTY3OU		;11 - UC1:
;
;	printer status, return 0FFH if character ready, 00H if not
;
LISTST	CALL	DISPATCH	;go to one of the physical device routines
	DB	3		;use bits 7-6 of iobyte
	DW	DEVNST		;00 - TTY:
	DW	DEVNST		;01 - CRT:
	DW	LPTST		;10 - LPT:
	DW	TTY3IS		;11 - UL1:
;
;	line printer output
;
LIST	CALL	DISPATCH	;go to one of the physical device routines
	DB	3		;use bits 7-6 of iobyte
	DW	DEVNOU		;00 - TTY:
	DW	DEVNOU		;01 - CRT:
	DW	LPTOUT		;10 - LPT:
	DW	TTY3OU		;11 - UL1:
;
;	punch character from register C
;
PUNCH	CALL	DISPATCH	;go to one of the physical device routines
	DB	5		;use bits 5-4 of iobyte
	DW	DEVNOU		;00 - TTY:
	DW	TTY3OU		;01 - PTP:
	DW	DEVNOU		;10 - UP1:
	DW	DEVNOU		;11 - UP2:
;
;	read character into register A from reader
;
READER	CALL	DISPATCH	;go to one of the physical device routines
	DB	7		;use bits 3-2 OF iobyte
	DW	DEVNIN		;00 - TTY:
	DW	TTY3IN		;01 - RDR:
	DW	DEVNIN		;10 - UR1:
	DW	DEVNIN		;11 - UR2:
;
;***************************************************************************
;	Physical device routines
;
;	Accessed via logical device routines above
;***************************************************************************
;
;	get tty 1 input status
;
TTY1IS	IN	TTY1ST		;get tty 1 status
	RRC			;test bit 0
	JC	TTY1I1		;not ready
	MVI	A,0FFH		;ready, set flag
	RET
TTY1I1	XRA	A		;zero A
	RET
;
;	get tty 1 input
;
TTY1IN	IN	TTY1ST		;get tty 1 status
	RRC			;test bit 0
	JC	TTY1IN		;not ready
	IN	TTY1DA		;get character from tty 1
	RET
;
;	tty 1 output
;
TTY1OU	IN	TTY1ST		;get tty 1 status
	RLC			;test bit 7
	JC	TTY1OU		;wait until transmitter ready
	MOV	A,C		;get character into accumulator
	OUT	TTY1DA		;send to tty 1
	RET
;
;	get tty 3 input status
;
TTY3IS	IN	TTY3ST		;get tty 3 status
	RRC			;test bit 0
	JC	TTY3I1		;not ready
	MVI	A,0FFH		;ready, set flag
	RET
TTY3I1	XRA	A		;zero A
	RET
;
;	get tty 3 input
;
TTY3IN	IN	TTY3ST		;get tty 3 status
	RRC			;test bit 0
	JC	TTY3IN		;not ready
	IN	TTY3DA		;get character from tty 3
	RET
;
;	tty 3 output
;
TTY3OU	IN	TTY3ST		;get tty 3 status
	RLC			;test bit 7
	JC	TTY3OU		;wait until transmitter ready
	MOV	A,C		;get character into accumulator
	OUT	TTY3DA		;send to tty 1
	RET
;
;	printer status, return 0FFH if output ready, 00H if not
;
LPTST	IN	PRTSTA		;get printer status
	RET
;
;	line printer output
;
LPTOUT	IN	PRTSTA		;get printer status
	ORA	A		;ready ?
	JZ	LPTOUT		;wait if not
	MOV	A,C		;get character into accumulator
	OUT	PRTDAT		;send to printer
	RET
;
;	null device
;
DEVNST	MVI	A,0FFH		;this device is always ready
	RET
DEVNIN	MVI	A,01AH		;always returns CP/M EOF
DEVNOU	RET			;and won't output anywhere
;
;	move to track 0 position on current disk
;
HOME	MVI	C,0		;select track 0
	JMP	SETTRK
;
;	select disk given by register C
;
SELDSK	LXI	H,0		;error return code
	MOV	A,C		;get disk # to accumulator
	CPI	4		;disk drive < 4 ?
	JC	SEL1
	RET			;no, return with error
SEL1	STA	DSKNO		;save disk #
	MOV	L,C		;HL = disk #
	DAD	H		;*2
	DAD	H		;*4
	DAD	H		;*8
	DAD	H		;*16 (size of each header)
	LXI	D,DPBASE
	DAD	D		;HL=.DPBASE(DISKNO*16)
	RET
;
;	set track given by register C
;
SETTRK	MOV	A,C		;get to accumulator
	STA	FDCCMD+DDTRK	;set in FDC command
	RET
;
;	set sector given by register C
;
SETSEC	MOV	A,C		;get to accumulator
	STA	FDCCMD+DDSEC	;set in FDC command
	RET
;
;	set DMA address given by registers B and C
;
SETDMA	MOV	A,C		;low order address
	STA	FDCCMD+DDLDMA	;set in FDC command
	MOV	A,B		;high order address
	STA	FDCCMD+DDHDMA	;set in FDC command
	RET
;
;	perform read operation
;
READ	LDA	DSKNO		;get disk #
	ORI	20H		;mask in read command
	OUT	FDC		;ask FDC to execute the command
	IN	FDC		;get status from FDC
	ORA	A		;is it zero?
	RZ			;return if OK
	CMA			;complement for LED's
	OUT	LEDS		;display the error code
	RET			;return with error
;
;	perform write operation
;
WRITE	LDA	DSKNO		;get disk #
	ORI	40H		;mask in write command
	OUT	FDC		;ask FDC to execute the command
	IN	FDC		;get status from FDC
	ORA	A		;is it zero?
	RZ			;return if OK
	CMA			;complement for LED's
	OUT	LEDS		;display the error code
	RET			;return with error
;
;	translate the sector given by BC using
;	the translation table given by DE
;
SECTRAN	XCHG			;HL=.TRANS
	DAD	B		;HL=.TRANS(SECTOR)
	XCHG
	LDAX	D
	MOV	L,A		;L=TRANS(SECTOR)
	MVI	H,0		;HL=TRANS(SECTOR)
	RET			;with value in HL
;
;	The remainder of the CBIOS is reserved uninitialized
;	data area, and does not need to be part of the system
;	memory image. The space must be available, however,
;	between "BEGDAT" and "ENDDAT".
;
BEGDAT	EQU	$		;begin of data area
;
DSKNO	DS	1		;selected disk
;
DIRBF	DS	128		;scratch directory area
ALL00	DS	31		;allocation vector 0
ALL01	DS	31		;allocation vector 1
ALL02	DS	31		;allocation vector 2
ALL03	DS	31		;allocation vector 3
CHK00	DS	16		;check vector 0
CHK01	DS	16		;check vector 1
CHK02	DS	16		;check vector 2
CHK03	DS	16		;check vector 3
;
ENDDAT	EQU	$		;end of data area
DATSIZ	EQU	$-BEGDAT	;size of data area
;
	END			;of CBIOS
