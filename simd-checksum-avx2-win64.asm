; simd-checksum-avx2-win64.asm
;
; MASM x64 translation of simd-checksum-avx2.S for the Windows / MSVC
; build. The upstream .S file is GAS Intel syntax + System V x86-64 ABI;
; this file:
;
;   * Uses MASM syntax (no `.intel_syntax`, `.text` -> `.code`, `.section
;     .rodata` -> `.const`, dotted local labels -> bare labels, RIP-rel
;     addressing implicit).
;   * Adapts to the Microsoft x64 ABI. Win64 passes args in RCX/EDX/R8D/
;     R9/[rsp+40] (buf/len/i/ps1/ps2), but the SysV body wants them in
;     RDI/ESI/EDX/RCX/R8. The prologue saves Win64 non-volatile regs
;     that the body clobbers (RDI, RSI, XMM6, XMM7, XMM12, XMM13, XMM15),
;     then shuffles args into the SysV positions and falls through into
;     the (almost-verbatim) body.
;   * Wraps the function in PROC FRAME with .pushreg / .allocstack /
;     .savexmm128 directives so the OS unwinder can walk this frame
;     (required for SEH and for debuggers to display the call stack
;     during exceptions).
;
; Activated by --enable-roll-asm; the C side (simd-checksum-x86_64.cpp)
; calls this via the extern "C" declaration of get_checksum1_avx2_asm.

INCLUDELIB libcmt

; CHAR_OFFSET stays in lockstep with rsync.h. The original .S checks the
; macro at assemble time; MASM has no #ifdef so we hard-code the default.
CHAR_OFFSET EQU 0

; vmovntdqa needs 32-byte alignment for ymm loads; the simplified .const
; segment defaults to 16-byte alignment, so declare an explicit READONLY
; segment with ALIGN(32). The function entry alignment is just for branch
; predictor friendliness — 16 (the default .code alignment) is fine.
_RDATA SEGMENT READONLY ALIGN(32) 'CONST'
    ALIGN 32
mul_T2 LABEL BYTE
    BYTE 64, 63, 62, 61, 60, 59, 58, 57
    BYTE 56, 55, 54, 53, 52, 51, 50, 49
    BYTE 48, 47, 46, 45, 44, 43, 42, 41
    BYTE 40, 39, 38, 37, 36, 35, 34, 33
    BYTE 32, 31, 30, 29, 28, 27, 26, 25
    BYTE 24, 23, 22, 21, 20, 19, 18, 17
    BYTE 16, 15, 14, 13, 12, 11, 10,  9
    BYTE  8,  7,  6,  5,  4,  3,  2,  1
_RDATA ENDS

.code

; int32 get_checksum1_avx2_asm(schar *buf, int32 len, int32 i,
;                              uint32 *ps1, uint32 *ps2)
;
; Win64 entry args:  RCX=buf, EDX=len, R8D=i, R9=ps1, [RSP+40]=ps2
; After arg shuffle: RDI=buf, ESI=len, EDX=i, RCX=ps1, R8=ps2
; Returns updated `i` in EAX.

ALIGN 16
PUBLIC get_checksum1_avx2_asm
get_checksum1_avx2_asm PROC FRAME
    push    rsi
    .pushreg rsi
    push    rdi
    .pushreg rdi
    ; 5*16 = 80 bytes for XMM saves + 8 bytes pad so the post-prologue
    ; RSP lands at 16-byte alignment (needed for movaps).
    sub     rsp, 88
    .allocstack 88
    movaps  XMMWORD PTR [rsp + 0],  xmm6
    .savexmm128 xmm6, 0
    movaps  XMMWORD PTR [rsp + 16], xmm7
    .savexmm128 xmm7, 16
    movaps  XMMWORD PTR [rsp + 32], xmm12
    .savexmm128 xmm12, 32
    movaps  XMMWORD PTR [rsp + 48], xmm13
    .savexmm128 xmm13, 48
    movaps  XMMWORD PTR [rsp + 64], xmm15
    .savexmm128 xmm15, 64
    .endprolog

    ; Win64 -> SysV-style arg layout. The 5th arg (ps2) sits above the
    ; caller's 32-byte shadow space; from our current RSP that's:
    ;   88 (allocstack) + 16 (push rdi+rsi) + 8 (return addr) + 32
    ;   (shadow space) = 144.
    mov     rdi, rcx                        ; rdi = buf
    mov     esi, edx                        ; esi = len (zero-ext into rsi)
    mov     edx, r8d                        ; edx = i  (zero-ext into rdx)
    mov     rcx, r9                         ; rcx = ps1
    mov     r8,  QWORD PTR [rsp + 144]      ; r8  = ps2

    ; ----- begin body (transliterated from simd-checksum-avx2.S) -----

    vmovd   xmm6, DWORD PTR [rcx]           ; load *ps1
    lea     eax, [rsi - 128]                ; at least 128 bytes to process?
    cmp     edx, eax
    jg      exit_label
    lea     rax, mul_T2                     ; RIP-relative (implicit in MASM)
    vmovntdqa ymm7,  YMMWORD PTR [rax]      ; load T2 multiplication
    vmovntdqa ymm12, YMMWORD PTR [rax + 32] ; constants from memory.
    vpcmpeqd  ymm15, ymm15, ymm15           ; set all elements to -1.

