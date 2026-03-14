;*********************************************************
;*  Starfield Demo - Amiga hardware-banging assembly     *
;*                                                        *
;*  Stars radiate from screen center.                     *
;*  WASD/arrow keys steer the view (sphere projection).  *
;*  USS Enterprise crosses the screen periodically.       *
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
BLTDPTH		equ	$054
BLTDPTL		equ	$056
BLTSIZE		equ	$058
BLTDMOD		equ	$066
COP1LCH		equ	$080
COP1LCL		equ	$082
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
NUM_BPL		equ	3
BPLROW		equ	SCR_W/8			; 40 bytes per row

;--- Star constants ---
MAX_STARS	equ	200
STAR_STRUCT_SIZE equ	16		; dx.w, dy.w, x.l (16.16), y.l (16.16), speed.w, color.w

;--- Enterprise constants ---
ENT_W		equ	96			; width in pixels
ENT_H		equ	48			; height
ENT_WORDS	equ	6			; words per row
ENT_INTERVAL	equ	500			; frames between appearances
ENT_SPEED	equ	2			; pixels per frame

;--- Steering ---
STEER_ACCEL	equ	$10000			; 1.0 pixel per frame in 16.16

;*********************************************************
;*  Exported symbols for C startup                       *
;*********************************************************
	xdef	_sf_main
	xdef	_sf_frame_count
	xdef	_sf_star_count
	xdef	_sf_key_up
	xdef	_sf_key_down
	xdef	_sf_key_left
	xdef	_sf_key_right
	xdef	_sf_key_esc

;*********************************************************
;*  External: C functions                                *
;*********************************************************
	xref	_sf_read_keys
	xref	_sf_start_music

;*********************************************************
;*  sf_main(custom, chipmem)                             *
;*  C calling convention: args on stack.                  *
;*  Returns when ESC pressed or LMB clicked.             *
;*********************************************************
_sf_main:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	4+11*4(sp),a5		; a5 = custom base ($dff000)
	move.l	8+11*4(sp),a4		; a4 = chip memory base

	; Store chipmem base
	move.l	a4,chipmem_base

	; Set up bitplane pointers
	move.l	a4,d0
	move.l	d0,bpl0_ptr
	addi.l	#SCR_BPL_SIZE,d0
	move.l	d0,bpl1_ptr
	addi.l	#SCR_BPL_SIZE,d0
	move.l	d0,bpl2_ptr

	; Clear steering velocity
	clr.l	steer_vx
	clr.l	steer_vy

	; Initialize the copper list
	bsr	init_copper

	; Initialize stars
	bsr	init_stars

	; Initialize Enterprise
	clr.l	ent_active
	move.l	#ENT_INTERVAL,ent_timer

	; Set up display registers
	move.w	#$3200,BPLCON0(a5)	; 3 bpl, color
	move.w	#$0000,BPLCON1(a5)
	move.w	#$0024,BPLCON2(a5)
	move.w	#$0038,DDFSTRT(a5)
	move.w	#$00D0,DDFSTOP(a5)
	move.w	#$2C81,DIWSTRT(a5)
	move.w	#$2CC1,DIWSTOP(a5)
	move.w	#0,BPL1MOD(a5)
	move.w	#0,BPL2MOD(a5)

	; Load copper list
	move.l	copper_list_ptr,d0
	move.w	d0,COP1LCL(a5)
	swap	d0
	move.w	d0,COP1LCH(a5)
	move.w	d0,COPJMP1(a5)

	; Enable DMA: DMAEN(9) + BPLEN(8) + COPEN(7) + BLTEN(6)
	move.w	#$83C0,DMACON(a5)	; SET DMAEN+BPLEN+COPEN+BLTEN

	; Audio DMA for ptplayer (channels 0-3)
	move.w	#$800F,DMACON(a5)	; SET AUD0+AUD1+AUD2+AUD3

	; Enable interrupts: master + VERTB + EXTER (CIA-B for ptplayer)
	move.w	#$E028,INTENA(a5)	; SET master + VERTB + EXTER

	; Set up copper BPL pointers for first frame
	bsr	update_copper

	; Start music now that DMA and interrupts are configured
	jsr	_sf_start_music

