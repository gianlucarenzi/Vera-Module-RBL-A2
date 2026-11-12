; ============================================================================
; slinky_rom.s -- $Cn00-$CnFF slot ROM of the VERA Module RBL-A2
;
; The card's expanded memory follows the Apple II Memory Expansion Card
; ("Slinky") interface, served by the ESP32 (firmware/MCU/include/a2_slinky.h):
;
;   $C0n0  address bits 0-7    (read/write)
;   $C0n1  address bits 8-15   (read/write)
;   $C0n2  address bits 16-23  (read/write; reads OR $F0, as the Apple card)
;   $C0n3  data: each read or write moves the address forward by one
;   $C0n4-$C0nE  plain registers
;   $C0nF  bit 0 = 1: VERA registers at $Cn00-$Cn1F instead of this ROM
;          (cleared by an Apple reset, like every register)
;
; This ROM makes the memory a ProDOS block device ("out of the box"):
;   $Cn01/$Cn03/$Cn05/$Cn07 = $20/$00/$03/$3C  block device, not SmartPort
;   $CnFC-$CnFD  block count, $CnFE status byte, $CnFF driver entry (low byte)
; ProDOS reads the signature only at boot; afterwards it calls the driver at
; $Cn00 + ($CnFF), which lies above $Cn1F, so the driver keeps working when a
; program maps the VERA over $Cn00-$Cn1F.
;
; The code is position independent (only relative branches inside the ROM):
; the same image answers in any slot.  The ESP32 formats the memory as an
; empty ProDOS volume (/SLINKY) at power-on; PR#n boots it once it holds a
; ProDOS boot block, otherwise the autostart scan goes on with slot n-1.
;
; Never touch $C0n3 with an indexed store (STA abs,X / abs,Y / (zp),Y): the
; NMOS 6502 makes a dummy read of the target first, which would move the
; address forward.  Writes use STA (zp,X), which has no dummy access.
;
; Build: ca65 + ld65 (see Makefile).  Style: 6502 assembler, English.
; ============================================================================

        .setcpu "6502"

NBLOCKS = 512                   ; 256 KB / 512 B

; ProDOS block driver parameters
CMD     = $42                   ; 0 status, 1 read, 2 write, 3 format
UNIT    = $43                   ; DSSS0000
BUFLO   = $44
BUFHI   = $45
BLKLO   = $46
BLKHI   = $47

ZPTR    = $3A                   ; scratch pointer, saved and restored

; Slinky registers, indexed by X = slot * 16
ADDR0   = $C080
ADDR1   = $C081
ADDR2   = $C082
DATA    = $C083

; Apple II monitor / autostart ROM
KNOWNRTS = $FF58
SLOOP    = $FABA                ; autostart slot scan: next lower slot
MSLOT    = $07F8
SETKBD   = $FE89                ; KSW = keyboard
SETVID   = $FE93                ; CSW = screen

ERR_BADCMD = $01
ERR_IO     = $27

        .segment "CODE"

