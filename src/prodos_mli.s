;
; localprefix.s -- ProDOS ON_LINE ($C5) and SET_PREFIX ($C6) MLI wrappers
;
; ON_LINE output buffer entry (16 bytes each):
;   Byte 0  bit  7:    drive  (0=D1, 1=D2)
;   Byte 0  bits 6-4:  slot   (1-7)
;   Byte 0  bits 3-0:  name_len (0 = error / not online)
;   Bytes 1..name_len: volume name (not null-terminated)
; List is terminated by an entry whose byte 0 equals $00.
;

        .export         _get_online_volumes
        .export         _set_local_prefix
        .export         _get_local_prefix
        .export         _prodos_mkdir
        .export         _online_buf
        .export         _rdb_unit
        .export         _rdb_block
        .export         _prodos_read_block

PRODOS_MLI      = $BF00
MLI_ON_LINE     = $C5
MLI_SET_PREFIX  = $C6
MLI_GET_PREFIX  = $C7
MLI_CREATE      = $C0
MLI_READ_BLOCK  = $80

; ----------------------------------------------------------------
        .segment        "BSS"

_online_buf:    .res    256             ; 16-byte entries x 16 devices

; ----------------------------------------------------------------
        .segment        "DATA"

; ON_LINE parameter block (never modified at runtime)
online_params:
        .byte   2                       ; param_count
        .byte   0                       ; unit_num = 0 (all devices)
        .word   _online_buf             ; data_buffer (resolved at link time)

; SET_PREFIX parameter block (pathname pointer filled at runtime)
setpfx_params:
        .byte   1                       ; param_count
        .word   0                       ; pathname ptr (set by set_local_prefix)

; GET_PREFIX parameter block (buffer pointer filled at runtime)
getpfx_params:
        .byte   1                       ; param_count
        .word   0                       ; buffer ptr (set by get_local_prefix)

; READ_BLOCK parameters (unit and block filled at runtime by C code)
_rdb_unit:      .byte   0               ; exported for C: rdb_unit = unit_num
_rdb_block:     .word   0               ; exported for C: rdb_block = block number

rdb_params:
        .byte   3                       ; param_count
        .byte   0                       ; unit_num (filled at runtime)
        .word   0                       ; data_buffer (filled at runtime)
        .word   0                       ; block_num (filled at runtime)

; CREATE parameter block for directory (pathname pointer filled at runtime)
create_params:
        .byte   7                       ; param_count
        .word   0                       ; pathname ptr (set by prodos_mkdir)
        .byte   $C3                     ; access (full access)
        .byte   $0F                     ; file_type = DIR
        .word   $0000                   ; aux_type
        .byte   $0D                     ; storage_type = directory
        .word   $0000                   ; create_date
        .word   $0000                   ; create_time

; ----------------------------------------------------------------
        .segment        "CODE"

; ---------------------------------------------------------------
; uint8_t get_online_volumes(void)
;
; Calls ProDOS ON_LINE for all devices; fills online_buf.
; Returns count of entries where name_len > 0 (i.e. actually online).
; ---------------------------------------------------------------
.proc   _get_online_volumes

        jsr     PRODOS_MLI
        .byte   MLI_ON_LINE
        .word   online_params
        bcs     err

        ldy     #0                      ; byte index into online_buf
        ldx     #0                      ; count of online volumes
loop:
        lda     _online_buf,y           ; byte 0 of this entry
        beq     done                    ; $00 = terminator
        and     #$0F                    ; low nibble = name_len
        beq     skip                    ; 0 = device error, not online
        inx                             ; count this volume
skip:
        tya
        clc
        adc     #16                     ; advance to next 16-byte entry
        tay
        bne     loop                    ; Y wraps to 0 after all 16 entries
done:
        txa                             ; return count in A
        ldx     #0
        rts
err:
        lda     #0
        ldx     #0
        rts

.endproc


; ---------------------------------------------------------------
; void __fastcall__ set_local_prefix(const char *path)
;
; Sets the ProDOS system prefix via MLI SET_PREFIX.
; path: Pascal string — byte 0 is the length, bytes 1..n are
;       the path characters (e.g. length=9, "/VOLNAME/").
; On entry (cc65 fastcall): A = low byte, X = high byte of path.
; ---------------------------------------------------------------
.proc   _set_local_prefix

        sta     setpfx_params+1         ; low byte of pathname ptr
        stx     setpfx_params+2         ; high byte of pathname ptr
        jsr     PRODOS_MLI
        .byte   MLI_SET_PREFIX
        .word   setpfx_params
        rts                             ; carry and error code ignored

.endproc


; ---------------------------------------------------------------
; void __fastcall__ get_local_prefix(char *buf)
;
; Gets the ProDOS system prefix into buf as a Pascal string.
; buf must be at least 34 bytes: byte 0 = length, bytes 1..n = path.
; On entry (cc65 fastcall): A = low byte, X = high byte of buf.
; ---------------------------------------------------------------
.proc   _get_local_prefix

        sta     getpfx_params+1         ; low byte of buffer ptr
        stx     getpfx_params+2         ; high byte of buffer ptr
        jsr     PRODOS_MLI
        .byte   MLI_GET_PREFIX
        .word   getpfx_params
        rts                             ; error ignored

.endproc


; ---------------------------------------------------------------
; uint8_t __fastcall__ prodos_mkdir(const char *pascal_path)
;
; Creates a ProDOS directory via MLI CREATE with storage_type=$0D.
; pascal_path: Pascal string — byte 0 = length, bytes 1..n = path.
; Returns 0 on success, ProDOS error code on failure.
; On entry (cc65 fastcall): A = low byte, X = high byte of pascal_path.
; ---------------------------------------------------------------
.proc   _prodos_mkdir

        sta     create_params+1         ; low byte of pathname ptr
        stx     create_params+2         ; high byte of pathname ptr
        jsr     PRODOS_MLI
        .byte   MLI_CREATE
        .word   create_params
        bcc     ok
        ldx     #0                      ; A already has error code
        rts
ok:
        lda     #0
        ldx     #0
        rts

.endproc


; ---------------------------------------------------------------
; uint8_t __fastcall__ prodos_read_block(unsigned char *buf)
;
; Reads one 512-byte ProDOS block into buf.
; Caller must set rdb_unit and rdb_block before calling.
; Returns 0 on success, ProDOS error code on failure.
; On entry (cc65 fastcall): A = low byte, X = high byte of buf.
; ---------------------------------------------------------------
.proc   _prodos_read_block

        sta     rdb_params+2            ; data_buffer low byte
        stx     rdb_params+3            ; data_buffer high byte
        lda     _rdb_unit
        sta     rdb_params+1            ; unit_num
        lda     _rdb_block
        sta     rdb_params+4            ; block_num low
        lda     _rdb_block+1
        sta     rdb_params+5            ; block_num high
        jsr     PRODOS_MLI
        .byte   MLI_READ_BLOCK
        .word   rdb_params
        bcc     ok
        ldx     #0                      ; A already has error code
        rts
ok:
        lda     #0
        ldx     #0
        rts

.endproc