;*********************************************************
;*  Main loop                                            *
;*********************************************************
.mainloop:
	; Wait for vertical blank
	bsr	wait_vblank

	; Read keyboard (calls C function)
	jsr	_sf_read_keys

	; Check ESC
	tst.b	_sf_key_esc
	bne	.exit

	; Handle steering: set velocity for star field shift
	bsr	handle_steering

	; Clear all 3 bitplanes via blitter
	bsr	blit_clear_screen

	; Update and plot stars
	bsr	update_stars
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

	; Check LMB (also exit)
	btst	#6,$bfe001
	bne	.mainloop

.exit:
	movem.l	(sp)+,d2-d7/a2-a6
	rts

;*********************************************************
;*  handle_steering - Set steer velocity from key state  *
;*  Stars shift in the pressed direction (sphere effect) *
;*********************************************************
handle_steering:
	; Reset velocity each frame
	clr.l	steer_vx
	clr.l	steer_vy

	; Left
	tst.b	_sf_key_left
	beq.s	.no_left
	move.l	#-STEER_ACCEL,steer_vx
.no_left:
	; Right
	tst.b	_sf_key_right
	beq.s	.no_right
	move.l	#STEER_ACCEL,steer_vx
.no_right:
	; Up
	tst.b	_sf_key_up
	beq.s	.no_up
	move.l	#-STEER_ACCEL,steer_vy
.no_up:
	; Down
	tst.b	_sf_key_down
	beq.s	.no_down
	move.l	#STEER_ACCEL,steer_vy
.no_down:
	rts

;*********************************************************
;*  wait_vblank - Wait for vertical blank via VPOSR      *
;*********************************************************
wait_vblank:
.wv1:	move.l	VPOSR(a5),d0
	andi.l	#$1FF00,d0
	cmpi.l	#$00100,d0
	bne.s	.wv1
.wv2:	move.l	VPOSR(a5),d0
	andi.l	#$1FF00,d0
	cmpi.l	#$00100,d0
	beq.s	.wv2
	rts

;*********************************************************
;*  blit_clear_screen - Clear all bitplanes via blitter  *
;*********************************************************
blit_clear_screen:
.bwait:	btst	#14,DMACONR(a5)
	bne.s	.bwait

	move.l	bpl0_ptr,d0
	move.w	d0,BLTDPTL(a5)
	swap	d0
	move.w	d0,BLTDPTH(a5)
	move.w	#$0100,BLTCON0(a5)
	move.w	#$0000,BLTCON1(a5)
	move.w	#$0000,BLTDMOD(a5)
	move.w	#$FFFF,BLTAFWM(a5)
	move.w	#$FFFF,BLTALWM(a5)
	move.w	#(SCR_H<<6)|(SCR_W/16),BLTSIZE(a5)

.bwait2: btst	#14,DMACONR(a5)
	bne.s	.bwait2
	move.l	bpl1_ptr,d0
	move.w	d0,BLTDPTL(a5)
	swap	d0
	move.w	d0,BLTDPTH(a5)
	move.w	#(SCR_H<<6)|(SCR_W/16),BLTSIZE(a5)

.bwait3: btst	#14,DMACONR(a5)
	bne.s	.bwait3
	move.l	bpl2_ptr,d0
	move.w	d0,BLTDPTL(a5)
	swap	d0
	move.w	d0,BLTDPTH(a5)
	move.w	#(SCR_H<<6)|(SCR_W/16),BLTSIZE(a5)

.bwait4: btst	#14,DMACONR(a5)
	bne.s	.bwait4
	rts

;*********************************************************
;*  init_stars - Initialize star array                   *
;*********************************************************
init_stars:
	lea	star_data,a0
	move.w	#MAX_STARS-1,d7
	move.w	VHPOSR(a5),d0		; seed from beam position
	swap	d0
	move.w	VHPOSR(a5),d0
	ori.l	#$A3B5C7D9,d0		; ensure non-zero
	move.l	d0,random_seed
.init_loop:
	bsr	random
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,(a0)+		; dx

	bsr	random
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,(a0)+		; dy

	; Start at screen center (16.16 fixed)
	move.l	#160<<16,(a0)+		; x
	move.l	#128<<16,(a0)+		; y

	; Randomize initial position by scattering
	bsr	random
	andi.w	#$FF,d0
	ext.l	d0
	asl.l	#8,d0			; random sub-pixel offset x
	add.l	d0,-8(a0)
	bsr	random
	andi.w	#$FF,d0
	ext.l	d0
	asl.l	#8,d0
	add.l	d0,-4(a0)

	bsr	random
	andi.w	#3,d0
	addq.w	#1,d0
	move.w	d0,(a0)+		; speed 1-4

	move.w	d0,d1
	addq.w	#2,d1
	cmpi.w	#7,d1
	ble.s	.col_ok
	move.w	#7,d1
