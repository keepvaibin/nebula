/* What a Windows image needs without a C runtime: the list of static constructors and the compiler's small helpers. */
int _fltused = 1;

void _purecall(void) {
    __builtin_trap();
}

/* Static constructors: the compiler lists them between these two in the sections it sorts by name. */
typedef void (*Constructor)(void);
__attribute__((section(".CRT$XCA"), used)) static Constructor sConstructorsBegin = 0;
__attribute__((section(".CRT$XCZ"), used)) static Constructor sConstructorsEnd = 0;

void nebula_run_constructors(void) {
    for (Constructor* p = &sConstructorsBegin + 1; p < &sConstructorsEnd; ++p) {
        if (*p != 0) {
            (*p)();
        }
    }
}

/* A frame over a page touches each page on the way down, so the stack's guard page is never stepped over. */
__asm__(
    ".text\n"
    ".globl __chkstk\n"
    "__chkstk:\n"
    "    pushq %rcx\n"
    "    pushq %rax\n"
    "    leaq 24(%rsp), %rcx\n"
    "1:\n"
    "    cmpq $0x1000, %rax\n"
    "    jb 2f\n"
    "    subq $0x1000, %rcx\n"
    "    testq %rcx, (%rcx)\n"
    "    subq $0x1000, %rax\n"
    "    jmp 1b\n"
    "2:\n"
    "    subq %rax, %rcx\n"
    "    testq %rcx, (%rcx)\n"
    "    popq %rax\n"
    "    popq %rcx\n"
    "    retq\n");
