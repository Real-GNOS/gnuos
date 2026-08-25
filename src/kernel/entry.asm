; entry.asm — gnuos 内核入口（GPLv2）
; 引导器在长模式下以 rdi = bootinfo* 跳入 _start。

[bits 64]

section .text
global _start
extern kernel_main

_start:
    mov  rsp, stack_top        ; 换用内核自己的栈
    xor  ebp, ebp
    call kernel_main           ; rdi 已是 bootinfo 指针
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 65536
stack_top: