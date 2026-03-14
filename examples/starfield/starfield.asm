;*********************************************************
;*  Starfield Demo - Amiga hardware-banging assembly     *
;*                                                        *
;*  Stars radiate from center at varying speeds.          *
;*  Occasional USS Enterprise crosses the screen.         *
;*  Uses Copper for gradient background, Blitter for      *
;*  screen clear, direct pixel plotting for stars.        *
;*                                                        *
;*  3 bitplanes (8 colors) - 320x256 PAL lowres           *
;*  CPU: 68020+                                           *
;*********************************************************

	section	code,code

;--- Custom chip register offsets ---
DMACONR		equ	$002
VPOSR		equ	$004
VHPOSR		equ	$006
BLTCON0		equ	$040
BLTCON1		equ	$042
BLTAFWM		equ	$044
BLTALWM		equ	$046
BLTCPTH		equ	$048
BLTCPTL		equ	$04A
BLTBPTH		equ	$04C
BLTBPTL		equ	$04E
BLTAPTH		equ	$050
BLTAPTL		equ	$052
BLTDPTH		equ	$054
BLTDPTL		equ	$056
BLTSIZE		equ	$058
BLTCMOD		equ	$060
BLTBMOD		equ	$062
BLTAMOD		equ	$064
BLTDMOD		equ	$066
COP1LCH		equ	$080
COP1LCL		equ	$082
COP2LCH		equ	$084
COP2LCL		equ	$086
COPJMP1		equ	$088
DIWSTRT		equ	$08E
DIWSTOP		equ	$090
DDFSTRT		equ	$092
DDFSTOP		equ	$094
DMACON		equ	$096
INTENA		equ	$09A
INTREQ		equ	$09C
BPL1PTH		equ	$0E0
BPL1PTL		equ	$0E2
BPL2PTH		equ	$0E4
BPL2PTL		equ	$0E6
BPL3PTH		equ	$0E8
BPL3PTL		equ	$0EA
BPLCON0		equ	$100
BPLCON1		equ	$102
BPLCON2		equ	$104
BPL1MOD		equ	$108
BPL2MOD		equ	$10A
COLOR00		equ	$180

;--- Screen constants ---
SCR_W		equ	320
SCR_H		equ	256
SCR_BPL_SIZE	equ	(SCR_W/8)*SCR_H	; 10240 bytes per bitplane
NUM_BPL		equ	3			; 8 colors
BPLROW		equ	SCR_W/8			; 40 bytes per row

;--- Star constants ---
MAX_STARS	equ	200
STAR_STRUCT_SIZE equ	16		; dx.w, dy.w, x.l (16.16), y.l (16.16), speed.w, color.w

;--- Enterprise constants ---
ENT_W		equ	64			; sprite width in pixels
ENT_H		equ	16			; sprite height
ENT_INTERVAL	equ	600			; frames between appearances

;*********************************************************
;*  Exported symbols for C startup                       *
;*********************************************************
	xdef	_sf_main
	xdef	_sf_frame_count
	xdef	_sf_star_count