.col_ok:
	move.w	d1,(a0)+		; color

	dbf	d7,.init_loop
	move.l	#MAX_STARS,_sf_star_count
	rts

;*********************************************************
;*  update_stars - Move stars outward + apply steering   *
;*  d5 = steer_vx, d6 = steer_vy (loaded before loop)   *
;*********************************************************
update_stars:
	lea	star_data,a0
	move.w	#MAX_STARS-1,d7
	move.l	steer_vx,d5
	move.l	steer_vy,d6

.upd_loop:
	move.w	(a0),d0			; dx
	move.w	2(a0),d1		; dy
	move.l	4(a0),d2		; x (16.16)
	move.l	8(a0),d3		; y (16.16)
	move.w	12(a0),d4		; speed

	muls.w	d4,d0
	ext.l	d0
	asl.l	#6,d0
	add.l	d0,d2

	muls.w	d4,d1
	ext.l	d1
	asl.l	#6,d1
	add.l	d1,d3

	; Apply steering velocity (shifts all stars)
	add.l	d5,d2
	add.l	d6,d3

	move.l	d2,4(a0)
	move.l	d3,8(a0)

	; Check bounds
	swap	d2
	swap	d3
	tst.w	d2
	blt.s	.reset
	cmpi.w	#SCR_W,d2
	bge.s	.reset
	tst.w	d3
	blt.s	.reset
	cmpi.w	#SCR_H,d3
	bge.s	.reset

	lea	STAR_STRUCT_SIZE(a0),a0
	dbf	d7,.upd_loop
	rts

.reset:
	; Reset to screen center with new random direction
	bsr	random
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,(a0)			; new dx
	bsr	random
	andi.w	#$00FF,d0
	subi.w	#128,d0
	move.w	d0,2(a0)		; new dy

	; Ensure at least one component is non-zero
	tst.w	(a0)
	bne.s	.dir_ok
	tst.w	2(a0)
	bne.s	.dir_ok
	move.w	#64,(a0)
.dir_ok:

	; Always spawn from center
	move.l	#160<<16,4(a0)		; x = center
	move.l	#128<<16,8(a0)		; y = center

	bsr	random
	andi.w	#3,d0
	addq.w	#1,d0
	move.w	d0,12(a0)		; speed

	move.w	d0,d1
	addq.w	#2,d1
	cmpi.w	#7,d1
	ble.s	.col_ok2
	move.w	#7,d1
.col_ok2:
	move.w	d1,14(a0)		; color

	lea	STAR_STRUCT_SIZE(a0),a0
	dbf	d7,.upd_loop
	rts

;*********************************************************
;*  plot_stars - Plot each star as a pixel                *
;*********************************************************
plot_stars:
	lea	star_data,a0
	move.l	bpl0_ptr,a1
	move.l	bpl1_ptr,a2
	move.l	bpl2_ptr,a3
	move.w	#MAX_STARS-1,d7

.plot_loop:
	move.l	4(a0),d0		; x fixed
	swap	d0
	move.l	8(a0),d1		; y fixed
	swap	d1

	tst.w	d0
	blt.s	.skip
	cmpi.w	#SCR_W-1,d0
	bgt.s	.skip
	tst.w	d1
	blt.s	.skip
	cmpi.w	#SCR_H-1,d1
	bgt.s	.skip

	; Byte offset: y * 40 + x/8
	move.w	d1,d2
	mulu.w	#BPLROW,d2
	move.w	d0,d3
	lsr.w	#3,d3
	add.w	d3,d2

	; Bit position: 7 - (x & 7)
	move.w	d0,d3
	not.w	d3
	andi.w	#7,d3

	move.w	14(a0),d4		; color 1-7

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

	subq.l	#1,ent_timer
	bgt.s	.ent_done

	; Activate
	move.l	#1,ent_active
	bsr	random
	andi.w	#$7F,d0
	addi.w	#40,d0
	move.w	d0,ent_y
	move.w	#SCR_W+ENT_W,ent_x
	move.w	#-ENT_SPEED,ent_dx
	rts

.ent_moving:
	move.w	ent_dx,d0
	add.w	d0,ent_x
	move.w	ent_x,d0
	cmpi.w	#-ENT_W-16,d0
	bgt.s	.ent_done
	clr.l	ent_active
	move.l	#ENT_INTERVAL,ent_timer
.ent_done:
	rts

;*********************************************************
;*  draw_enterprise - USS Enterprise NCC-1701            *
;*  96 pixels wide x 48 pixels tall, 6 words per row    *
;*  Uses a per-word loop for flexibility                 *
;*********************************************************
draw_enterprise:
	move.w	ent_x,d5
	move.w	ent_y,d6
	lea	enterprise_gfx,a0
	move.l	bpl0_ptr,a1
	move.l	bpl1_ptr,a2
	move.l	bpl2_ptr,a3
	moveq	#ENT_H-1,d7

.ent_row:
	; Calculate screen Y for this row
	move.w	d6,d4
	move.w	#ENT_H-1,d3
	sub.w	d7,d3
	add.w	d3,d4			; d4 = ent_y + row offset

	tst.w	d4
	blt.s	.ent_skip_row
	cmpi.w	#SCR_H-1,d4
	bgt.s	.ent_skip_row

	; Draw 6 words (96 pixels) at x offsets 0,16,32,48,64,80
	move.w	d5,-(sp)		; save x base
	moveq	#ENT_WORDS-1,d3	; 6 words

.ent_word_loop:
	move.w	(a0)+,d0		; load word data
	tst.w	d0
	beq.s	.ent_word_skip		; skip empty words
	bsr	.draw_word
.ent_word_skip:
	addi.w	#16,d5			; advance x by 16 pixels
	dbf	d3,.ent_word_loop

	move.w	(sp)+,d5		; restore x base
	dbf	d7,.ent_row
	rts

.ent_skip_row:
	lea	ENT_WORDS*2(a0),a0	; skip 6 words (12 bytes)
	dbf	d7,.ent_row
	rts

; Draw one 16-pixel word. d0.w=pixels, d5.w=screen x base, d4.w=screen y
; Sets color 7 (white) in all 3 bitplanes. Preserves d3,d4,d5.
.draw_word:
	movem.l	d1-d3/d5,-(sp)
	moveq	#15,d1
.dw_bit:
	btst	d1,d0
	beq.s	.dw_next
	move.w	#15,d2
	sub.w	d1,d2
	add.w	d5,d2			; d2 = screen x
	tst.w	d2
	blt.s	.dw_next
	cmpi.w	#SCR_W-1,d2
	bgt.s	.dw_next

	; Byte offset
	move.w	d4,d3
	mulu.w	#BPLROW,d3
	move.w	d2,-(sp)
	lsr.w	#3,d2
	add.w	d2,d3
	move.w	(sp)+,d2
	not.w	d2
	andi.w	#7,d2

	bset	d2,0(a1,d3.w)
	bset	d2,0(a2,d3.w)
	bset	d2,0(a3,d3.w)
.dw_next:
	dbf	d1,.dw_bit
	movem.l	(sp)+,d1-d3/d5
	rts

;*********************************************************
;*  init_copper - Build copper list in chip memory        *
;*********************************************************
init_copper:
	move.l	chipmem_base,d0
	addi.l	#SCR_BPL_SIZE*3,d0
	move.l	d0,copper_list_ptr
	move.l	d0,a0

	; --- Set palette ---
	move.w	#COLOR00,(a0)+
	move.w	#$0002,(a0)+		; color 0: deep space blue

	move.w	#COLOR00+2,(a0)+
	move.w	#$0333,(a0)+		; color 1: very dim

	move.w	#COLOR00+4,(a0)+
	move.w	#$0666,(a0)+		; color 2: dim

	move.w	#COLOR00+6,(a0)+
	move.w	#$0999,(a0)+		; color 3: medium

	move.w	#COLOR00+8,(a0)+
	move.w	#$0BBB,(a0)+		; color 4: bright

	move.w	#COLOR00+10,(a0)+
	move.w	#$0ADF,(a0)+		; color 5: blue-white

	move.w	#COLOR00+12,(a0)+
	move.w	#$0DEF,(a0)+		; color 6: very bright

	move.w	#COLOR00+14,(a0)+
	move.w	#$0FFF,(a0)+		; color 7: pure white

	; --- Bitplane pointers (updated each frame) ---
	move.w	#BPL1PTH,(a0)+
	move.w	#0,(a0)+
	move.w	#BPL1PTL,(a0)+
	move.w	#0,(a0)+
	move.w	#BPL2PTH,(a0)+
	move.w	#0,(a0)+
	move.w	#BPL2PTL,(a0)+
	move.w	#0,(a0)+
	move.w	#BPL3PTH,(a0)+
	move.w	#0,(a0)+
	move.w	#BPL3PTL,(a0)+
	move.w	#0,(a0)+

	; Save pointer to BPL entries
	; 8 palette entries * 4 bytes each = 32 bytes before BPL ptrs
	move.l	copper_list_ptr,d0
	addi.l	#8*4,d0
	move.l	d0,copper_bpl_ptrs

	; --- Background gradient ---
	move.w	#$2C,d1			; start line
	move.w	#$0003,d2		; starting color

	moveq	#15,d3
