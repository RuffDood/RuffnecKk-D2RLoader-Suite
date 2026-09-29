; RuffnecKk laboratory adapter for the pinned 0x44C030 prologue.
option casemap:none
EXTERN DamageBodyWrapper:QWORD
EXTERN DamageBodyOriginal:QWORD
EXTERN DamageSecurityCookie:QWORD
.code
PUBLIC DamageBodyDetour
PUBLIC DamageBodyResume
DamageBodyDetour PROC FRAME
    .pushreg rsi
    .pushreg rdi
    .pushreg r12
    .allocstack 510h
    .endprolog
    add rsp, 510h
    pop r12
    pop rdi
    pop rsi
    jmp QWORD PTR [DamageBodyWrapper]
DamageBodyDetour ENDP
DamageBodyResume PROC FRAME
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    sub rsp, 510h
    .allocstack 510h
    .endprolog
    mov rax, QWORD PTR [DamageSecurityCookie]
    mov rax, [rax]
    xor rax, rsp
    mov [rsp+4D0h], rax
    mov rax, QWORD PTR [DamageBodyOriginal]
    jmp rax
DamageBodyResume ENDP
END
