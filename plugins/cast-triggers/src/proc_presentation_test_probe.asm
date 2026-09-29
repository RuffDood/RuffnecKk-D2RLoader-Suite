option casemap:none
.code
PresentationTargetProbe proc frame
    push rbx
    .pushreg rbx
    sub rsp,100h
    .allocstack 100h
    .endprolog
    mov rax,rcx
    mov rbx,rdx
    mov [rsp+0D0h],r8d
    mov [rsp+0D8h],r9d
    mov dword ptr [rsp+0E0h],0BEEFh
    jmp rax
public PresentationTargetResume
PresentationTargetResume::
    mov eax,r9d
    add rsp,100h
    pop rbx
    ret
PresentationTargetProbe endp
PresentationRewindProbe proc frame
    sub rsp,38h
    .allocstack 38h
    .endprolog
    mov rax,rcx
    mov rcx,rdx
    xor edx,edx
    jmp rax
public PresentationRewindResume
PresentationRewindResume::
    add rsp,38h
    ret
PresentationRewindProbe endp
end
