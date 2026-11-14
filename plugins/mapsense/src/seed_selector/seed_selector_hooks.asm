option casemap:none

EXTERN gMapSeedOverrideActive:DWORD
EXTERN gSelectedMapSeed:DWORD
EXTERN gMapSeedFlowState:DWORD
EXTERN gLastObservedMapSeed:DWORD
EXTERN gLastObservedMapSeedValid:DWORD
EXTERN gOriginalDrlgSeedInitialization:QWORD
EXTERN gOriginalGameSeedIngress:QWORD
EXTERN gOriginalSavedSeedCopyA:QWORD
EXTERN gOriginalSavedSeedCopyB:QWORD
EXTERN gOriginalSavedSeedCopyC:QWORD

.code
PUBLIC MapDrlgSeedInitializationHook

MapDrlgSeedInitializationHook PROC
    pushfq
    push r11
    mov r11d, DWORD PTR [gMapSeedOverrideActive]
    test r11d, r11d
    jz SeedInitializationUseOriginal
    mov eax, DWORD PTR [rdi]
    mov r11d, DWORD PTR [gMapSeedFlowState]
    cmp r11d, 2
    jne SeedInitializationCheckInjected
    mov r11d, DWORD PTR [gSelectedMapSeed]
    cmp eax, r11d
    je SeedInitializationPassThrough
    mov DWORD PTR [gMapSeedFlowState], 1
    jmp SeedInitializationForce
SeedInitializationCheckInjected:
    cmp r11d, 1
    je SeedInitializationForce
    mov r11d, DWORD PTR [gSelectedMapSeed]
    cmp eax, r11d
    jne SeedInitializationMarkInjected
    mov DWORD PTR [gMapSeedFlowState], 2
    jmp SeedInitializationPassThrough
SeedInitializationMarkInjected:
    mov DWORD PTR [gMapSeedFlowState], 1
SeedInitializationForce:
    mov ebx, DWORD PTR [gSelectedMapSeed]
    mov DWORD PTR [rdi], ebx
    mov DWORD PTR [rdi+4], 29Ah
    mov DWORD PTR [gLastObservedMapSeed], ebx
    mov r11d, 1
    xchg DWORD PTR [gLastObservedMapSeedValid], r11d
    jmp SeedInitializationContinue
SeedInitializationPassThrough:
    mov DWORD PTR [gLastObservedMapSeed], eax
    mov r11d, 1
    xchg DWORD PTR [gLastObservedMapSeedValid], r11d
    jmp SeedInitializationContinue
SeedInitializationUseOriginal:
    mov r11d, DWORD PTR [rdi]
    mov DWORD PTR [gLastObservedMapSeed], r11d
    mov r11d, 1
    xchg DWORD PTR [gLastObservedMapSeedValid], r11d
SeedInitializationContinue:
    pop r11
    popfq
    jmp QWORD PTR [gOriginalDrlgSeedInitialization]
MapDrlgSeedInitializationHook ENDP

PUBLIC MapGameSeedIngressHook

MapGameSeedIngressHook PROC
    pushfq
    push r10
    push r11
    mov r10d, DWORD PTR [rdi+12Ch]
    mov r11d, DWORD PTR [gMapSeedOverrideActive]
    test r11d, r11d
    jz GameSeedIngressRecordWritten
    mov r11d, DWORD PTR [gMapSeedFlowState]
    test r11d, r11d
    jnz GameSeedIngressCheckValue
    mov r11d, DWORD PTR [gSelectedMapSeed]
    cmp r10d, r11d
    jne GameSeedIngressMarkInjected
    mov DWORD PTR [gMapSeedFlowState], 2
    jmp GameSeedIngressCheckValue
GameSeedIngressMarkInjected:
    mov DWORD PTR [gMapSeedFlowState], 1
GameSeedIngressCheckValue:
    mov r11d, DWORD PTR [gSelectedMapSeed]
    cmp r10d, r11d
    je GameSeedIngressRecordWritten
    mov DWORD PTR [gMapSeedFlowState], 1
    mov r11d, DWORD PTR [gSelectedMapSeed]
    mov DWORD PTR [rdi+12Ch], r11d
    mov r10d, r11d
GameSeedIngressRecordWritten:
    pop r11
    pop r10
    popfq
    jmp QWORD PTR [gOriginalGameSeedIngress]
MapGameSeedIngressHook ENDP

PUBLIC MapSavedSeedCopyAHook

MapSavedSeedCopyAHook PROC
    pushfq
    push r10
    push r11
    mov r10d, eax
    mov r11d, DWORD PTR [gMapSeedOverrideActive]
    test r11d, r11d
    jz SavedSeedCopyARecordWritten
    mov r11d, DWORD PTR [gSelectedMapSeed]
    cmp r10d, r11d
    je SavedSeedCopyAMatchesSaved
    mov DWORD PTR [gMapSeedFlowState], 1
    mov eax, DWORD PTR [gSelectedMapSeed]
    jmp SavedSeedCopyARecordWritten
SavedSeedCopyAMatchesSaved:
    mov DWORD PTR [gMapSeedFlowState], 2
SavedSeedCopyARecordWritten:
    pop r11
    pop r10
    popfq
    jmp QWORD PTR [gOriginalSavedSeedCopyA]
MapSavedSeedCopyAHook ENDP

PUBLIC MapSavedSeedCopyBHook

MapSavedSeedCopyBHook PROC
    pushfq
    push r10
    push r11
    mov r10d, eax
    mov r11d, DWORD PTR [gMapSeedOverrideActive]
    test r11d, r11d
    jz SavedSeedCopyBRecordWritten
    mov r11d, DWORD PTR [gSelectedMapSeed]
    cmp r10d, r11d
    je SavedSeedCopyBMatchesSaved
    mov DWORD PTR [gMapSeedFlowState], 1
    mov eax, DWORD PTR [gSelectedMapSeed]
    jmp SavedSeedCopyBRecordWritten
SavedSeedCopyBMatchesSaved:
    mov DWORD PTR [gMapSeedFlowState], 2
SavedSeedCopyBRecordWritten:
    pop r11
    pop r10
    popfq
    jmp QWORD PTR [gOriginalSavedSeedCopyB]
MapSavedSeedCopyBHook ENDP

PUBLIC MapSavedSeedCopyCHook

MapSavedSeedCopyCHook PROC
    pushfq
    push r10
    push r11
    mov r10d, eax
    mov r11d, DWORD PTR [gMapSeedOverrideActive]
    test r11d, r11d
    jz SavedSeedCopyCRecordWritten
    mov r11d, DWORD PTR [gSelectedMapSeed]
    cmp r10d, r11d
    je SavedSeedCopyCMatchesSaved
    mov DWORD PTR [gMapSeedFlowState], 1
    mov eax, DWORD PTR [gSelectedMapSeed]
    jmp SavedSeedCopyCRecordWritten
SavedSeedCopyCMatchesSaved:
    mov DWORD PTR [gMapSeedFlowState], 2
SavedSeedCopyCRecordWritten:
    pop r11
    pop r10
    popfq
    jmp QWORD PTR [gOriginalSavedSeedCopyC]
MapSavedSeedCopyCHook ENDP

END
