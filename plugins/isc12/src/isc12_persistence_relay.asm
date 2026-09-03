OPTION CASEMAP:NONE

EXTERN ISC12PrepareNativeStoreRead:PROC
EXTERN ISC12PrepareNativeStoreWrite:PROC
EXTERN ISC12ReadAuxiliaryWithPreflight:PROC
EXTERN ISC12ReadRegularWithPreflight:PROC
EXTERN ISC12CopyPreviewWithPreflight:PROC
EXTERN ISC12InvokeItemAction9CNative:PROC
EXTERN ISC12InvokeItemAction9DNative:PROC
EXTERN ISC12CaptureItemAction9CQueue:PROC
EXTERN ISC12CaptureItemAction9DQueue:PROC

EXTERN gISC12PersistenceReaderContinueExit:QWORD
EXTERN gISC12PersistenceReaderRejectedExit:QWORD
EXTERN gISC12PersistenceWriterVanillaExit:QWORD
EXTERN gISC12PersistenceWriterCommittedExit:QWORD
EXTERN gISC12PersistenceWriterRejectedExit:QWORD
EXTERN gISC12CodecReturnExit:QWORD
EXTERN gISC12ItemTransportReturnExit:QWORD

PUBLIC ISC12PersistenceReaderMidHook
PUBLIC ISC12PersistenceWriterMidHook
PUBLIC ISC12AuxiliaryReaderCallHook
PUBLIC ISC12PlayerReaderCallHook
PUBLIC ISC12PlayerPreviewCallHook
PUBLIC ISC12ItemAction9CEntryHook
PUBLIC ISC12ItemAction9DEntryHook
PUBLIC ISC12ItemAction9CQueueHook
PUBLIC ISC12ItemAction9DQueueHook
PUBLIC ISC12PersistenceRelayTemplateBegin
PUBLIC ISC12PersistenceRelayTemplateWriterEntry
PUBLIC ISC12PersistenceRelayTemplateReaderContinueExit
PUBLIC ISC12PersistenceRelayTemplateReaderRejectedExit
PUBLIC ISC12PersistenceRelayTemplateWriterVanillaExit
PUBLIC ISC12PersistenceRelayTemplateWriterCommittedExit
PUBLIC ISC12PersistenceRelayTemplateWriterRejectedExit
PUBLIC ISC12PersistenceRelayTemplatePlayerSaveFinalizeEntry
PUBLIC ISC12PersistenceRelayTemplateAuxiliaryReaderEntry
PUBLIC ISC12PersistenceRelayTemplatePlayerReaderEntry
PUBLIC ISC12PersistenceRelayTemplatePlayerPreviewEntry
PUBLIC ISC12PersistenceRelayTemplateCodecReturnExit
PUBLIC ISC12PersistenceRelayTemplatePacket9CQueueEntry
PUBLIC ISC12PersistenceRelayTemplatePacket9DQueueEntry
PUBLIC ISC12PersistenceRelayTemplatePacket9CProducerEntry
PUBLIC ISC12PersistenceRelayTemplatePacket9DProducerEntry
PUBLIC ISC12PersistenceRelayTemplateItemTransportReturnExit
PUBLIC ISC12PersistenceRelayTemplatePacket9CTrampoline
PUBLIC ISC12PersistenceRelayTemplatePacket9CTrampolineRel32
PUBLIC ISC12PersistenceRelayTemplatePacket9CTrampolineEnd
PUBLIC ISC12PersistenceRelayTemplatePacket9DTrampoline
PUBLIC ISC12PersistenceRelayTemplatePacket9DTrampolineRel32
PUBLIC ISC12PersistenceRelayTemplatePacket9DTrampolineEnd
PUBLIC ISC12PersistenceRelayTemplateItemTrampolineUnwindInfo
PUBLIC ISC12PersistenceRelayTemplateStatePointer
PUBLIC ISC12PersistenceRelayTemplateEnd

.code

; Both future save hooks share one process-lifetime RX block and one separate
; RW state block. All branches and the state-slot reference are internal, so
; this complete block remains position-independent after it is copied near the
; two save seams. P3b prepares it but deliberately publishes neither hook.
ALIGN 16
ISC12PersistenceRelayTemplateBegin LABEL BYTE
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+10h]

ISC12PersistenceRelayTemplateReaderContinueExit:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    lea rcx, [rsp+28h]
    jmp qword ptr [r11+20h]

ISC12PersistenceRelayTemplateReaderRejectedExit:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    jmp qword ptr [r11+28h]

ALIGN 16
ISC12PersistenceRelayTemplateWriterEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+18h]

ISC12PersistenceRelayTemplateWriterVanillaExit:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    lea rcx, [rsp+28h]
    jmp qword ptr [r11+30h]

ISC12PersistenceRelayTemplateWriterCommittedExit:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    jmp qword ptr [r11+38h]

