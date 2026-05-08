; PBI Device Driver ROM (MPD) skeleton
; based on Earl Rice ANTIC Magazine
;
; (C) 2025-26 RetroBit Lab
; written by Gianluca Renzi

; Generic OS Parallel Device handler vectors
.define NEWDEV $E486
.define GENDEV $E48F

; Parallel Device Mask
.define PDVMSK $0247

; Activated PBI device
.define PNDEVREQ $248

; Parallel Interrupt Mask
.define PDIMSK $0249

; Generic OS Parallel Device Vector
.define GPDVV $E48F

; Device Handler table
.define HATABS $031A

; Critical code section flag
.define CRITIC $42

; Device Name [V]era Module
.define DEVNAM 'V'

; OS Equates
.define DDEVIC     $0300
.define DUNIT      $0301
.define DCOMND     $0302
.define DSTATS     $0303
.define DBUFLO     $0304
.define DBUFHI     $0305
.define DTIMLO     $0306
.define DUNUSE     $0307
.define DBYTLO     $0308
.define DBYTHI     $0309
.define DAUX1      $030A
.define DAUX2      $030B

.define IOCB       $0340    ;128-byte I/O control blocks area
.define ICHID      $0340    ;1-byte handler ID ($FF = free)
.define ICDNO      $0341    ;1-byte device number
.define ICCOM      $0342    ;1-byte command code
.define ICSTA      $0343    ;1-byte status of last action
.define ICBAL      $0344    ;1-byte low buffer address
.define ICBAH      $0345    ;1-byte high buffer address
.define ICPTL      $0346    ;1-byte low PUT-BYTE routine address-1
.define ICPTH      $0347    ;1-byte high PUT-BYTE routine address-1
.define ICBLL      $0348    ;1-byte low buffer length
.define ICBLH      $0349    ;1-byte high buffer length
.define ICAX1      $034A    ;1-byte first auxiliary information
.define ICAX2      $034B    ;1-byte second auxiliary information
.define ICSPR      $034C    ;4-byte work area

; The best approach should be using CIO/IOCB routines.
; Having setting the index offset somewhere in the IOCB block so the
; GET/PUT/STATUS vectors will retreive them from the call mechanism.

; Device VERA REGISTERS are mapped $D100-$D11F
.define PBI_ADDR            $D100
.define VERA_REG_ADDR       PBI_ADDR + $00
.define VERA_ADDR_L         VERA_REG_ADDR + $00
.define VERA_ADDR_M         VERA_REG_ADDR + $01
.define VERA_ADDR_H         VERA_REG_ADDR + $02
.define VERA_DATA0          VERA_REG_ADDR + $03
.define VERA_CTRL_REG       VERA_REG_ADDR + $05
.define VERA_IEN            VERA_REG_ADDR + $06
.define VERA_ISR            VERA_REG_ADDR + $07
.define VERA_DC_VIDEO       VERA_REG_ADDR + $09
.define VERA_DC_HSCALE      VERA_REG_ADDR + $0A
.define VERA_DC_VSCALE      VERA_REG_ADDR + $0B
.define VERA_DC_BORDER      VERA_REG_ADDR + $0C
.define VERA_DC_HSTART      VERA_REG_ADDR + $09
.define VERA_DC_HSTOP       VERA_REG_ADDR + $0A
.define VERA_DC_VSTART      VERA_REG_ADDR + $0B
.define VERA_DC_VSTOP       VERA_REG_ADDR + $0C
.define VERA_L1_CONFIG      VERA_REG_ADDR + $14
.define VERA_L1_MAPBASE     VERA_REG_ADDR + $15
.define VERA_L1_TILEBASE    VERA_REG_ADDR + $16
.define VERA_L1_HSCROLL_L   VERA_REG_ADDR + $17
.define VERA_L1_HSCROLL_H   VERA_REG_ADDR + $18
.define VERA_L1_VSCROLL_L   VERA_REG_ADDR + $19
.define VERA_L1_VSCROLL_H   VERA_REG_ADDR + $1A
.define VERA_LAST_REG       VERA_REG_ADDR + $1F

; cx16.inc-compatible VERA constants adapted to the PBI window
.define VERA_INC1           $10
.define VERA_DCSEL1         $02
.define VERA_VIDEO_VGA      $01
.define VERA_ENABLE_LAYER1  $20
.define VERA_MAP_128x64     $60
.define VERA_TILE_HEIGHT16  $02

.define TEXT_COLOR          $61    ; white on blue
.define SCREEN_ADDR         $01B000
.define CHARSET_ADDR        $01F000
.define SCREEN_MAPBASE      $D8
.define SCREEN_TILEBASE     $FA
.define SCREEN_COLS         80
.define SCREEN_ROWS         25
.define SCREEN_ROW_STRIDE   160

.define BANNER1_ADDR        $01B5F4
.define BANNER2_ADDR        $01B72C
.define BANNER3_ADDR        $01B8CA

