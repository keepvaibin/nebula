/* The guard's test mods (contract guard.md 4): one fault each. Shared: the fault itself and the facts each logs.
   FAULT 1: a write through a null pointer (access); 2: an illegal instruction (x86 ud2, aarch64 udf #0;
   __builtin_trap is brk on arm64, a SIGTRAP, so it is not used); 3: an integer division by zero (x86 only: arm64's
   division never traps). */
#ifndef GUARD_FACTS_H
#define GUARD_FACTS_H
#include <luma.h>

#if FAULT == 3 && defined(__aarch64__)
#error "FAULT 3 (divide): an aarch64 division by zero gives 0 and never traps; build it for x86-64 only"
#endif

static const LumaCore* g_core;
static const LumaMod* g_mod;

/* "fact <name> <value>" in the game's log, as the harness reads facts. */
static void fact(const char* name, int32_t value) {
    char line[64];
    uint32_t at = 0;
    for (const char* p = "fact "; *p; p++) {
        line[at++] = *p;
    }
    for (const char* p = name; *p && at < 40; p++) {
        line[at++] = *p;
    }
    line[at++] = ' ';
    if (value < 0) {
        line[at++] = '-';
    }
    uint32_t left = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    char digits[12];
    int32_t count = 0;
    do {
        digits[count++] = (char)('0' + left % 10u);
        left /= 10u;
    } while (left != 0u);
    while (count > 0) {
        line[at++] = digits[--count];
    }
    line[at] = 0;
    if (g_core && g_mod) {
        g_core->log(g_mod, line);
    }
}

/* The fault: "entered" before it, "survived" after it (which must never appear: the guard resumes at the boundary,
   not after the faulting instruction). */
__attribute__((unused)) static void fault_here(void) {
    fact("entered", 1);
#if FAULT == 1
    int32_t* volatile nowhere = 0;
    *nowhere = 1;
#elif FAULT == 2
#if defined(__aarch64__)
    __asm__ volatile("udf #0");
#else
    __asm__ volatile("ud2");
#endif
#elif FAULT == 3
    /* The instruction itself: clang makes `1 / x` a branchless select (x for 1 and -1, else 0), with no division. */
    volatile uint32_t zero = 0;
    uint32_t divisor = zero, quotient = 1;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %1" : "+a"(quotient) : "r"(divisor) : "edx", "cc");
    fact("quotient", (int32_t)quotient);
#else
#error "FAULT is 1 (access), 2 (illegal) or 3 (divide)"
#endif
    fact("survived", 1);
}

#endif