;*********************************************************
;*  sf_main(a0=custom_base, a1=chipmem)                  *
;*  Main entry point called from C.                      *
;*  C calling convention: args on stack.                  *
;*********************************************************
_sf_main:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	4+11*4(sp),a5		; a5 = custom base ($dff000)
	move.l	8+11*4(sp),a4		; a4 = chip memory base

	; Store chipmem base for later
	move.l	a4,chipmem_base

	; Set up bitplane pointers
	; bpl0 = chipmem + 0
	; bpl1 = chipmem + SCR_BPL_SIZE
	; bpl2 = chipmem + SCR_BPL_SIZE*2
	move.l	a4,d0
	move.l	d0,bpl0_ptr
	addi.l	#SCR_BPL_SIZE,d0
	move.l	d0,bpl1_ptr
	addi.l	#SCR_BPL_SIZE,d0
	move.l	d0,bpl2_ptr

	; Initialize the copper list
	bsr	init_copper

	; Initialize stars
	bsr	init_stars

	; Initialize Enterprise
	clr.l	ent_active
	move.l	#ENT_INTERVAL,ent_timer

	; Set up display
	; 3 bitplanes, color on, lowres
	move.w	#$3200,BPLCON0(a5)	; 3 bpl, color
	move.w	#$0000,BPLCON1(a5)	; no scroll
	move.w	#$0024,BPLCON2(a5)	; sprites have priority
	move.w	#$0038,DDFSTRT(a5)	; data fetch start
	move.w	#$00D0,DDFSTOP(a5)	; data fetch stop
	move.w	#$2C81,DIWSTRT(a5)	; display window start
	move.w	#$2CC1,DIWSTOP(a5)	; display window stop
	move.w	#0,BPL1MOD(a5)		; modulo = 0 (single playfield)
	move.w	#0,BPL2MOD(a5)

	; Load copper list
	move.l	copper_list_ptr,d0
	move.w	d0,COP1LCL(a5)
	swap	d0
	move.w	d0,COP1LCH(a5)
	move.w	d0,COPJMP1(a5)		; strobe copper restart

	; Enable DMA: bitplane, copper, blitter
	move.w	#$83E0,DMACON(a5)	; SET copper+bpl+blitter+sprite DMA

	; Enable VERTB interrupt for vblank sync
	move.w	#$C020,INTENA(a5)	; SET VERTB interrupt

;*********************************************************
;*  Main loop                                            *
;*********************************************************
.mainloop:
	; Wait for vertical blank
	bsr	wait_vblank

	; Clear all 3 bitplanes via blitter
	bsr	blit_clear_screen

	; Update stars
	bsr	update_stars

	; Plot stars
	bsr	plot_stars

	; Update & draw Enterprise
	bsr	update_enterprise
	tst.l	ent_active
	beq.s	.no_ent
	bsr	draw_enterprise
.no_ent:

	; Update copper list with bitplane pointers
	bsr	update_copper

	; Increment frame counter
	addq.l	#1,_sf_frame_count

	; Check for mouse button (LMB = exit)
	btst	#6,$bfe001
	bne	.mainloop

	; Done
	movem.l	(sp)+,d2-d7/a2-a6
	rts

;*********************************************************
;*  wait_vblank - Wait for vertical blank via VPOSR      *
;*********************************************************
wait_vblank:
.wv1:	move.l	VPOSR(a5),d0
	andi.l	#$1FF00,d0
	cmpi.l	#$00100,d0		; wait for line $100 (256) - end of visible area
	bne.s	.wv1
.wv2:	move.l	VPOSR(a5),d0
	andi.l	#$1FF00,d0
	cmpi.l	#$00100,d0		; wait until we leave line 256
	beq.s	.wv2
	rts

;*********************************************************
;*  blit_clear_screen - Clear all bitplanes via blitter  *
;*********************************************************
blit_clear_screen:
	; Wait for blitter ready
.bwait:	btst	#14,DMACONR(a5)
	bne.s	.bwait

	; Clear bpl0
	move.l	bpl0_ptr,d0
	move.w	d0,BLTDPTL(a5)
	swap	d0
	move.w	d0,BLTDPTH(a5)
	move.w	#$0100,BLTCON0(a5)	; use D only, clear
	move.w	#$0000,BLTCON1(a5)
	move.w	#$0000,BLTDMOD(a5)
	move.w	#$FFFF,BLTAFWM(a5)
	move.w	#$FFFF,BLTALWM(a5)
	; BLTSIZE: height in upper 10 bits, width in words in lower 6 bits
	; 256 lines * 20 words = $1014 (256<<6 | 20)
	move.w	#(SCR_H<<6)|(SCR_W/16),BLTSIZE(a5)

	; Wait for blitter, clear bpl1
