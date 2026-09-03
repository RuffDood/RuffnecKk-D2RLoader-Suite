OPTION CASEMAP:NONE

EXTERN gISC12LoaderSuccessExit:QWORD
EXTERN gISC12LoaderVanillaExit:QWORD
EXTERN ISC12BuildDescriptionIndex:PROC

PUBLIC ISC12LoaderTailMidHook
PUBLIC ISC12LoaderRelayTemplateBegin
PUBLIC ISC12LoaderRelayTemplateVanillaExit
PUBLIC ISC12LoaderRelayTemplateSuccessExit
PUBLIC ISC12LoaderRelayTemplateStatePointer
PUBLIC ISC12LoaderRelayTemplateEnd

.code

; This block is copied byte-for-byte to a process-lifetime relay page. Every
; branch and data reference is internal to the block, so the copy remains
; position-independent. The single pointer slot is filled before the page is
; changed from RW to RX. Mutable state lives on a separate RW page.
ALIGN 16
ISC12LoaderRelayTemplateBegin LABEL BYTE
    mov r11, qword ptr [ISC12LoaderRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je RelayInactive
    jmp qword ptr [r11+10h]

RelayInactive:
    cmp dword ptr [r11+8], 0
    je ISC12LoaderRelayTemplateVanillaExit
    mov r11, qword ptr [rsp+48h]
    test r11, r11
    je RelayFailClosed
    cmp qword ptr [r11+1260h], 01FFh
    ja RelayFailClosed

ISC12LoaderRelayTemplateVanillaExit:
    mov r11, qword ptr [ISC12LoaderRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    mov r15, qword ptr [rsp+0F40h]
    jmp qword ptr [r11+18h]

ISC12LoaderRelayTemplateSuccessExit:
    mov r11, qword ptr [ISC12LoaderRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    jmp qword ptr [r11+20h]

RelayFailClosed:
    ; Once the loader cap is widened, an inactive DLL may only pass vanilla
    ; row counts. FAST_FAIL_FATAL_APP_EXIT cannot be resumed by a user-mode
    ; exception handler, so the old 512-entry stack tail cannot corrupt memory
    ; if shutdown ordering is ever violated.
    mov ecx, 7
    int 29h
    jmp RelayFailClosed

ALIGN 8
ISC12LoaderRelayTemplateStatePointer QWORD 0
ISC12LoaderRelayTemplateEnd LABEL BYTE

ALIGN 16
ISC12LoaderTailMidHook PROC FRAME
    ; The governed seam enters with RSP 16-byte aligned. Reserve only the
    ; Win64 shadow space plus one local slot. Preserve the original RAX because
    ; the vanilla continuation tests it when the helper permits a safe fallback.
    ; FRAME metadata describes this stub's own allocation only. Entry is a
    ; tail-jump from the middle of a native frame, so recoverable exceptions
    ; must never unwind across this boundary: the C++ callback is noexcept,
    ; contains expected faults and fast-fails on every unexpected exception.
    sub rsp, 30h
    .allocstack 30h
    .endprolog
    mov qword ptr [rsp+20h], rax
    mov rcx, qword ptr [rsp+78h]
    call ISC12BuildDescriptionIndex
    mov r10d, eax
    mov rax, qword ptr [rsp+20h]
    add rsp, 30h
    test r10d, r10d
    je VanillaFallback

    mov r15, qword ptr [rsp+0F40h]
    mov r14, qword ptr [rsp+0F48h]
    mov r13, qword ptr [rsp+0F50h]
    mov rbx, qword ptr [rsp+0F78h]
    mov rsi, qword ptr [rsp+0F80h]
    mov rdi, qword ptr [rsp+0F88h]
    jmp qword ptr [gISC12LoaderSuccessExit]

VanillaFallback:
    mov r9, qword ptr [rsp+48h]
    jmp qword ptr [gISC12LoaderVanillaExit]
ISC12LoaderTailMidHook ENDP

END
