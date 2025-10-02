; PBI Device Driver ROM (MPD) skeleton
; based on Earl Rice ANTIC Magazine
;
; (C) 2025 RetroBit Lab
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

; For now we are using a zero page variable.
.define VERAINDEXREG    $fa ; zero page index register

; Device VERA REGISTERS are mapped $D100-$D11F
.define PBI_ADDR        $D100
.define VERA_REG_ADDR   PBI_ADDR + 0
.define VERA_CTRL_REG   PBI_ADDR + $05
.define VERA_LAST_REG   PBI_ADDR + $1F

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
    
    ; TODO: something useful with result codes
   
    ; TODO: device-specifi init
    ; Like initialize the 320x240 256 colors mode
    ; and write something on the screen
    
    lda #0            ; initialize the register index
    sta VERAINDEXREG  ; 

    rts

; GET BYTE ROUTINE
GETBYT:
    lda #0
    sta CRITIC                ; Enable deferred vertical blank

    ldx VERAINDEXREG          ; reading index
    lda VERA_REG_ADDR,x       ; Accessing to the needed register
    
    ldy #1
    sec                       ; Indicate we handled it
                              ; Register 'A' holds the value to be read
    rts

; PUT BYTE ROUTINE
PUTBYT:
    ldx #0
    stx CRITIC                ; Enable deferred vertical blank
    
    ldx VERAINDEXREG          ; reading index 
    sta VERA_REG_ADDR,x       ; Regsiter 'A' holds the value to be written

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