.bwait2: btst	#14,DMACONR(a5)
	bne.s	.bwait2
	move.l	bpl1_ptr,d0
	move.w	d0,BLTDPTL(a5)
	swap	d0
	move.w	d0,BLTDPTH(a5)
	move.w	#(SCR_H<<6)|(SCR_W/16),BLTSIZE(a5)

	; Wait for blitter, clear bpl2
.bwait3: btst	#14,DMACONR(a5)
	bne.s	.bwait3
	move.l	bpl2_ptr,d0
	move.w	d0,BLTDPTL(a5)
	swap	d0
	move.w	d0,BLTDPTH(a5)
	move.w	#(SCR_H<<6)|(SCR_W/16),BLTSIZE(a5)

	; Wait for blitter to finish before we plot
.bwait4: btst	#14,DMACONR(a5)
	bne.s	.bwait4
	rts

;*********************************************************
;*  init_stars - Initialize star array                   *
;*  Each star: dx.w, dy.w, x.l (16.16 fixed), y.l,      *
;*             speed.w, color.w = 16 bytes                *
;*********************************************************
init_stars:
	lea	star_data,a0
	move.w	#MAX_STARS-1,d7
	; Seed the random number generator
	move.l	#$12345678,random_seed
.init_loop:
	; Random angle via dx/dy
	bsr	random
	; dx = random in range -127..+127
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,(a0)+		; dx

	bsr	random
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,(a0)+		; dy

	; Start position = center (160.0, 128.0) in 16.16 fixed point
	move.l	#160<<16,(a0)+		; x = 160.0
	move.l	#128<<16,(a0)+		; y = 128.0

	; Speed: random 1-4
	bsr	random
	andi.w	#3,d0
	addq.w	#1,d0
	move.w	d0,(a0)+		; speed

	; Color based on speed: faster = brighter
	; colors: 1=dim, 2=med, 3=bright, 4-7=brightest
	move.w	d0,d1
	addq.w	#2,d1
	cmpi.w	#7,d1
	ble.s	.col_ok
	move.w	#7,d1
.col_ok:
	move.w	d1,(a0)+		; color (1-7)

	dbf	d7,.init_loop

	move.l	#MAX_STARS,_sf_star_count
	rts

;*********************************************************
;*  update_stars - Move stars, reset if off screen        *
;*********************************************************
update_stars:
	lea	star_data,a0
	move.w	#MAX_STARS-1,d7

.upd_loop:
	; Load star data
	move.w	(a0),d0			; dx
	move.w	2(a0),d1		; dy
	move.l	4(a0),d2		; x (16.16)
	move.l	8(a0),d3		; y (16.16)
	move.w	12(a0),d4		; speed

	; Multiply velocity by speed and add to position
	; dx and dy are -128..127, speed is 1-4
	; We scale: x += dx * speed * 64 (shift left 6 for 16.16 sub-pixel)
	muls.w	d4,d0			; d0 = dx * speed (word result)
	ext.l	d0
	asl.l	#6,d0			; scale to 16.16 fraction
	add.l	d0,d2			; new x

	muls.w	d4,d1			; d1 = dy * speed
	ext.l	d1
	asl.l	#6,d1
	add.l	d1,d3			; new y

	; Store updated position
	move.l	d2,4(a0)
	move.l	d3,8(a0)

	; Check bounds (integer part = upper 16 bits)
	swap	d2			; d2.w = x integer
	swap	d3			; d3.w = y integer

	; Off screen? Reset to center with new random direction
	cmpi.w	#0,d2
	blt.s	.reset
	cmpi.w	#SCR_W,d2
	bge.s	.reset
	cmpi.w	#0,d3
	blt.s	.reset
	cmpi.w	#SCR_H,d3
	bge.s	.reset
	; Star is still on screen
	lea	STAR_STRUCT_SIZE(a0),a0
	dbf	d7,.upd_loop
	rts

.reset:
	; Reset to center with new random direction
	bsr	random
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,(a0)			; new dx

	bsr	random
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,2(a0)		; new dy

	; If both dx and dy are 0, force movement
	tst.w	(a0)
	bne.s	.dir_ok
	tst.w	2(a0)
	bne.s	.dir_ok
	move.w	#32,(a0)		; force some dx
