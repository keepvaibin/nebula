/* The one machine-specific piece of the native OS layer: switching between game threads' stacks.

   Game threads (OSThread) run one at a time on the simulation thread, as on the console's single core; each has its
   own stack in the game's arena (where the GPU can see stack data). nebula_switch_stack saves the callee-saved state
   on the current stack, stores the stack pointer to *from_sp, loads to_sp and restores the state found there. A new
   stack is prepared by nebula_prepare_stack so that the first switch to it enters nebula_thread_trampoline, which
   calls entry(arg). The calling conventions: Win64 (x86-64), System V (x86-64) and AAPCS64 (arm64). */
#include <stddef.h>
#include <stdint.h>

void nebula_switch_stack(void** from_sp, void* to_sp);
void nebula_thread_trampoline(void);

#if defined(_WIN32) && defined(__x86_64__)
/* Win64: rbx rbp rdi rsi r12-r15 and xmm6-xmm15 are callee-saved; the TIB's stack base, limit and deallocation
   stack (gs:0x08, gs:0x10, gs:0x1478) describe the current stack to the system and are switched with it. */
__asm__(
    ".text\n"
    ".globl nebula_switch_stack\n"
    "nebula_switch_stack:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    pushq %gs:0x08\n"
    "    pushq %gs:0x10\n"
    "    pushq %gs:0x1478\n"
    "    subq $0xA0, %rsp\n"
    "    movaps %xmm6, 0x00(%rsp)\n"
    "    movaps %xmm7, 0x10(%rsp)\n"
    "    movaps %xmm8, 0x20(%rsp)\n"
    "    movaps %xmm9, 0x30(%rsp)\n"
    "    movaps %xmm10, 0x40(%rsp)\n"
    "    movaps %xmm11, 0x50(%rsp)\n"
    "    movaps %xmm12, 0x60(%rsp)\n"
    "    movaps %xmm13, 0x70(%rsp)\n"
    "    movaps %xmm14, 0x80(%rsp)\n"
    "    movaps %xmm15, 0x90(%rsp)\n"
    "    movq %rsp, (%rcx)\n"
    "    movq %rdx, %rsp\n"
    "    movaps 0x00(%rsp), %xmm6\n"
    "    movaps 0x10(%rsp), %xmm7\n"
    "    movaps 0x20(%rsp), %xmm8\n"
    "    movaps 0x30(%rsp), %xmm9\n"
    "    movaps 0x40(%rsp), %xmm10\n"
    "    movaps 0x50(%rsp), %xmm11\n"
    "    movaps 0x60(%rsp), %xmm12\n"
    "    movaps 0x70(%rsp), %xmm13\n"
    "    movaps 0x80(%rsp), %xmm14\n"
    "    movaps 0x90(%rsp), %xmm15\n"
    "    addq $0xA0, %rsp\n"
    "    popq %gs:0x1478\n"
    "    popq %gs:0x10\n"
    "    popq %gs:0x08\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rsi\n"
    "    popq %rdi\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    ret\n");
enum { kSavedWords = 11 + 0xA0 / 8 };
#elif defined(__x86_64__)
/* System V: rbx rbp r12-r15 are callee-saved; the x87 control word and MXCSR are kept per thread. */
__asm__(
    ".text\n"
    ".globl nebula_switch_stack\n"
    "nebula_switch_stack:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    subq $8, %rsp\n"
    "    stmxcsr (%rsp)\n"
    "    fnstcw 4(%rsp)\n"
    "    movq %rsp, (%rdi)\n"
    "    movq %rsi, %rsp\n"
    "    ldmxcsr (%rsp)\n"
    "    fldcw 4(%rsp)\n"
    "    addq $8, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    ret\n");