.grad_loop:
	move.w	d1,d0
	lsl.w	#8,d0
	ori.w	#$01,d0
	move.w	d0,(a0)+
	move.w	#$FFFE,(a0)+

	move.w	#COLOR00,(a0)+
	move.w	d2,(a0)+

	cmpi.w	#7,d3
	bgt.s	.no_dark
	subq.w	#1,d2
	bpl.s	.no_dark
	clr.w	d2
.no_dark:
	addi.w	#16,d1
	cmpi.w	#$100,d1
	bge.s	.grad_done
	dbf	d3,.grad_loop
.grad_done:

	; End copper list
	move.l	#$FFFFFFFE,(a0)+
	rts

;*********************************************************
;*  update_copper - Update BPL pointers in copper list   *
;*********************************************************
update_copper:
	move.l	copper_bpl_ptrs,a0

	move.l	bpl0_ptr,d0
	swap	d0
	move.w	d0,2(a0)
	swap	d0
	move.w	d0,6(a0)

	move.l	bpl1_ptr,d0
	swap	d0
	move.w	d0,10(a0)
	swap	d0
	move.w	d0,14(a0)

	move.l	bpl2_ptr,d0
	swap	d0
	move.w	d0,18(a0)
	swap	d0
	move.w	d0,22(a0)
	rts

;*********************************************************
;*  random - xorshift32 PRNG                             *
;*  Returns: d0 = random value                           *
;*********************************************************
random:
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
;*  Enterprise NCC-1701 bitmap (converted from PNG)      *
;*  96 pixels wide x 48 pixels tall                      *
;*  Each row = 6 words (96 bits)                         *
;*  Front/bottom view of Constitution-class starship     *
;*********************************************************

	section	data,data