.dir_ok:

	move.l	#160<<16,4(a0)		; x = center
	move.l	#128<<16,8(a0)		; y = center

	; New random speed 1-4
	bsr	random
	andi.w	#3,d0
	addq.w	#1,d0
	move.w	d0,12(a0)

	; Color from speed
	move.w	d0,d1
	addq.w	#2,d1
	cmpi.w	#7,d1
	ble.s	.col_ok2
	move.w	#7,d1
.col_ok2:
	move.w	d1,14(a0)

	lea	STAR_STRUCT_SIZE(a0),a0
	dbf	d7,.upd_loop
	rts

;*********************************************************
;*  plot_stars - Plot each star as a pixel                *
;*  Color determines which bitplanes get the pixel set.   *
;*********************************************************
plot_stars:
	lea	star_data,a0
	move.l	bpl0_ptr,a1
	move.l	bpl1_ptr,a2
	move.l	bpl2_ptr,a3
	move.w	#MAX_STARS-1,d7

.plot_loop:
	; Get integer x,y from 16.16 fixed point
	move.l	4(a0),d0		; x fixed
	swap	d0			; d0.w = x integer
	move.l	8(a0),d1		; y fixed
	swap	d1			; d1.w = y integer

	; Bounds check (should be in range but be safe)
	cmpi.w	#0,d0
	blt.s	.skip
	cmpi.w	#SCR_W-1,d0
	bgt.s	.skip
	cmpi.w	#0,d1
	blt.s	.skip
	cmpi.w	#SCR_H-1,d1
	bgt.s	.skip

	; Calculate byte offset: y * 40 + (x / 8)
	; And bit number: 7 - (x & 7)
	move.w	d1,d2
	mulu.w	#BPLROW,d2		; d2 = y * 40
	move.w	d0,d3
	lsr.w	#3,d3			; d3 = x / 8
	add.w	d3,d2			; d2 = byte offset

	move.w	d0,d3
	not.w	d3
	andi.w	#7,d3			; d3 = bit number (7 - (x&7))

	; Get color (which planes to set)
	move.w	14(a0),d4		; color 1-7

	; Set pixel in appropriate bitplanes
	btst	#0,d4
	beq.s	.no_bpl0
	bset	d3,0(a1,d2.w)
.no_bpl0:
	btst	#1,d4
	beq.s	.no_bpl1
	bset	d3,0(a2,d2.w)
.no_bpl1:
	btst	#2,d4
	beq.s	.no_bpl2
	bset	d3,0(a3,d2.w)
.no_bpl2:

.skip:
	lea	STAR_STRUCT_SIZE(a0),a0
	dbf	d7,.plot_loop
	rts

;*********************************************************
;*  Enterprise routines                                  *
;*********************************************************

update_enterprise:
	tst.l	ent_active
	bne.s	.ent_moving

	; Count down to next appearance
	subq.l	#1,ent_timer
	bgt.s	.ent_done

	; Activate Enterprise
	move.l	#1,ent_active

	; Random Y position 40-200
	bsr	random
	andi.w	#$7F,d0			; 0-127
	addi.w	#40,d0
	move.w	d0,ent_y

	; Start from right side, move left
	move.w	#SCR_W+ENT_W,ent_x
	move.w	#-3,ent_dx		; move left at 3 pixels/frame
	rts

.ent_moving:
	; Move Enterprise
	move.w	ent_dx,d0
	add.w	d0,ent_x

	; Off screen left?
	move.w	ent_x,d0
	cmpi.w	#-ENT_W-16,d0
	bgt.s	.ent_done

	; Deactivate
	clr.l	ent_active
	move.l	#ENT_INTERVAL,ent_timer

.ent_done:
	rts

