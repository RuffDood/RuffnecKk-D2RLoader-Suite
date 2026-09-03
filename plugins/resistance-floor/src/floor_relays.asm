OPTION CASEMAP:NONE

EXTERN ResistanceFloorSelectFloor:PROC
EXTERN gResistanceFloorFirstContinuation:QWORD
EXTERN gResistanceFloorSecondContinuation:QWORD

.code

ResistanceFloorFirstMidHook PROC
    sub rsp, 0B0h
    mov qword ptr [rsp+20h], rax
    mov qword ptr [rsp+28h], rdx
    mov qword ptr [rsp+30h], r8
    mov qword ptr [rsp+38h], r9
    mov qword ptr [rsp+40h], r10
    mov qword ptr [rsp+48h], r11
    movdqu xmmword ptr [rsp+50h], xmm0
    movdqu xmmword ptr [rsp+60h], xmm1
    movdqu xmmword ptr [rsp+70h], xmm2
    movdqu xmmword ptr [rsp+80h], xmm3
    movdqu xmmword ptr [rsp+90h], xmm4
    movdqu xmmword ptr [rsp+0A0h], xmm5

    mov rcx, qword ptr [rsi+10h]
    mov edx, dword ptr [r14+8h]
    call ResistanceFloorSelectFloor
    mov ecx, eax

    movdqu xmm0, xmmword ptr [rsp+50h]
    movdqu xmm1, xmmword ptr [rsp+60h]
    movdqu xmm2, xmmword ptr [rsp+70h]
    movdqu xmm3, xmmword ptr [rsp+80h]
    movdqu xmm4, xmmword ptr [rsp+90h]
    movdqu xmm5, xmmword ptr [rsp+0A0h]
    mov rax, qword ptr [rsp+20h]
    mov rdx, qword ptr [rsp+28h]
    mov r8, qword ptr [rsp+30h]
    mov r9, qword ptr [rsp+38h]
    mov r10, qword ptr [rsp+40h]
    mov r11, qword ptr [rsp+48h]
    add rsp, 0B0h
    jmp qword ptr [gResistanceFloorFirstContinuation]
ResistanceFloorFirstMidHook ENDP

ResistanceFloorSecondMidHook PROC
    sub rsp, 0B0h
    mov qword ptr [rsp+20h], rax
    mov qword ptr [rsp+28h], rdx
    mov qword ptr [rsp+30h], r8
    mov qword ptr [rsp+38h], r9
    mov qword ptr [rsp+40h], r10
    mov qword ptr [rsp+48h], r11
    movdqu xmmword ptr [rsp+50h], xmm0
    movdqu xmmword ptr [rsp+60h], xmm1
    movdqu xmmword ptr [rsp+70h], xmm2
    movdqu xmmword ptr [rsp+80h], xmm3
    movdqu xmmword ptr [rsp+90h], xmm4
    movdqu xmmword ptr [rsp+0A0h], xmm5

    mov rcx, qword ptr [rsi+10h]
    mov edx, dword ptr [r14+8h]
    call ResistanceFloorSelectFloor
    mov ecx, eax

    movdqu xmm0, xmmword ptr [rsp+50h]
    movdqu xmm1, xmmword ptr [rsp+60h]
    movdqu xmm2, xmmword ptr [rsp+70h]
    movdqu xmm3, xmmword ptr [rsp+80h]
    movdqu xmm4, xmmword ptr [rsp+90h]
    movdqu xmm5, xmmword ptr [rsp+0A0h]
    mov rax, qword ptr [rsp+20h]
    mov rdx, qword ptr [rsp+28h]
    mov r8, qword ptr [rsp+30h]
    mov r9, qword ptr [rsp+38h]
    mov r10, qword ptr [rsp+40h]
    mov r11, qword ptr [rsp+48h]
    add rsp, 0B0h
    jmp qword ptr [gResistanceFloorSecondContinuation]
ResistanceFloorSecondMidHook ENDP

END