ISC12PersistenceRelayTemplateWriterRejectedExit:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    jmp qword ptr [r11+40h]

PersistenceRelayFailClosed:
    ; An installed persistence seam may never fall back into vanilla after
    ; its DLL handler is inactive: doing so could decode an ISC12 payload as
    ; nine-bit data or destructively rewrite its canonical file.
    mov ecx, 7
    int 29h
    jmp PersistenceRelayFailClosed

ALIGN 16
ISC12PersistenceRelayTemplatePlayerSaveFinalizeEntry:
    ; Leaf replacement for the native bit-writer used-end helper. RCX owns
    ; the writer. RAX preserves the native used-end result while EDX carries
    ; the sticky overflow flag to the caller's final status publication.
    cmp qword ptr [rcx+18h], 0
    mov rax, qword ptr [rcx+10h]
    je PlayerSaveFinalizeNoIncrement
    inc rax
PlayerSaveFinalizeNoIncrement:
    mov edx, dword ptr [rcx+20h]
    ret

; These three entries are CALL targets, not inline prologue detours. Their
; native return address therefore remains on the original stack. The DLL FRAME
; wrappers call the untouched native owners, then leave the DLL through the
; shared copied exit so rundown reaches zero only after all DLL code is gone.
ALIGN 16
ISC12PersistenceRelayTemplateAuxiliaryReaderEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    cmp dword ptr [r11+8], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+48h]

ALIGN 16
ISC12PersistenceRelayTemplatePlayerReaderEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    cmp dword ptr [r11+8], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+50h]

ALIGN 16
ISC12PersistenceRelayTemplatePlayerPreviewEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    cmp dword ptr [r11+8], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+58h]

ISC12PersistenceRelayTemplateCodecReturnExit:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    ret

; When the canonical transaction is eventually activated, G9 transport is
; published before every codec width mutation. Queue relays suppress native
; dispatch and copy into TLS staging; producer relays invoke the DLL FRAME
; wrappers. A non-ready or inactive copied relay may never pass through.
ALIGN 16
ISC12PersistenceRelayTemplatePacket9CQueueEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    cmp dword ptr [r11+60h], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+78h]

ALIGN 16
ISC12PersistenceRelayTemplatePacket9DQueueEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    cmp dword ptr [r11+60h], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+80h]

ALIGN 16
ISC12PersistenceRelayTemplatePacket9CProducerEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    cmp dword ptr [r11+60h], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+68h]

ALIGN 16
ISC12PersistenceRelayTemplatePacket9DProducerEntry:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock inc dword ptr [r11]
    cmp dword ptr [r11+4], 0
    je PersistenceRelayFailClosed
    cmp dword ptr [r11+60h], 0
    je PersistenceRelayFailClosed
    jmp qword ptr [r11+70h]

ISC12PersistenceRelayTemplateItemTransportReturnExit:
    mov r11, qword ptr [ISC12PersistenceRelayTemplateStatePointer]
    lock dec dword ptr [r11]
    ret

ALIGN 16
ISC12PersistenceRelayTemplatePacket9CTrampoline LABEL BYTE
    db 040h,053h,055h,056h,057h,0E9h
ISC12PersistenceRelayTemplatePacket9CTrampolineRel32 LABEL BYTE
    dd 0
ISC12PersistenceRelayTemplatePacket9CTrampolineEnd LABEL BYTE

ALIGN 16
ISC12PersistenceRelayTemplatePacket9DTrampoline LABEL BYTE
    db 040h,053h,055h,056h,057h,0E9h
ISC12PersistenceRelayTemplatePacket9DTrampolineRel32 LABEL BYTE
    dd 0
ISC12PersistenceRelayTemplatePacket9DTrampolineEnd LABEL BYTE

ALIGN 4
ISC12PersistenceRelayTemplateItemTrampolineUnwindInfo LABEL BYTE
    db 001h,005h,004h,000h,005h,070h
    db 004h,060h,003h,050h,002h,030h

ALIGN 8
ISC12PersistenceRelayTemplateStatePointer QWORD 0
ISC12PersistenceRelayTemplateEnd LABEL BYTE

ALIGN 16
ISC12PersistenceReaderMidHook PROC FRAME
    ; At 0x9FC654 RSP is aligned, RBX owns the locked save object, RDI is the
    ; announced file length, EAX is the native read status and [RSP+30h] is
    ; the native DWORD bytes-read slot.
    ; FRAME metadata covers this stub's allocation, not generic unwind through
    ; the tail-entered native frame or the copied relay. The noexcept callback
    ; contains expected failures and fast-fails every unexpected exception.
    sub rsp, 20h
    .allocstack 20h
    .endprolog
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, dword ptr [rsp+50h]
    mov r9d, eax
    call ISC12PrepareNativeStoreRead
    add rsp, 20h
    cmp eax, 2
    je ReaderRejected
    cmp eax, 1
    jbe ReaderContinue
    mov ecx, 7
    int 29h

