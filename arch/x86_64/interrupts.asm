; SPDX-License-Identifier: GPL-2.0-only
; nexusOS - Interrupt stubs for x86_64
bits 64
section .text

; Macro for ISR without error code
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0                       ; dummy error code
    push %1                      ; interrupt number
    jmp isr_common_stub
%endmacro

; Macro for ISR with error code
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1                      ; interrupt number (error code already pushed)
    jmp isr_common_stub
%endmacro

; Macro for IRQ
%macro IRQ 2
global irq%1
irq%1:
    push 0
    push %2                      ; interrupt number
    jmp isr_common_stub
%endmacro

; CPU exceptions
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

; IRQs 0-15 mapped to interrupts 32-47
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; Syscall interrupt (128) - legacy support
ISR_NOERRCODE 128

; Common ISR stub - saves all registers and calls C handler
extern isr_handler
isr_common_stub:
    ; Save all general purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Save segment registers
    mov ax, ds
    push rax

    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax

    ; Pass pointer to register frame
    mov rdi, rsp
    call isr_handler

    ; Restore segment registers
    pop rax
    mov ds, ax
    mov es, ax

    ; Restore general purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove error code and interrupt number
    add rsp, 16
    iretq

; SYSCALL entry point (called via SYSCALL instruction, not interrupt)
global syscall_entry
extern syscall_handler
syscall_entry:
    ; SYSCALL: RCX=return RIP, R11=saved RFLAGS
    ; Swap to kernel stack
    swapgs
    mov [gs:8], rsp              ; save user RSP to per-cpu area
    mov rsp, [gs:0]              ; load kernel RSP from per-cpu area

    ; Push fake interrupt frame for consistency
    push 0x1B                    ; user SS (data segment | RPL3)
    push qword [gs:8]           ; user RSP
    push r11                     ; RFLAGS
    push 0x23                    ; user CS (code segment | RPL3)
    push rcx                     ; return RIP

    ; Save registers
    push rax                     ; syscall number
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    push 0                       ; ds placeholder

    ; Call C syscall handler
    ; Linux x86_64 syscall convention: rax=syscall#, rdi,rsi,rdx,r10,r8,r9=args
    mov rdi, rsp                 ; pointer to saved registers
    call syscall_handler

    ; Restore registers
    pop r15                      ; ds placeholder (discard)
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax                      ; return value in rax

    ; Restore user stack
    mov rsp, [gs:8]
    swapgs

    ; Return to user mode
    o64 sysret

; Helper to jump to usermode
global jump_to_usermode
; void jump_to_usermode(uint64_t entry, uint64_t user_stack)
jump_to_usermode:
    mov rcx, rdi                 ; user entry point -> RCX for SYSRET
    mov rsp, rsi                 ; user stack
    mov r11, 0x202               ; RFLAGS with IF set
    swapgs
    o64 sysret

; Load GDT
global gdt_load
gdt_load:
    lgdt [rdi]
    ; Reload segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Far return to reload CS
    pop rdi
    push 0x08
    push rdi
    retfq

; Load IDT
global idt_load
idt_load:
    lidt [rdi]
    ret

; Load TSS
global tss_load
tss_load:
    mov ax, di
    ltr ax
    ret

; Read/Write MSR
global rdmsr_safe
rdmsr_safe:
    mov ecx, edi
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret

global wrmsr_safe
wrmsr_safe:
    mov ecx, edi
    mov eax, esi
    mov rdx, rsi
    shr rdx, 32
    wrmsr
    ret

; Port I/O
global inb
inb:
    mov dx, di
    xor rax, rax
    in al, dx
    ret

global outb
outb:
    mov dx, di
    mov al, sil
    out dx, al
    ret

global inw
inw:
    mov dx, di
    xor rax, rax
    in ax, dx
    ret

global outw
outw:
    mov dx, di
    mov ax, si
    out dx, ax
    ret

global inl
inl:
    mov dx, di
    xor rax, rax
    in eax, dx
    ret

global outl
outl:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret

; Enable/disable interrupts
global sti_wrap
sti_wrap:
    sti
    ret

global cli_wrap
cli_wrap:
    cli
    ret

; Halt
global hlt_wrap
hlt_wrap:
    hlt
    ret

; Invalidate TLB entry
global invlpg
invlpg:
    invlpg [rdi]
    ret

; Read CR2 (page fault address)
global read_cr2
read_cr2:
    mov rax, cr2
    ret

; Read CR3
global read_cr3
read_cr3:
    mov rax, cr3
    ret

; Write CR3
global write_cr3
write_cr3:
    mov cr3, rdi
    ret

; CPUID wrapper
global cpuid_wrap
; void cpuid_wrap(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
cpuid_wrap:
    push rbx
    mov eax, edi
    cpuid
    mov [rsi], eax
    mov [rdx], ebx
    mov [rcx], ecx
    mov [r8], edx
    pop rbx
    ret

; Read LAPIC ID
global read_lapic_id
read_lapic_id:
    mov eax, 1
    cpuid
    shr ebx, 24
    and ebx, 0xFF
    mov eax, ebx
    ret