IF CHAR_OFFSET NE 0
    mov     eax, 32 * CHAR_OFFSET
    vmovd   xmm10, eax
    vpbroadcastd ymm10, xmm10
    mov     eax, 528 * CHAR_OFFSET
    vmovd   xmm13, eax
    vpbroadcastd ymm13, xmm13
ENDIF

    vpabsb  ymm15, ymm15                    ; set all byte elements to 1.
    add     rdi, rdx
    vmovdqu ymm2, YMMWORD PTR [rdi]         ; preload the first 64 bytes.
    vmovdqu ymm3, YMMWORD PTR [rdi + 32]
    and     esi, NOT 63                     ; only needed during final
                                            ; reduction, done here to
                                            ; avoid a longer NOP for
                                            ; alignment below.
    add     edx, esi
    shr     rsi, 6                          ; longer opcode for alignment
    add     rdi, 64
    vpxor   xmm1, xmm1, xmm1                ; reset partial sum accumulators
    vpxor   xmm4, xmm4, xmm4
    mov     eax, DWORD PTR [r8]

    ALIGN 16
loop_label:
    vpmaddubsw ymm0, ymm15, ymm2            ; s1 partial sums
    vpmaddubsw ymm5, ymm15, ymm3
    vmovdqu    ymm8, YMMWORD PTR [rdi]      ; preload the next
    vmovdqu    ymm9, YMMWORD PTR [rdi + 32] ; 64 bytes.
    add        rdi, 64
    vpaddd     ymm4, ymm4, ymm6
    vpaddw     ymm5, ymm5, ymm0
    vpsrld     ymm0, ymm5, 16
    vpaddw     ymm5, ymm0, ymm5
    vpaddd     ymm6, ymm5, ymm6
    vpmaddubsw ymm2, ymm7, ymm2             ; s2 partial sums
    vpmaddubsw ymm3, ymm12, ymm3
    prefetcht0 [rdi + 384]                  ; 6 cachelines ahead
    vpaddw     ymm3, ymm2, ymm3
    vpsrldq    ymm2, ymm3, 2
    vpaddd     ymm3, ymm2, ymm3
    vpaddd     ymm1, ymm1, ymm3

IF CHAR_OFFSET NE 0
    vpaddd     ymm6, ymm10, ymm6            ; 32*CHAR_OFFSET
    vpaddd     ymm1, ymm13, ymm1            ; 528*CHAR_OFFSET
ENDIF

    vmovdqa    ymm2, ymm8                   ; move next 64 bytes
    vmovdqa    ymm3, ymm9                   ; into the right registers
    sub        esi, 1
    jnz        loop_label

    ; now we reduce the partial sums.
    vpslld   ymm3, ymm4, 6
    vpsrldq  ymm2, ymm6, 4

    vpaddd   ymm0, ymm3, ymm1
    vpaddd   ymm6, ymm2, ymm6
    vpsrlq   ymm3, ymm0, 32

    vpsrldq  ymm2, ymm6, 8
    vpaddd   ymm0, ymm3, ymm0
    vpsrldq  ymm3, ymm0, 8
    vpaddd   ymm6, ymm2, ymm6
    vpaddd   ymm0, ymm3, ymm0
    vextracti128 xmm2, ymm6, 1
    vextracti128 xmm1, ymm0, 1
    vpaddd   xmm6, xmm2, xmm6
    vmovd    DWORD PTR [rcx], xmm6
    vpaddd   xmm1, xmm1, xmm0
    vmovd    ecx, xmm1
    add      eax, ecx
    mov      DWORD PTR [r8], eax

exit_label:
    vzeroupper
    mov      eax, edx

    ; Epilogue: restore non-volatile regs in reverse order.
    movaps   xmm15, XMMWORD PTR [rsp + 64]
    movaps   xmm13, XMMWORD PTR [rsp + 48]
    movaps   xmm12, XMMWORD PTR [rsp + 32]
    movaps   xmm7,  XMMWORD PTR [rsp + 16]
    movaps   xmm6,  XMMWORD PTR [rsp + 0]
    add      rsp, 88
    pop      rdi
    pop      rsi
    ret
get_checksum1_avx2_asm ENDP

END