.macro PRINT_LINE addr, label
    .local CopyChar, Done
    lda #<(addr)
    sta VERA_ADDR_L
    lda #>(addr)
    sta VERA_ADDR_M
    lda #(VERA_INC1 | ^(addr))
    sta VERA_ADDR_H
    ldx #0
CopyChar:
    lda label,x
    beq Done
    sta VERA_DATA0
    lda #TEXT_COLOR
    sta VERA_DATA0
    inx
    bne CopyChar
Done:
.endmacro

; OS ROM (MPD) $D800 Vector Table

.code
.org $D800 ; OS ROM Vector Table

    .word 0     ; ROM checksum LSB/MSB (optional)
    .byte 0     ; ROM Revision number (optional)

    ; PBI Mandatory ID Number
    .byte $80

    .byte 0     ; Optional Name or Type

    ; LOW LEVEL I/O Vector
    jmp IOVECTOR

    ; INTERRUPT VECTOR
    jmp IRQVECTOR

    ; Mandatory ID Number
    .byte $91

    ; Device Name
    .byte DEVNAM

    ; OPEN VECTOR
    .word NONEED-1

    ; CLOSE VECTOR
    .word NONEED-1

    ; GET BYTE VECTOR
    .word GETBYT-1

    ; PUT BYTE VECTOR
    .word PUTBYT-1

    ; GET STATUS VECTOR
    .word GETSTA-1

    ; SPECIAL VECTOR
    .word NONEED-1

    ; INIT VECTOR @ PowerUp or RESET
    jmp INIT

    .byte 0     ; NOT USED

; Code starts here

; Not implementing now
IOVECTOR:
    clc
    rts

; no IRQ handling yet
IRQVECTOR:
    rts

; Initialize device and device handler
INIT:
    lda PDVMSK   ; Get Enabled devices PBI flags
    ora PNDEVREQ ; OR in the current device request bit
    sta PDVMSK   ; store the device back

; Put device name in Handler Table HATABS Earl Rice (ANTIC JAN-APR 1985)
;    ldx #0
;SEARCH:
;    lda HATABS,X    ; Get a byte from table
;    beq FNDIT       ; 0? Then we found space
;    inx
;    inx
;    inx
;    cpx #36         ; Length of HATABS
;    bcc SEARCH      ; Still looking
;    rts             ; No room in HATABS device not initialized
;
; We found a spot
;FNDIT:
;    lda #DEVNAM     ; Get Device Name
;    sta HATABS,X    ; Put it in blank spot
;    inx
;    lda #<GPDVV     ; Get LOW BYTE of a vector GPDVV
;    sta HATABS+1,X
;    lda #>GPDVV     ; Get HIGH BYTE of a vector GPDVV
;    sta HATABS+2,X
;    rts

    ; roland scholz/fjc method using the NEWDEV routine in the OS
    ldx #DEVNAM
    lda #>GENDEV
    ldy #<GENDEV
    jsr NEWDEV        ; returns: N = 1 - failed, C = 0 - success, C = 1 - entry already exists

    jsr INIT_VERA_SCREEN
    rts

INIT_VERA_SCREEN:
    jsr WAIT_VERA

    lda #0
    sta VERA_CTRL_REG
    sta VERA_IEN
    sta VERA_ISR

    jsr LOAD_BANNER_FONT

    lda #VERA_MAP_128x64
    sta VERA_L1_CONFIG
    lda #SCREEN_MAPBASE
    sta VERA_L1_MAPBASE
    lda #SCREEN_TILEBASE
    sta VERA_L1_TILEBASE

    lda #0
    sta VERA_L1_HSCROLL_L
    sta VERA_L1_HSCROLL_H
    sta VERA_L1_VSCROLL_L
    sta VERA_L1_VSCROLL_H

    lda #VERA_DCSEL1
    sta VERA_CTRL_REG
    lda #$00
    sta VERA_DC_HSTART
    lda #$A0
    sta VERA_DC_HSTOP
    lda #$14
    sta VERA_DC_VSTART
    lda #$DC
    sta VERA_DC_VSTOP

    lda #0
    sta VERA_CTRL_REG
    lda #(VERA_VIDEO_VGA | VERA_ENABLE_LAYER1)
    sta VERA_DC_VIDEO
    lda #$80
    sta VERA_DC_HSCALE
    sta VERA_DC_VSCALE
    lda #0
    sta VERA_DC_BORDER

    jsr CLEAR_SCREEN

    PRINT_LINE BANNER1_ADDR, BannerLine1
    PRINT_LINE BANNER2_ADDR, BannerLine2
    PRINT_LINE BANNER3_ADDR, BannerLine3
    rts

WAIT_VERA:
    ldx #$FF
WaitForVeraLoop:
    lda #$2A
    sta VERA_ADDR_L
    lda VERA_ADDR_L
    cmp #$2A
    beq WaitForVeraDone
    dex
    bne WaitForVeraLoop
WaitForVeraDone:
    rts

