#include <linux/export.h>
#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/cpufeatures.h>

.intel_syntax noprefix

.section .text

.extern _vmlaunch
  
SYM_TYPED_FUNC_START(save_regs)
    mov [rdi+0],  rbp;
    mov [rdi+8],  rbx;
    mov [rdi+16], rcx;
    mov [rdi+24], rdx;
    mov [rdi+32], rsi;
    mov [rdi+40], rdi;
    mov [rdi+48], r15;
    mov [rdi+56], r14;
    mov [rdi+64], r13;
    mov [rdi+72], r12;
    mov [rdi+80], r11;
    mov [rdi+88], r10;
    mov [rdi+96],  r9;
    mov [rdi+104], r8;
    mov [rdi+112], rax;
    ret
SYM_FUNC_END(save_regs)

SYM_TYPED_FUNC_START(restore_regs)
    mov rbp, [rdi+0]  ;
    mov rbx, [rdi+8]  ;
    mov rcx, [rdi+16] ;
    mov rdx, [rdi+24] ;
    mov rsi, [rdi+32] ;
    mov rdi, [rdi+40] ;
    mov r15, [rdi+48] ;
    mov r14, [rdi+56] ;
    mov r13, [rdi+64] ;
    mov r12, [rdi+72] ;
    mov r11, [rdi+80] ;
    mov r10, [rdi+88] ;
    mov r9,  [rdi+96] ;
    mov r8,  [rdi+104];
    mov rax, [rdi+104];
    ret
SYM_FUNC_END(restore_regs)

SYM_TYPED_FUNC_START(clear_regs)
    xor rax, rax;
    xor rbp, rbp;
    xor rbx, rbx;
    xor rcx, rcx;
    xor rdx, rdx;
    xor rsi, rsi;
    xor rdi, rdi;
    xor r15, r15;
    xor r14, r14;
    xor r13, r13;
    xor r12, r12;
    xor r11, r11;
    xor r10, r10;
    xor r9,  r9 ;
    xor r8,  r8 ;
    ret
SYM_FUNC_END(clear_regs)

SYM_TYPED_FUNC_START(_vmlaunch)
    push rbp
    mov rbp, rsp
    call save_regs;
    push rsi;
    call clear_regs;
    mov rsi, 0x6c14;
    mov rdi, 0x6c16;
    vmwrite rsi, rsp;
    xor rsp, rsp;
    lea rax, [rip+0x9];
    vmwrite rdi, rax;
    xor rax, rax;
    vmlaunch;
    call [rsp];
    pop rsi;
    leave;
    ret
SYM_FUNC_END(clear_regs)