;*********************************************************
;*  draw_enterprise - Draw the Enterprise sprite data    *
;*  as pixels in the bitplanes.                          *
;*  Simple 1-bit shape drawn in color 7 (white).         *
;*********************************************************
draw_enterprise:
	move.w	ent_x,d5		; Enterprise x position
	move.w	ent_y,d6		; Enterprise y position
	lea	enterprise_gfx,a0	; pointer to shape data (ENT_H words)
	move.l	bpl0_ptr,a1
	move.l	bpl1_ptr,a2
	move.l	bpl2_ptr,a3
	move.w	#ENT_H-1,d7		; row counter

.ent_row:
	; Get the shape data for this row (64 bits = 4 words)
	move.w	(a0)+,d0		; pixels 0-15
	move.w	(a0)+,d1		; pixels 16-31
	move.w	(a0)+,d2		; pixels 32-47
	move.w	(a0)+,d3		; pixels 48-63

	; Current screen Y
	move.w	d6,d4
	add.w	d7,d4			; row on screen (we draw bottom-up, but let's fix)
	; Actually draw top-down: row = ent_y + (ENT_H-1 - d7)
	move.w	#ENT_H-1,d4
	sub.w	d7,d4
	add.w	d6,d4			; d4 = screen Y for this row

	; Bounds check Y
	cmpi.w	#0,d4
	blt.s	.ent_skip_row
	cmpi.w	#SCR_H-1,d4
	bgt.s	.ent_skip_row

	; For each set bit in the 64-bit shape, plot at (ent_x + bitpos, d4)
	; Process word 0 (bits 15..0 = pixels 0..15)
	move.w	d5,-(sp)		; save ent_x
	bsr	.draw_ent_word		; d0=word data, d5=x offset of first pixel in word, d4=y
	move.w	(sp),d5
	addi.w	#16,d5
	move.w	d1,d0
	bsr	.draw_ent_word
	move.w	(sp),d5
	addi.w	#32,d5
	move.w	d2,d0
	bsr	.draw_ent_word
	move.w	(sp)+,d5
	addi.w	#48,d5
	move.w	d3,d0
	bsr	.draw_ent_word
	move.w	ent_x,d5		; restore d5

.ent_skip_row:
	dbf	d7,.ent_row
	rts

; Draw one 16-pixel word of Enterprise shape
; d0.w = pixel mask, d5.w = screen x of leftmost pixel, d4.w = screen y
; Trashes d0, preserves d4, d5
.draw_ent_word:
	tst.w	d0
	beq.s	.dew_done		; skip if no pixels
	movem.l	d1-d3/d5,-(sp)
	moveq	#15,d1			; bit counter

.dew_bit:
	btst	d1,d0
	beq.s	.dew_next

	; Plot pixel at (d5 + (15-d1), d4) in all 3 planes (color 7 = white)
	move.w	#15,d2
	sub.w	d1,d2
	add.w	d5,d2			; d2 = screen x

	; Bounds check
	cmpi.w	#0,d2
	blt.s	.dew_next
	cmpi.w	#SCR_W-1,d2
	bgt.s	.dew_next

	; Byte offset = d4 * 40 + d2/8
	move.w	d4,d3
	mulu.w	#BPLROW,d3
	move.w	d2,-(sp)
	lsr.w	#3,d2
	add.w	d2,d3			; byte offset
	move.w	(sp)+,d2
	not.w	d2
	andi.w	#7,d2			; bit position

	; Set in all 3 bitplanes (color 7)
	bset	d2,0(a1,d3.w)
	bset	d2,0(a2,d3.w)
	bset	d2,0(a3,d3.w)

.dew_next:
	dbf	d1,.dew_bit
	movem.l	(sp)+,d1-d3/d5
.dew_done:
	rts

;*********************************************************
;*  init_copper - Build copper list in chip memory        *
;*  Sets palette, bitplane pointers, and background       *
;*  gradient (deep space blue to black).                  *
;*********************************************************
init_copper:
	; Copper list goes at end of bitplane area
	move.l	chipmem_base,d0
	addi.l	#SCR_BPL_SIZE*3,d0	; after 3 bitplanes
	move.l	d0,copper_list_ptr

	move.l	d0,a0

	; --- Set palette ---
	; Color 0: deep space (overridden by gradient)
	move.w	#COLOR00,d1
	move.w	d1,(a0)+
	move.w	#$0002,(a0)+		; very dark blue

	; Color 1: dim star (dark grey)
	move.w	#COLOR00+2,(a0)+
	move.w	#$0444,(a0)+

	; Color 2: medium star
	move.w	#COLOR00+4,(a0)+
	move.w	#$0888,(a0)+

	; Color 3: bright star
	move.w	#COLOR00+6,(a0)+
	move.w	#$0BBB,(a0)+

	; Color 4: brighter
	move.w	#COLOR00+8,(a0)+
	move.w	#$0CCC,(a0)+

	; Color 5: hot star (slight blue tint)
	move.w	#COLOR00+10,(a0)+
	move.w	#$0ADF,(a0)+

	; Color 6: very bright
	move.w	#COLOR00+12,(a0)+
	move.w	#$0EEF,(a0)+

	; Color 7: white (Enterprise + brightest stars)
	move.w	#COLOR00+14,(a0)+
	move.w	#$0FFF,(a0)+

	; --- Bitplane pointers (will be updated each frame) ---
	; BPL1PT
	move.w	#BPL1PTH,(a0)+
	move.w	#0,(a0)+		; placeholder high
	move.w	#BPL1PTL,(a0)+
	move.w	#0,(a0)+		; placeholder low
	; BPL2PT
	move.w	#BPL2PTH,(a0)+
	move.w	#0,(a0)+
	move.w	#BPL2PTL,(a0)+
	move.w	#0,(a0)+
	; BPL3PT
	move.w	#BPL3PTH,(a0)+
	move.w	#0,(a0)+
	move.w	#BPL3PTL,(a0)+
	move.w	#0,(a0)+

	; Save pointer to where BPL pointers are in the copper list
	; They start 8 colors * 2 longs = 16 longs = 64 bytes into the list
	move.l	copper_list_ptr,d0
	addi.l	#8*4,d0		; past 8 color instructions
	move.l	d0,copper_bpl_ptrs

	; --- Background gradient (subtle deep space) ---
	; Create a gentle gradient from dark blue at top to black at bottom
	; We'll do every 16 lines
	move.w	#$2C,d1			; start at line $2C (top of display)
	move.w	#$0003,d2		; starting color (dark blue)

	moveq	#15,d3			; 16 gradient steps
.grad_loop:
	; WAIT instruction: (vpos<<8)|$01, $FFFE
	move.w	d1,d0
	lsl.w	#8,d0
	ori.w	#$01,d0
	move.w	d0,(a0)+		; WAIT vpos
	move.w	#$FFFE,(a0)+		; WAIT mask

	; Set COLOR00
	move.w	#COLOR00,(a0)+
	move.w	d2,(a0)+

	; Darken: reduce blue component
	; Gradient: $0003, $0003, $0002, $0002, $0002, $0001, $0001, ... $0000
	cmpi.w	#7,d3
	bgt.s	.no_dark
	subq.w	#1,d2
	bpl.s	.no_dark
	clr.w	d2
.no_dark:

	addi.w	#16,d1			; next 16 lines
	; Handle vpos > $FF
	cmpi.w	#$100,d1
	blt.s	.no_wrap
	; For PAL, lines >= 256 need special WAIT
	; We'll stop the gradient at line 255
	bra.s	.grad_done
.no_wrap:
	dbf	d3,.grad_loop
.grad_done:

	; --- End of copper list ---
	move.l	#$FFFFFFFE,(a0)+	; COPEND

	rts

;*********************************************************
;*  update_copper - Update BPL pointers in copper list   *
;*********************************************************
update_copper:
	move.l	copper_bpl_ptrs,a0

	; BPL1PT
	move.l	bpl0_ptr,d0
	swap	d0
	move.w	d0,2(a0)		; high word
	swap	d0
	move.w	d0,6(a0)		; low word

	; BPL2PT
	move.l	bpl1_ptr,d0
	swap	d0
	move.w	d0,10(a0)		; high word
	swap	d0
	move.w	d0,14(a0)		; low word

	; BPL3PT
	move.l	bpl2_ptr,d0
	swap	d0
	move.w	d0,18(a0)		; high word
	swap	d0
	move.w	d0,22(a0)		; low word

	rts

;*********************************************************
;*  random - Simple LFSR pseudo-random number generator  *
;*  Returns: d0 = random 16-bit value                    *
;*  Trashes: d0 only                                     *
;*********************************************************
random:
	; xorshift32 PRNG
	move.l	random_seed,d0
	move.l	d0,-(sp)
	lsl.l	#7,d0
	eor.l	d0,(sp)
	move.l	(sp),d0
	lsr.l	#5,d0
	eor.l	d0,(sp)
	move.l	(sp),d0
	lsl.l	#3,d0
	eor.l	d0,(sp)
	move.l	(sp)+,d0
	move.l	d0,random_seed
	rts

;*********************************************************
;*  Enterprise shape data                                *
;*  64 pixels wide x 16 pixels tall, 1-bit               *
;*  Each row = 4 words (64 bits)                         *
;*  Classic side-view silhouette                          *
;*********************************************************

	section	data,data

enterprise_gfx:
	; Row 0:  saucer top (narrow)
	dc.w	$0000,$0000,$07E0,$0000
	; Row 1:  saucer wider
	dc.w	$0000,$0000,$1FF8,$0000
	; Row 2:  saucer full width
	dc.w	$0000,$0001,$FFFC,$0000
	; Row 3:  saucer with neck starting
	dc.w	$0000,$0003,$FFFE,$0000
	; Row 4:  saucer bottom + neck
	dc.w	$0000,$0001,$FFFC,$0000
	; Row 5:  neck (thin connecting section)
	dc.w	$0000,$0000,$07E0,$0000
	; Row 6:  neck
	dc.w	$0000,$0000,$07E0,$0000
	; Row 7:  neck widens to engineering hull
	dc.w	$0000,$0000,$0FF0,$0000
	; Row 8:  engineering hull starts
	dc.w	$0000,$0000,$3FFC,$0000
	; Row 9:  engineering hull + nacelle pylons
	dc.w	$3800,$0000,$7FFE,$001C
	; Row 10: nacelle pylons extend
	dc.w	$7C00,$0000,$FFFF,$003E
	; Row 11: nacelles start
	dc.w	$FE00,$0000,$FFFF,$007F
	; Row 12: nacelles full + hull
	dc.w	$FF00,$0000,$7FFE,$00FF
	; Row 13: nacelles full + hull
	dc.w	$FF00,$0000,$3FFC,$00FF
	; Row 14: nacelles tapering
	dc.w	$7E00,$0000,$1FF8,$007E
	; Row 15: nacelle tips + hull end
	dc.w	$3C00,$0000,$07E0,$003C

;*********************************************************
;*  BSS data                                             *
;*********************************************************

	section	bss,bss

_sf_frame_count:	ds.l	1
_sf_star_count:		ds.l	1
random_seed:		ds.l	1
chipmem_base:		ds.l	1
bpl0_ptr:		ds.l	1
bpl1_ptr:		ds.l	1
bpl2_ptr:		ds.l	1
copper_list_ptr:	ds.l	1
copper_bpl_ptrs:	ds.l	1

; Enterprise state
ent_active:		ds.l	1
ent_timer:		ds.l	1
ent_x:			ds.w	1
ent_y:			ds.w	1
ent_dx:			ds.w	1

	cnop	0,4

; Star array: MAX_STARS * STAR_STRUCT_SIZE bytes
star_data:		ds.b	MAX_STARS*STAR_STRUCT_SIZE