CLEAR_SCREEN:
    lda #<SCREEN_ADDR
    sta VERA_ADDR_L
    lda #>SCREEN_ADDR
    sta VERA_ADDR_M
    lda #(VERA_INC1 | ^SCREEN_ADDR)
    sta VERA_ADDR_H
    ldy #SCREEN_ROWS
ClearScreenRow:
    ldx #SCREEN_COLS
ClearScreenCol:
    lda #' '
    sta VERA_DATA0
    lda #TEXT_COLOR
    sta VERA_DATA0
    dex
    bne ClearScreenCol
    dey
    bne ClearScreenRow
    rts

LOAD_BANNER_FONT:
    ldx #0
NextGlyph:
    lda GlyphTable,x
    beq LoadFontDone
    jsr SET_GLYPH_ADDR

    lda GlyphTable+1,x
    sta VERA_DATA0
    sta VERA_DATA0
    lda GlyphTable+2,x
    sta VERA_DATA0
    sta VERA_DATA0
    lda GlyphTable+3,x
    sta VERA_DATA0
    sta VERA_DATA0
    lda GlyphTable+4,x
    sta VERA_DATA0
    sta VERA_DATA0
    lda GlyphTable+5,x
    sta VERA_DATA0
    sta VERA_DATA0
    lda GlyphTable+6,x
    sta VERA_DATA0
    sta VERA_DATA0
    lda GlyphTable+7,x
    sta VERA_DATA0
    sta VERA_DATA0
    lda GlyphTable+8,x
    sta VERA_DATA0
    sta VERA_DATA0

    txa
    clc
    adc #9
    tax
    bne NextGlyph
LoadFontDone:
    rts

SET_GLYPH_ADDR:
    pha
    and #$0F
    asl
    asl
    asl
    asl
    sta VERA_ADDR_L
    pla
    lsr
    lsr
    lsr
    lsr
    clc
    adc #>CHARSET_ADDR
    sta VERA_ADDR_M
    lda #(VERA_INC1 | ^CHARSET_ADDR)
    sta VERA_ADDR_H
    rts

; GET BYTE ROUTINE
GETBYT:
    lda #0
    sta CRITIC                ; Enable deferred vertical blank
    ldy ICAX1,x
    lda VERA_REG_ADDR,y

    ldy #1
    sec                       ; Indicate we handled it
                              ; Register 'A' holds the value to be read
    rts

; PUT BYTE ROUTINE
PUTBYT:
    pha
    lda #0
    sta CRITIC                ; Enable deferred vertical blank
    ldy ICAX1,x
    pla
    sta VERA_REG_ADDR,y

    ldy #1
    sec                       ; Indicate we handled it
    rts

; GET STATUS ROUTINE
GETSTA:
    lda #0
    sta CRITIC              ; Enable deferred vertical blank
    lda VERA_CTRL_REG       ; Accessing to the CTRL register

    ldy #1
    sec                     ; Indicate we handled it
                            ; Register 'A' holds the value to be read
    rts

; DO NOTHING ROUTINE
NONEED:
    ldy #1
    sec                     ; Indicate we handled it
    rts

BannerLine1:
    .asciiz "**** COMMANDER X16 VERA ****"
BannerLine2:
    .asciiz "PBI VIDEO INTERFACE"
BannerLine3:
    .asciiz "READY."

GlyphTable:
    .byte ' ', $00, $00, $00, $00, $00, $00, $00, $00
    .byte '*', $00, $66, $3C, $FF, $3C, $66, $00, $00
    .byte '.', $00, $00, $00, $00, $00, $18, $18, $00
    .byte '1', $18, $38, $18, $18, $18, $18, $7E, $00
    .byte '6', $3C, $60, $60, $7C, $66, $66, $3C, $00
    .byte 'A', $18, $3C, $66, $66, $7E, $66, $66, $00
    .byte 'B', $7C, $66, $66, $7C, $66, $66, $7C, $00
    .byte 'C', $3C, $66, $60, $60, $60, $66, $3C, $00
    .byte 'D', $78, $6C, $66, $66, $66, $6C, $78, $00
    .byte 'E', $7E, $60, $60, $7C, $60, $60, $7E, $00
    .byte 'F', $7E, $60, $60, $7C, $60, $60, $60, $00
    .byte 'I', $3C, $18, $18, $18, $18, $18, $3C, $00
    .byte 'M', $63, $77, $7F, $6B, $63, $63, $63, $00
    .byte 'N', $66, $76, $7E, $7E, $6E, $66, $66, $00
    .byte 'O', $3C, $66, $66, $66, $66, $66, $3C, $00
    .byte 'P', $7C, $66, $66, $7C, $60, $60, $60, $00
    .byte 'R', $7C, $66, $66, $7C, $6C, $66, $66, $00
    .byte 'T', $7E, $18, $18, $18, $18, $18, $18, $00
    .byte 'V', $66, $66, $66, $66, $66, $3C, $18, $00
    .byte 'X', $66, $66, $3C, $18, $3C, $66, $66, $00
    .byte 'Y', $66, $66, $66, $3C, $18, $18, $18, $00
    .byte 0