enum { kSavedWords = 7 };
#elif defined(__aarch64__)
/* AAPCS64: x19-x28, fp, lr and d8-d15 are callee-saved. */
__asm__(
    ".text\n"
    ".globl nebula_switch_stack\n"
    "nebula_switch_stack:\n"
    "    sub sp, sp, #0xA0\n"
    "    stp x19, x20, [sp, #0x00]\n"
    "    stp x21, x22, [sp, #0x10]\n"
    "    stp x23, x24, [sp, #0x20]\n"
    "    stp x25, x26, [sp, #0x30]\n"
    "    stp x27, x28, [sp, #0x40]\n"
    "    stp x29, x30, [sp, #0x50]\n"
    "    stp d8, d9, [sp, #0x60]\n"
    "    stp d10, d11, [sp, #0x70]\n"
    "    stp d12, d13, [sp, #0x80]\n"
    "    stp d14, d15, [sp, #0x90]\n"
    "    mov x9, sp\n"
    "    str x9, [x0]\n"
    "    mov sp, x1\n"
    "    ldp x19, x20, [sp, #0x00]\n"
    "    ldp x21, x22, [sp, #0x10]\n"
    "    ldp x23, x24, [sp, #0x20]\n"
    "    ldp x25, x26, [sp, #0x30]\n"
    "    ldp x27, x28, [sp, #0x40]\n"
    "    ldp x29, x30, [sp, #0x50]\n"
    "    ldp d8, d9, [sp, #0x60]\n"
    "    ldp d10, d11, [sp, #0x70]\n"
    "    ldp d12, d13, [sp, #0x80]\n"
    "    ldp d14, d15, [sp, #0x90]\n"
    "    add sp, sp, #0xA0\n"
    "    ret\n");
enum { kSavedWords = 0xA0 / 8 };
#else
#error "nebula_switch_stack: no implementation for this architecture"
#endif

/* The entry and argument of a thread whose stack has not run yet; written by nebula_prepare_stack just above the
   saved state, read once by the trampoline. */
typedef struct {
    void (*entry)(void*);
    void* arg;
} NebulaStackStart;

void nebula_thread_start(void (*entry)(void*), void* arg);

#if defined(__x86_64__)
/* Entered by the first switch's ret with rsp 16-byte aligned and the start record at rsp; the call below is made
   with rsp 16-byte aligned (Win64 also reserves its 32-byte shadow space). */
__asm__(
    ".text\n"
    ".globl nebula_thread_trampoline\n"
    "nebula_thread_trampoline:\n"
#if defined(_WIN32)
    "    movq 0(%rsp), %rcx\n"
    "    movq 8(%rsp), %rdx\n"
    "    subq $0x20, %rsp\n"
#else
    "    movq 0(%rsp), %rdi\n"
    "    movq 8(%rsp), %rsi\n"
#endif
    "    call nebula_thread_start\n"
    "    ud2\n");
#elif defined(__aarch64__)
/* Entered by the first switch's ret (lr = trampoline) with the start record at sp. */
__asm__(
    ".text\n"
    ".globl nebula_thread_trampoline\n"
    "nebula_thread_trampoline:\n"
    "    ldp x0, x1, [sp]\n"
    "    add sp, sp, #16\n"
    "    bl nebula_thread_start\n"
    "    brk #0\n");
#endif

/* Lays out a stack (top: one past its highest byte) so that switching to the returned stack pointer starts
   entry(arg) on it. */
void* nebula_prepare_stack(void* top, size_t size, void (*entry)(void*), void* arg) {
    uintptr_t sp = ((uintptr_t)top) & ~(uintptr_t)15;
    uintptr_t* p;
    (void)size;
#if defined(__x86_64__)
    /* From the top down: the start record (2 words), the return address (the trampoline), the saved state. After
       the state is popped and ret consumes the return address, rsp points at the start record, 16-byte aligned. */
    sp -= 16;
    ((NebulaStackStart*)sp)->entry = entry;
    ((NebulaStackStart*)sp)->arg = arg;
    sp -= 8;
    *(uintptr_t*)sp = (uintptr_t)&nebula_thread_trampoline;
    sp -= kSavedWords * 8;
    p = (uintptr_t*)sp;
    for (int i = 0; i < kSavedWords; i++) {
        p[i] = 0;
    }
#if defined(_WIN32)
    /* Saved TIB words (popped in reverse order of the pushes): deallocation stack, limit, base. */
    p[0xA0 / 8 + 0] = (uintptr_t)top - size;
    p[0xA0 / 8 + 1] = (uintptr_t)top - size;
    p[0xA0 / 8 + 2] = (uintptr_t)top;
#else
    /* MXCSR (all exceptions masked, round to nearest) and the x87 control word (extended, all masked). */
    ((uint32_t*)p)[0] = 0x1F80;
    ((uint16_t*)p)[2] = 0x037F;
#endif
#elif defined(__aarch64__)
    sp -= 16;
    ((NebulaStackStart*)sp)->entry = entry;
    ((NebulaStackStart*)sp)->arg = arg;
    sp -= kSavedWords * 8;
    p = (uintptr_t*)sp;
    for (int i = 0; i < kSavedWords; i++) {
        p[i] = 0;
    }
    p[0x58 / 8] = (uintptr_t)&nebula_thread_trampoline; /* x30 (lr) */
#endif
    return (void*)sp;
}
