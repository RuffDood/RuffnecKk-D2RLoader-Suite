OPTION CASEMAP:NONE

EXTERN PlayerXPlayersAtoi:PROC
EXTERN PlayerXPlayersApply:PROC
EXTERN PlayerXMonsterOffense:PROC
EXTERN PlayerXNoDropCount:PROC
EXTERN PlayerXNoDropCap:PROC
EXTERN gPlayerXPlayersAtoiContinuation:QWORD
EXTERN gPlayerXPlayersApplyContinuation:QWORD
EXTERN gPlayerXMonsterOffenseContinuation:QWORD
EXTERN gPlayerXNoDropCountContinuation:QWORD
EXTERN gPlayerXNoDropCapContinuation:QWORD

.code

PlayerXPlayersAtoiSite PROC
    sub rsp, 20h
    call PlayerXPlayersAtoi
    add rsp, 20h
    jmp qword ptr [gPlayerXPlayersAtoiContinuation]
PlayerXPlayersAtoiSite ENDP

PlayerXPlayersApplySite PROC
    sub rsp, 20h
    call PlayerXPlayersApply
    add rsp, 20h
    jmp qword ptr [gPlayerXPlayersApplyContinuation]
PlayerXPlayersApplySite ENDP

PlayerXMonsterOffenseSite PROC
    sub rsp, 20h
    call PlayerXMonsterOffense
    add rsp, 20h
    jmp qword ptr [gPlayerXMonsterOffenseContinuation]
PlayerXMonsterOffenseSite ENDP

PlayerXNoDropCountSite PROC
    ; The old call pushed a return address before entering the hook.
    ; Its return-address slot +48h is native RSP +40h at this site.
    lea rdx, qword ptr [rsp + 40h]
    sub rsp, 20h
    call PlayerXNoDropCount
    add rsp, 20h
    jmp qword ptr [gPlayerXNoDropCountContinuation]
PlayerXNoDropCountSite ENDP

PlayerXNoDropCapSite PROC
    lea r9, qword ptr [rsp + 40h]
    sub rsp, 20h
    call PlayerXNoDropCap
    add rsp, 20h
    jmp qword ptr [gPlayerXNoDropCapContinuation]
PlayerXNoDropCapSite ENDP

END
