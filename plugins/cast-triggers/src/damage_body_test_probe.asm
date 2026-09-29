option casemap:none
EXTERN DamageSecurityCookie:QWORD
EXTERN DamageBodyDetour:PROC
.code
PUBLIC DamageTestProbe
DamageTestProbe PROC
    mov rax, [rsp+4D0h]
    xor rax, rsp
    mov r10, QWORD PTR [DamageSecurityCookie]
    cmp rax, [r10]
    je cookie_ok
    int 3
cookie_ok:
    jmp DamageBodyDetour
DamageTestProbe ENDP
END