; ---------------------------------------------------------------------------
; $Cn00: signature and boot entry (PR#n, autostart).  $Cn00-$Cn1F are
; visible only while the VERA is not mapped, which is the case after reset.
; ---------------------------------------------------------------------------
SIG:    ldx     #$20            ; $Cn01 = $20
        ldy     #$00            ; $Cn03 = $00
        ldx     #$03            ; $Cn05 = $03
        stx     $3C             ; $Cn07 = $3C (not $00: not a SmartPort)

        jsr     KNOWNRTS        ; the ROM is on at boot: find our page
        tsx
        lda     $0100,x         ; $Cn
        sta     MSLOT
        sta     $01             ; $00/$01 = $Cn00 for the autostart scan
        asl     a
        asl     a
        asl     a
        asl     a
        sta     UNIT            ; slot n, drive 1
        ldy     #$00
        sty     $00
        sty     BUFLO
        sty     BLKLO
        sty     BLKHI
        iny
        sty     CMD             ; READ block 0 ...
        lda     #$08
        sta     BUFHI           ; ... to $0800
        lda     $01             ; return to BOOT2 through RTS
        pha
        lda     #<(BOOT2 - 1)
        pha
        bne     DRIVER          ; always

; ---------------------------------------------------------------------------
; ProDOS block driver.  In: $42-$47.  Out: C = 0, A = 0 on success;
; C = 1, A = error code otherwise.  STATUS returns the block count in X/Y.
; ---------------------------------------------------------------------------
DRIVER: lda     UNIT
        and     #$70
        tax                     ; X = slot * 16
        lda     CMD
        beq     STATUS
        cmp     #$03
        beq     OK              ; FORMAT: nothing to do on RAM
        bcs     BADCMD
        lda     BLKHI           ; block < NBLOCKS ?
        cmp     #>NBLOCKS
        bcc     INRANGE
        bne     IOERR
        lda     BLKLO
        cmp     #<NBLOCKS
        bcs     IOERR
INRANGE:
        lda     #$00            ; card address = block * 512
        sta     ADDR0,x         ; (the dummy read of $C0n0-2 is harmless)
        lda     BLKLO
        asl     a
        sta     ADDR1,x
        lda     BLKHI
        rol     a
        sta     ADDR2,x
        ldy     #$00
        lda     CMD
        lsr     a
        bcc     WRITE           ; 2 = write, 1 = read

READ1:  lda     DATA,x
        sta     (BUFLO),y
        iny
        bne     READ1
        inc     BUFHI
READ2:  lda     DATA,x
        sta     (BUFLO),y
        iny
        bne     READ2
        dec     BUFHI
OK:     lda     #$00
        clc
        rts

WRITE:  lda     ZPTR
        pha
        lda     ZPTR+1
        pha
        txa
        ora     #<DATA          ; ZPTR = $C0n3
        sta     ZPTR
        lda     #>DATA
        sta     ZPTR+1
        ldx     #$00
WRITE1: lda     (BUFLO),y
        sta     (ZPTR,x)
        iny
        bne     WRITE1
        inc     BUFHI
WRITE2: lda     (BUFLO),y
        sta     (ZPTR,x)
        iny
        bne     WRITE2
        dec     BUFHI
        pla
        sta     ZPTR+1
        pla
        sta     ZPTR
        clc
        bcc     OK              ; always (no JMP: position independent)

STATUS: ldx     #<NBLOCKS
        ldy     #>NBLOCKS
        lda     #$00
        clc
        rts

BADCMD: lda     #ERR_BADCMD
        sec
        rts

IOERR:  lda     #ERR_IO
        sec
        rts

; ---------------------------------------------------------------------------
; Boot, after block 0 has been read to $0800.
; ---------------------------------------------------------------------------
BOOT2:  bcs     NOBOOT
        lda     $0800           ; ProDOS boot block: $01, code at $0801
        cmp     #$01
        bne     NOBOOT
        ldx     UNIT            ; X = slot * 16, as the boot code expects
        jmp     $0801
NOBOOT: jsr     SETVID          ; PR#n from BASIC enters here through the
        jsr     SETKBD          ; output hook: unhook, or every character
        jmp     SLOOP           ; would boot again.  $00/$01 = $Cn00: go on
                                ; with slot n-1, as the autostart scan

; ---------------------------------------------------------------------------
; Identification, read by the VERA detection code (always visible):
;   $CnF0  "VERA" (high bit clear), $CnF4 card type: 'S' = Slinky + VERA,
;   $CnF5  interface version.
; ---------------------------------------------------------------------------
        .res    $F0 - (* - SIG), $FF
ID:     .byte   "VERAS", $01
        .res    $FC - (* - SIG), $FF
        .word   NBLOCKS         ; $CnFC-$CnFD total blocks
        .byte   $0F             ; $CnFE: 1 volume, format, write, read, status
        .byte   <DRIVER         ; $CnFF: driver entry

        .assert DRIVER - SIG >= $20, error, "driver must lie above $Cn1F (VERA window)"
        .assert BOOT2 - SIG >= $20, error, "BOOT2 must lie above $Cn1F (VERA window)"
        .assert * - SIG = $100, error, "slot ROM must be 256 bytes"
