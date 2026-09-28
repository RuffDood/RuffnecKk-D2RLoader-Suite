OPTION CASEMAP:NONE

EXTERN FloatingDamageDirectCommit:PROC
EXTERN FloatingDamagePeriodicCommit:PROC
EXTERN gFloatingDamageDirectContinuation:QWORD
EXTERN gFloatingDamagePeriodicContinuation:QWORD

.code

FloatingDamageDirectCommitSite PROC
    ; The original five-byte call is replaced by a Loader-owned inline hook.
    ; At this callsite R14 and RDI carry the two additional damage arguments.
    ; The caller's stack is already aligned for a Win64 call.
    sub rsp, 30h
    mov qword ptr [rsp + 20h], r14
    mov qword ptr [rsp + 28h], rdi
    call FloatingDamageDirectCommit
    add rsp, 30h
    jmp qword ptr [gFloatingDamageDirectContinuation]
FloatingDamageDirectCommitSite ENDP

FloatingDamagePeriodicCommitSite PROC
    sub rsp, 20h
    call FloatingDamagePeriodicCommit
    add rsp, 20h
    jmp qword ptr [gFloatingDamagePeriodicContinuation]
FloatingDamagePeriodicCommitSite ENDP

END