ReaderContinue:
    jmp qword ptr [gISC12PersistenceReaderContinueExit]

ReaderRejected:
    jmp qword ptr [gISC12PersistenceReaderRejectedExit]
ISC12PersistenceReaderMidHook ENDP

ALIGN 16
ISC12PersistenceWriterMidHook PROC FRAME
    ; At 0x9F95A2 RSP is aligned, RBX owns the locked save object and the
    ; already-built NUL-terminated UTF-8 canonical path begins at [RSP+40h].
    ; The stolen native constructor at 0x11C7E20 performs exactly this
    ; INVALID_HANDLE_VALUE initialization. Both target continuations may then
    ; execute the native close helper safely without opening the final path.
    ; As above, no recoverable exception may unwind across this tail-entered
    ; boundary; registered/chained unwind would be required before allowing it.
    sub rsp, 20h
    .allocstack 20h
    .endprolog
    mov qword ptr [rsp+48h], -1
    mov rcx, rbx
    lea rdx, [rsp+60h]
    call ISC12PrepareNativeStoreWrite
    add rsp, 20h
    test eax, eax
    je WriterVanilla
    cmp eax, 1
    je WriterCommitted
    cmp eax, 2
    je WriterRejected
    mov ecx, 7
    int 29h

WriterVanilla:
    jmp qword ptr [gISC12PersistenceWriterVanillaExit]

WriterCommitted:
    jmp qword ptr [gISC12PersistenceWriterCommittedExit]

WriterRejected:
    jmp qword ptr [gISC12PersistenceWriterRejectedExit]
ISC12PersistenceWriterMidHook ENDP

ALIGN 16
ISC12AuxiliaryReaderCallHook PROC FRAME
    ; Preserve the native six-argument ABI when nesting the noexcept helper.
    ; At entry arg5/arg6 are [RSP+28h]/[RSP+30h]. After 38h bytes of shadow,
    ; outgoing arg5/arg6 live at [RSP+20h]/[RSP+28h].
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov eax, dword ptr [rsp+60h]
    mov dword ptr [rsp+20h], eax
    mov eax, dword ptr [rsp+68h]
    mov dword ptr [rsp+28h], eax
    call ISC12ReadAuxiliaryWithPreflight
    add rsp, 38h
    jmp qword ptr [gISC12CodecReturnExit]
ISC12AuxiliaryReaderCallHook ENDP

ALIGN 16
ISC12PlayerReaderCallHook PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov eax, dword ptr [rsp+60h]
    mov dword ptr [rsp+20h], eax
    mov eax, dword ptr [rsp+68h]
    mov dword ptr [rsp+28h], eax
    call ISC12ReadRegularWithPreflight
    add rsp, 38h
    jmp qword ptr [gISC12CodecReturnExit]
ISC12PlayerReaderCallHook ENDP

ALIGN 16
ISC12PlayerPreviewCallHook PROC FRAME
    ; Four-argument native copy ABI; 28h supplies shadow and call alignment.
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    call ISC12CopyPreviewWithPreflight
    add rsp, 28h
    jmp qword ptr [gISC12CodecReturnExit]
ISC12PlayerPreviewCallHook ENDP

ALIGN 16
ISC12ItemAction9CEntryHook PROC FRAME
    ; Native ABI: client, item, action, temporary flags, gamble. The helper
    ; owns staging and invokes the registered 10-byte producer trampoline.
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov eax, dword ptr [rsp+60h]
    mov dword ptr [rsp+20h], eax
    call ISC12InvokeItemAction9CNative
    add rsp, 38h
    jmp qword ptr [gISC12ItemTransportReturnExit]
ISC12ItemAction9CEntryHook ENDP

ALIGN 16
ISC12ItemAction9DEntryHook PROC FRAME
    ; Native ABI: client, parent, item, action, temporary flags, gamble.
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov eax, dword ptr [rsp+60h]
    mov dword ptr [rsp+20h], eax
    mov eax, dword ptr [rsp+68h]
    mov dword ptr [rsp+28h], eax
    call ISC12InvokeItemAction9DNative
    add rsp, 38h
    jmp qword ptr [gISC12ItemTransportReturnExit]
ISC12ItemAction9DEntryHook ENDP

ALIGN 16
ISC12ItemAction9CQueueHook PROC FRAME
    ; The staged queue relay never calls the native queue here. Root exit
    ; performs the final all-packet validation before any synchronous flush.
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    call ISC12CaptureItemAction9CQueue
    add rsp, 28h
    jmp qword ptr [gISC12ItemTransportReturnExit]
ISC12ItemAction9CQueueHook ENDP

ALIGN 16
ISC12ItemAction9DQueueHook PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    call ISC12CaptureItemAction9DQueue
    add rsp, 28h
    jmp qword ptr [gISC12ItemTransportReturnExit]
ISC12ItemAction9DQueueHook ENDP

END
