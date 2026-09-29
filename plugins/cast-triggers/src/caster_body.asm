; RuffnecKk laboratory adapter. The native prologues are SHA-pinned witnesses.
; Detours inherit the native frame, unwind it, then tail-call the C++ wrapper.
; Resume functions recreate that frame and enter the Loader-owned trampoline.
option casemap:none
EXTERN CastTargetWrapper:QWORD
EXTERN CastPositionWrapper:QWORD
EXTERN CastTargetBodyOriginal:QWORD
EXTERN CastPositionBodyOriginal:QWORD
.code
PUBLIC CastTargetBodyDetour
PUBLIC CastPositionBodyDetour
PUBLIC CastTargetResume
PUBLIC CastPositionResume

CastTargetBodyDetour PROC FRAME
    .pushreg rdi
    .pushreg r14
    .pushreg r15
    .allocstack 70h
    .savereg rbx, 098h
    .savereg rbp, 0A0h
    .savereg rsi, 0A8h
    .endprolog
    mov rbx, [rsp+098h]
    mov rbp, [rsp+0A0h]
    mov rsi, [rsp+0A8h]
    add rsp, 70h
    pop r15
    pop r14
    pop rdi
    jmp QWORD PTR [CastTargetWrapper]
CastTargetBodyDetour ENDP

CastPositionBodyDetour PROC FRAME
    .pushreg rsi
    .pushreg rdi
    .pushreg r12
    .pushreg r14
    .pushreg r15
    .allocstack 70h
    .savereg rbx, 0A8h
    .savereg rbp, 0B0h
    .endprolog
    mov rbx, [rsp+0A8h]
    mov rbp, [rsp+0B0h]
    add rsp, 70h
    pop r15
    pop r14
    pop r12
    pop rdi
    pop rsi
    jmp QWORD PTR [CastPositionWrapper]
CastPositionBodyDetour ENDP

CastTargetResume PROC FRAME
    mov [rsp+10h], rbx
    mov [rsp+18h], rbp
    mov [rsp+20h], rsi
    push rdi
    .pushreg rdi
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 70h
    .allocstack 70h
    .savereg rbx, 098h
    .savereg rbp, 0A0h
    .savereg rsi, 0A8h
    .endprolog
    mov rax, QWORD PTR [CastTargetBodyOriginal]
    jmp rax
CastTargetResume ENDP

CastPositionResume PROC FRAME
    mov [rsp+10h], rbx
    mov [rsp+18h], rbp
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 70h
    .allocstack 70h
    .savereg rbx, 0A8h
    .savereg rbp, 0B0h
    .endprolog
    mov rax, QWORD PTR [CastPositionBodyOriginal]
    jmp rax
CastPositionResume ENDP
END
