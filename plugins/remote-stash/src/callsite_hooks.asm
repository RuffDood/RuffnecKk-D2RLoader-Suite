OPTION CASEMAP:NONE

EXTERN RemoteStashTown:PROC
EXTERN RemoteStashTransfer:PROC
EXTERN RemoteStashTransition:PROC
EXTERN RemoteStashMovementClose:PROC
EXTERN RemoteStashSharedGold:PROC
EXTERN gRemoteStashTownContinuation:QWORD
EXTERN gRemoteStashTransferContinuation:QWORD
EXTERN gRemoteStashTransitionContinuation:QWORD
EXTERN gRemoteStashMovementCloseContinuation:QWORD
EXTERN gRemoteStashSharedGoldContinuation:QWORD

.code

REMOTE_TOWN_SITE MACRO siteName, slotOffset
siteName PROC
    ; Pass the original CALL return address, rather than this shim's address.
    mov rdx, qword ptr [gRemoteStashTownContinuation + slotOffset]
    sub rsp, 20h
    call RemoteStashTown
    add rsp, 20h
    jmp qword ptr [gRemoteStashTownContinuation + slotOffset]
siteName ENDP
ENDM

REMOTE_TRANSFER_SITE MACRO siteName, slotOffset
siteName PROC
    ; Preserve the native fifth and sixth stack arguments across this call.
    sub rsp, 30h
    mov rax, qword ptr [rsp + 50h]
    mov qword ptr [rsp + 20h], rax
    mov rax, qword ptr [rsp + 58h]
    mov qword ptr [rsp + 28h], rax
    call RemoteStashTransfer
    add rsp, 30h
    jmp qword ptr [gRemoteStashTransferContinuation + slotOffset]
siteName ENDP
ENDM

REMOTE_SIMPLE_SITE MACRO siteName, targetName, continuationName
siteName PROC
    sub rsp, 20h
    call targetName
    add rsp, 20h
    jmp qword ptr [continuationName]
siteName ENDP
ENDM

REMOTE_TOWN_SITE RemoteStashTownSite0, 0
REMOTE_TOWN_SITE RemoteStashTownSite1, 8
REMOTE_TOWN_SITE RemoteStashTownSite2, 16
REMOTE_TOWN_SITE RemoteStashTownSite3, 24

REMOTE_TRANSFER_SITE RemoteStashTransferSite0, 0
REMOTE_TRANSFER_SITE RemoteStashTransferSite1, 8
REMOTE_TRANSFER_SITE RemoteStashTransferSite2, 16
REMOTE_TRANSFER_SITE RemoteStashTransferSite3, 24
REMOTE_TRANSFER_SITE RemoteStashTransferSite4, 32
REMOTE_TRANSFER_SITE RemoteStashTransferSite5, 40
REMOTE_TRANSFER_SITE RemoteStashTransferSite6, 48
REMOTE_TRANSFER_SITE RemoteStashTransferSite7, 56

REMOTE_SIMPLE_SITE RemoteStashTransitionSite, RemoteStashTransition, gRemoteStashTransitionContinuation
REMOTE_SIMPLE_SITE RemoteStashMovementCloseSite, RemoteStashMovementClose, gRemoteStashMovementCloseContinuation
REMOTE_SIMPLE_SITE RemoteStashSharedGoldSite, RemoteStashSharedGold, gRemoteStashSharedGoldContinuation

END
