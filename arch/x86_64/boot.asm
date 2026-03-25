; SPDX-License-Identifier: GPL-2.0-only
; nexusOS v0.01 - Multiboot2 boot entry (x86_64)
; Sets up long mode, page tables, GDT, then jumps to C kernel

section .multiboot2
align 8
multiboot2_header_start:
    dd 0xE85250D6                ; magic
    dd 0                         ; arch: i386 (protected mode)
    dd multiboot2_header_end - multiboot2_header_start  ; header length
    dd -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header_start)) ; checksum

    ; Framebuffer tag
    align 8
    dw 5                         ; type: framebuffer
    dw 0                         ; flags
    dd 20                        ; size
    dd 1024                      ; width
    dd 768                       ; height
    dd 32                        ; depth

    ; Module alignment tag
    align 8
    dw 6                         ; type: module alignment
    dw 0                         ; flags
    dd 8                         ; size

    ; End tag
    align 8
    dw 0                         ; type
    dw 0                         ; flags
    dd 8                         ; size
multiboot2_header_end:

section .bss
align 4096
; Page tables for identity mapping first 4GB
p4_table: resb 4096
p3_table: resb 4096
p2_table: resb 4096
p2_table2: resb 4096
p2_table3: resb 4096
p2_table4: resb 4096

align 16
stack_bottom: resb 65536        ; 64KB stack
global stack_top
stack_top:

; AP (Application Processor) stacks for SMP
global ap_stacks
ap_stacks: resb 65536 * 16     ; 64KB * 16 CPUs max

section .data
global multiboot2_info_ptr
global multiboot2_magic
multiboot2_info_ptr: dq 0
multiboot2_magic: dd 0

section .text
bits 32
global _start
extern kernel_main

_start:
    cli
    ; Save multiboot2 info
    mov [multiboot2_magic], eax
    mov [multiboot2_info_ptr], ebx

    ; Check CPUID support
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_long_mode

    ; Check extended CPUID
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    ; Check long mode support
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ; Setup page tables - identity map first 4GB using 2MB pages
    ; P4[0] -> P3
    mov eax, p3_table
    or eax, 0x03                 ; present + writable
    mov [p4_table], eax

    ; P3[0] -> P2
    mov eax, p2_table
    or eax, 0x03
    mov [p3_table], eax

    ; P3[1] -> P2_2
    mov eax, p2_table2
    or eax, 0x03
    mov [p3_table + 8], eax

    ; P3[2] -> P2_3
    mov eax, p2_table3
    or eax, 0x03
    mov [p3_table + 16], eax

    ; P3[3] -> P2_4
    mov eax, p2_table4
    or eax, 0x03
    mov [p3_table + 24], eax

    ; Fill all 4 P2 tables (4GB total) with 2MB huge pages
    ; P2 table 0: 0x00000000 - 0x3FFFFFFF (1GB)
    mov ecx, 0
.map_p2:
    mov eax, ecx
    shl eax, 21                  ; * 2MB
    or eax, 0x83                 ; present + writable + huge page
    mov [p2_table + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2

    ; P2 table 1: 0x40000000 - 0x7FFFFFFF
    mov ecx, 0
.map_p2_2:
    mov eax, ecx
    shl eax, 21
    add eax, 0x40000000
    or eax, 0x83
    mov [p2_table2 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_2

    ; P2 table 2: 0x80000000 - 0xBFFFFFFF
    mov ecx, 0
.map_p2_3:
    mov eax, ecx
    shl eax, 21
    add eax, 0x80000000
    or eax, 0x83
    mov [p2_table3 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_3

    ; P2 table 3: 0xC0000000 - 0xFFFFFFFF (includes VESA framebuffer)
    mov ecx, 0
.map_p2_4:
    mov eax, ecx
    shl eax, 21
    add eax, 0xC0000000
    or eax, 0x83
    mov [p2_table4 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_4

    ; Load P4 into CR3
    mov eax, p4_table
    mov cr3, eax

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Enable long mode in EFER MSR
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8              ; LME
    or eax, 1 << 0              ; SCE (SYSCALL enable)
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Load 64-bit GDT
    lgdt [gdt64.pointer]

    ; Far jump to 64-bit code
    jmp 0x08:long_mode_start

.no_long_mode:
    ; Print error on VGA
    mov dword [0xB8000], 0x4F524F45 ; "ER"
    mov dword [0xB8004], 0x4F3A4F52 ; "R:"
    mov dword [0xB8008], 0x4F4F4F4E ; "NO"
    mov dword [0xB800C], 0x4F344F36 ; "64"
    hlt

bits 64
long_mode_start:
    ; Setup segment registers
    mov ax, 0x10                 ; data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Setup stack
    mov rsp, stack_top

    ; Clear BSS would be done by loader, but be safe
    ; Enable SSE
    mov rax, cr0
    and ax, 0xFFFB               ; clear CR0.EM
    or ax, 0x2                   ; set CR0.MP
    mov cr0, rax
    mov rax, cr4
    or ax, 3 << 9                ; set CR4.OSFXSR and CR4.OSXMMEXCPT
    mov cr4, rax

    ; Call kernel main
    xor rdi, rdi
    mov edi, [multiboot2_magic]
    mov rsi, [multiboot2_info_ptr]
    call kernel_main

    ; Halt if kernel returns
    cli
.hang:
    hlt
    jmp .hang

; ---------- GDT for 64-bit mode ----------
section .rodata
align 16
gdt64:
    dq 0                         ; null descriptor
.code: equ $ - gdt64
    dq (1<<43)|(1<<44)|(1<<47)|(1<<53) ; code segment: executable, code, present, 64-bit
.data: equ $ - gdt64
    dq (1<<44)|(1<<47)|(1<<41)   ; data segment: code=0, present, writable
.user_code: equ $ - gdt64
    dq (1<<43)|(1<<44)|(1<<47)|(1<<53)|(3<<45) ; user code: DPL=3
.user_data: equ $ - gdt64
    dq (1<<44)|(1<<47)|(1<<41)|(3<<45) ; user data: DPL=3
.tss: equ $ - gdt64
    dq 0                         ; TSS low (filled at runtime)
    dq 0                         ; TSS high
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

; Export GDT symbols
global gdt64
global gdt64.code
global gdt64.data
global gdt64.user_code
global gdt64.user_data
global gdt64.tss
global gdt64.pointer

; ---------- AP trampoline code ----------
section .ap_trampoline
bits 16
global ap_trampoline_start
global ap_trampoline_end
ap_trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    ; Load GDT
    lgdt [0x7100]                ; AP GDT pointer stored here by BSP
    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:(.ap_pm - ap_trampoline_start + 0x7000)
bits 32
.ap_pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    ; Load page tables (same as BSP)
    mov eax, [0x7200]            ; CR3 value stored by BSP
    mov cr3, eax
    ; Enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8) | (1 << 0)
    wrmsr
    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ; Load 64-bit GDT
    lgdt [0x7100]
    jmp 0x08:(.ap_lm - ap_trampoline_start + 0x7000)
bits 64
.ap_lm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Get CPU ID from LAPIC
    mov eax, 1
    cpuid
    shr ebx, 24
    and ebx, 0xFF
    ; Setup stack for this AP
    mov rax, rbx
    shl rax, 16                  ; * 65536
    lea rsp, [ap_stacks + rax + 65536]
    ; Store cpu_id as argument
    mov rdi, rbx
    ; Call AP entry
    extern ap_main
    call ap_main
    cli
    hlt
ap_trampoline_end:
