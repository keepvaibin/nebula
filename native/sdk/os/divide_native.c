/* Integer division as the console's CPU does it. PowerPC's divw and divwu never trap: a zero divisor (and divw's
   0x80000000 / -1) gives a quotient of 0, or -1 for a negative dividend with divw, and the game's code goes on with
   it (FlipPanel::endClipped divides by a frame count that is 0 until the bloom animation starts). x86's div and idiv
   trap there, so a trap inside the game module is finished with the console's result: the quotient as above, the
   remainder as the console's code computes it (a - q * b), and execution continues after the instruction. Nothing is
   done on the normal path. Windows delivers the trap as an exception, Linux and macOS as SIGFPE. arm64's sdiv and
   udiv do not trap (x / 0 is 0, which is PowerPC's result but for a negative dividend): sites that divide by zero
   divide with __nebula_divw (nebula_prelude.h) in their source. */
#include <stdint.h>

#include "nebula_sdk.h"

#if defined(__x86_64__) || defined(_M_X64)
/* The faulting instruction: legacy and REX prefixes, then F7 /6 (div) or /7 (idiv), and where its divisor is. */
typedef struct {
    int length;    /* 0: not a 32- or 64-bit div/idiv */
    int wide;      /* REX.W: 64-bit */
    int is_signed; /* idiv */
    int reg;       /* the divisor's register, for mod 3, else -1 */
    int base;      /* memory operand: base register (-1 none), index register (-1 none), scale, displacement */
    int index;
    int scale;
    int64_t disp;
    int rip_relative;
} DivideInsn;

static DivideInsn decode_divide(const uint8_t* ip) {
    DivideInsn insn = {0, 0, 0, -1, -1, -1, 1, 0, 0};
    int i = 0;
    int rex = 0;
    while (i < 4 && (ip[i] == 0x2E || ip[i] == 0x3E || ip[i] == 0x26 || ip[i] == 0x36 || ip[i] == 0x64 ||
                     ip[i] == 0x65)) {
        i++;
    }
    if (ip[i] == 0x66) {
        return insn; /* 16-bit: not something the game compiles to */
    }
    if ((ip[i] & 0xF0) == 0x40) {
        rex = ip[i++];
    }
    if (ip[i] != 0xF7) {
        return insn;
    }
    const uint8_t modrm = ip[i + 1];
    const int op = (modrm >> 3) & 7;
    if (op != 6 && op != 7) {
        return insn;
    }
    int length = i + 2;
    const int mod = modrm >> 6;
    const int rm = modrm & 7;
    if (mod == 3) {
        insn.reg = rm | (rex & 1) << 3;
    } else {
        if (rm == 4) {
            const uint8_t sib = ip[length++];
            insn.scale = 1 << (sib >> 6);
            const int index = (sib >> 3) & 7 | (rex & 2) << 2;
            insn.index = index == 4 ? -1 : index;
            if (mod == 0 && (sib & 7) == 5) {
                insn.disp = *(const int32_t*)(ip + length);
                length += 4;
            } else {
                insn.base = (sib & 7) | (rex & 1) << 3;
            }
        } else if (mod == 0 && rm == 5) {
            insn.rip_relative = 1;
            insn.disp = *(const int32_t*)(ip + length);
            length += 4;
        } else {
            insn.base = rm | (rex & 1) << 3;
        }
        if (mod == 1) {
            insn.disp = (int8_t)ip[length];
            length += 1;
        } else if (mod == 2) {
            insn.disp = *(const int32_t*)(ip + length);
            length += 4;
        }
    }
    insn.length = length;
    insn.wide = (rex & 8) != 0;
    insn.is_signed = op == 7;
    return insn;
}

/* PowerPC's quotient and the remainder its code computes, in place of rax and rdx. */
static void ppc_divide(const DivideInsn* insn, int64_t divisor, uint64_t* rax, uint64_t* rdx) {
    if (insn->wide) {
        const int64_t a = (int64_t)*rax;
        const int64_t q = insn->is_signed && a < 0 ? -1 : 0;
        *rax = (uint64_t)q;
        *rdx = (uint64_t)a - (uint64_t)q * (uint64_t)divisor;
    } else {
        const int32_t a = (int32_t)(uint32_t)*rax;
        const int32_t q = insn->is_signed && a < 0 ? -1 : 0;
        *rax = (uint32_t)q;
        *rdx = (uint32_t)((uint32_t)a - (uint32_t)q * (uint32_t)(int32_t)divisor);
    }
}

/* Sites seen, for the log: a trap costs microseconds, so a site that traps every frame divides with __nebula_divw in
   its source instead. */
static struct {
    uintptr_t offset;
    int count;
} sSites[16];
static int sSiteCount;

static void report_site(uintptr_t offset, int overflow) {
    int site = 0;
    while (site < sSiteCount && sSites[site].offset != offset) {
        site++;
    }
    if (site == sSiteCount && site < 16) {
        sSites[sSiteCount++].offset = offset;
    }
    const int count = site < 16 ? ++sSites[site].count : 0;
    if (count == 1 || count == 600) {
        nebula_log("integer %s at module +0x%llx%s: the console's result taken\n",
                   overflow ? "division overflow" : "division by zero", (unsigned long long)offset,
                   count == 600 ? " 600 times" : "");
    }
}
#endif

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#include <windows.h>

/* Every exception in the process passes here, an AddressSanitizer build's own included: uninstrumented. */
__attribute__((no_sanitize("address", "undefined"))) static LONG CALLBACK divide_handler(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CONTEXT* context = info->ContextRecord;
    uintptr_t base = 0, size = 0;
    nebula_image_range(&base, &size);
    if (context->Rip - base >= size) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DivideInsn insn = decode_divide((const uint8_t*)context->Rip);
    if (insn.length == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    /* A zero divisor, or (the overflow trap) idiv's most negative dividend by -1. */
    uint64_t rax = context->Rax, rdx = context->Rdx;
    ppc_divide(&insn, code == EXCEPTION_INT_OVERFLOW ? -1 : 0, &rax, &rdx);
    context->Rax = rax;
    context->Rdx = rdx;
    report_site(context->Rip - base, code == EXCEPTION_INT_OVERFLOW);
    context->Rip += (DWORD64)insn.length;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void nebula_divide_init(void) {
    AddVectoredExceptionHandler(1, divide_handler);
}
#elif defined(__x86_64__) && (defined(__linux__) || defined(__APPLE__))
#include <signal.h>
#include <string.h>
#include <ucontext.h>

/* The interrupted registers, by x86 number (rax rcx rdx rbx rsp rbp rsi rdi r8-r15), and rip. */
#if defined(__linux__)
static const int kGregs[16] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
                               REG_R8,  REG_R9,  REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15};
static uint64_t* gpr(ucontext_t* uc, int n) {
    return (uint64_t*)&uc->uc_mcontext.gregs[kGregs[n]];
}
static uint64_t* rip(ucontext_t* uc) {
    return (uint64_t*)&uc->uc_mcontext.gregs[REG_RIP];
}
#else
static uint64_t* gpr(ucontext_t* uc, int n) {
    _STRUCT_X86_THREAD_STATE64* ss = &uc->uc_mcontext->__ss;
    uint64_t* const regs[16] = {&ss->__rax, &ss->__rcx, &ss->__rdx, &ss->__rbx, &ss->__rsp, &ss->__rbp,
                                &ss->__rsi, &ss->__rdi, &ss->__r8,  &ss->__r9,  &ss->__r10, &ss->__r11,
                                &ss->__r12, &ss->__r13, &ss->__r14, &ss->__r15};
    return regs[n];
}
static uint64_t* rip(ucontext_t* uc) {
    return &uc->uc_mcontext->__ss.__rip;
}
#endif

__attribute__((no_sanitize("address", "undefined"))) static void fpe_handler(int sig, siginfo_t* info, void* context) {
    ucontext_t* uc = (ucontext_t*)context;
    uintptr_t base = 0, size = 0;
    nebula_image_range(&base, &size);
    const uint64_t pc = *rip(uc);
    const DivideInsn insn = pc - base < size ? decode_divide((const uint8_t*)pc) : (DivideInsn){0};
    if (insn.length == 0) {
        /* Not the game's division: the default action. */
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    /* SIGFPE does not say which: the divisor tells a zero divisor from idiv's most negative dividend by -1. */
    int64_t divisor;
    if (insn.reg >= 0) {
        divisor = (int64_t)*gpr(uc, insn.reg);
    } else {
        uint64_t address = (uint64_t)insn.disp;
        address += insn.rip_relative ? pc + (uint64_t)insn.length : 0;
        address += insn.base >= 0 ? *gpr(uc, insn.base) : 0;
        address += insn.index >= 0 ? *gpr(uc, insn.index) * (uint64_t)insn.scale : 0;
        divisor = insn.wide ? *(const int64_t*)address : *(const int32_t*)address;
    }
    if (!insn.wide) {
        divisor = (int32_t)divisor;
    }
    (void)info;
    ppc_divide(&insn, divisor, gpr(uc, 0), gpr(uc, 2));
    report_site(pc - base, divisor != 0);
    *rip(uc) = pc + (uint64_t)insn.length;
}

void nebula_divide_init(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = fpe_handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGFPE, &action, NULL);
}
#else
/* arm64: no trap (see above). */
void nebula_divide_init(void) {
}
#endif
