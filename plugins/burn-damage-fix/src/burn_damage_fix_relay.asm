OPTION CASEMAP:NONE

EXTERN BurnDamageFixNormalizeGeneric:PROC
EXTERN gBurnDamageFixProductionContinuation:QWORD

.code

BurnDamageFixProductionMidHook PROC
    ; At 0x44CB32: RSI=attacker, EBX=SrcDam*existing Burn, and R8 contains
    ; the freshly advanced native RNG state. The Win64 caller frame is aligned.
    sub rsp, 20h
    mov rcx, rsi
    mov edx, ebx
    ; R8D is already the third argument: the current native random value.
    call BurnDamageFixNormalizeGeneric
    add rsp, 20h

    mov ebx, eax
    ; Recreate the sign flag consumed by the native CMOVS at the continuation.
    test ebx, ebx
    jmp qword ptr [gBurnDamageFixProductionContinuation]
BurnDamageFixProductionMidHook ENDP

END