enterprise_gfx:
	; Row 0: saucer dome top
	dc.w	$0000,$0780,$0000,$0000,$0000,$0000
	; Row 1: saucer dome
	dc.w	$0000,$1FE0,$0000,$0000,$0000,$0000
	; Row 2: saucer upper
	dc.w	$0000,$3FFF,$0000,$0000,$0000,$0000
	; Row 3: saucer widening + nacelle hints
	dc.w	$0000,$3FFF,$0000,$E000,$0030,$0000
	; Row 4: saucer + nacelle emergence
	dc.w	$0000,$7FFF,$0001,$F000,$00FC,$0000
	; Row 5: saucer + nacelles growing
	dc.w	$0000,$7FFF,$0007,$F800,$07FE,$0000
	; Row 6: saucer + nacelles
	dc.w	$0000,$7FFF,$003F,$FC00,$07FF,$0000
	; Row 7: saucer lower + nacelles
	dc.w	$0000,$3FFF,$007F,$FF00,$07FF,$0000
	; Row 8: saucer base
	dc.w	$0000,$3FFC,$00FF,$FF80,$0FFF,$0000
	; Row 9: saucer taper + nacelle spread
	dc.w	$0000,$1FF0,$3FFF,$FFC0,$0FFF,$0000
	; Row 10: hull connection
	dc.w	$0200,$07FF,$FFFF,$FFE0,$07FF,$0000
	; Row 11: full span
	dc.w	$3FFF,$FFFF,$FFFF,$FFFF,$03FE,$0000
	; Row 12: maximum width
	dc.w	$1FFF,$FFFF,$FFFF,$FFFF,$E1FE,$0000
	; Row 13: nacelle span
	dc.w	$1FFF,$FFFF,$FFFF,$FFFF,$FFF8,$0000
	; Row 14: nacelle + hull
	dc.w	$0FFF,$FFFF,$FFFF,$FFFF,$FF80,$0000
	; Row 15: widest section
	dc.w	$07FF,$FFFF,$FFFF,$FFFF,$FFFE,$0000
	; Row 16: nacelle tips visible
	dc.w	$01FF,$FFFF,$FFFF,$FFFF,$FFFF,$FE00
	; Row 17: full nacelle extension
	dc.w	$0007,$FFFF,$FFFF,$FFFF,$FFFF,$FFFC
	; Row 18: hull narrowing
	dc.w	$0000,$07FF,$FFFF,$FFFF,$FFFF,$FFFE
	; Row 19: secondary hull
	dc.w	$0000,$0003,$FFFF,$FFFF,$FFFF,$FFFE
	; Row 20: hull taper
	dc.w	$0000,$0000,$7FFF,$FFFF,$FFFF,$FFFC
	; Row 21: lower hull
	dc.w	$0000,$0000,$3FFF,$FFFF,$FFFF,$FFF8
	; Row 22: neck region
	dc.w	$0000,$0000,$1FFF,$FFFE,$0007,$FF80
	; Row 23: neck
	dc.w	$0000,$0000,$1EFF,$FFFC,$0000,$0000
	; Row 24: pylons
	dc.w	$0000,$0000,$0E3F,$FFB8,$0000,$0000
	; Row 25: pylon junction
	dc.w	$0000,$0000,$071F,$FC70,$0000,$0000
	; Row 26: engineering section top
	dc.w	$0000,$0000,$0387,$E0E0,$0000,$0000
	; Row 27: engineering hull
	dc.w	$0000,$0000,$01C7,$C1C0,$0000,$0000
	; Row 28: engineering hull
	dc.w	$0000,$0000,$01E7,$C780,$0000,$0000
	; Row 29: deflector area
	dc.w	$0000,$0000,$00FF,$EF00,$0000,$0000
	; Row 30: deflector dish
	dc.w	$0000,$0000,$007F,$FE00,$0000,$0000
	; Row 31: engineering hull
	dc.w	$0000,$0000,$00FF,$FC00,$0000,$0000
	; Row 32: hull widening
	dc.w	$0000,$0000,$01FF,$FE00,$0000,$0000
	; Row 33: hull
	dc.w	$0000,$0000,$01FE,$FE00,$0000,$0000
	; Row 34: hull
	dc.w	$0000,$0000,$03FF,$DF00,$0000,$0000
	; Row 35: hull
	dc.w	$0000,$0000,$03FF,$FF00,$0000,$0000
	; Row 36: hull widest
	dc.w	$0000,$0000,$03FF,$FF00,$0000,$0000
	; Row 37: hull lower
	dc.w	$0000,$0000,$03FF,$FF80,$0000,$0000
	; Row 38: hull
	dc.w	$0000,$0000,$07FF,$FF80,$0000,$0000
	; Row 39: hull
	dc.w	$0000,$0000,$07FF,$FF80,$0000,$0000
	; Row 40: hull taper
	dc.w	$0000,$0000,$03FF,$FF00,$0000,$0000
	; Row 41: hull taper
	dc.w	$0000,$0000,$03FF,$FF00,$0000,$0000
	; Row 42: hull narrowing
	dc.w	$0000,$0000,$03FF,$FF00,$0000,$0000
	; Row 43: hull narrowing
	dc.w	$0000,$0000,$01FF,$FE00,$0000,$0000
	; Row 44: hull base
	dc.w	$0000,$0000,$00FF,$FC00,$0000,$0000
	; Row 45: hull base
	dc.w	$0000,$0000,$007F,$F800,$0000,$0000
	; Row 46: hull tip
	dc.w	$0000,$0000,$003F,$F000,$0000,$0000
	; Row 47: hull tip
	dc.w	$0000,$0000,$000F,$8000,$0000,$0000

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

; Steering velocity (16.16 fixed point, applied to all stars)
steer_vx:		ds.l	1
steer_vy:		ds.l	1

; Enterprise state
ent_active:		ds.l	1
ent_timer:		ds.l	1
ent_x:			ds.w	1
ent_y:			ds.w	1
ent_dx:			ds.w	1

; Keyboard state (written by C, read by asm)
_sf_key_up:		ds.b	1
_sf_key_down:		ds.b	1
_sf_key_left:		ds.b	1
_sf_key_right:		ds.b	1
_sf_key_esc:		ds.b	1

	cnop	0,4

; Star array
star_data:		ds.b	MAX_STARS*STAR_STRUCT_SIZE
