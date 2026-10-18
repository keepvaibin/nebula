/* Integer division as the console's CPU does it. PowerPC's divw and divwu never trap: a zero divisor (and divw's
   0x80000000 / -1) gives a quotient of 0, or -1 for a negative dividend with divw, and the game's code goes on with
   it (FlipPanel::endClipped divides by a frame count that is 0 until the bloom animation starts). x86's div and idiv
   trap there, so a trap inside the game module is finished with the console's result: the quotient as above, the
   remainder as the console's code computes it (a - q * b), and execution continues after the instruction. Nothing is
   done on the normal path. */
#include <stdint.h>

#include "nebula_sdk.h"

#if defined(_WIN32) && defined(_M_X64) || defined(_WIN32) && defined(__x86_64__)
#include <windows.h>

/* Sites seen, for the log: a trap costs microseconds, so a site that traps every frame is fixed in its source instead. */
static struct {
    uintptr_t offset;
    volatile LONG count;
} sSites[16];
static volatile LONG sSiteCount;

/* The faulting instruction: legacy and REX prefixes, then F7 /6 (div) or /7 (idiv). Returns its length, or 0. */
static int decode_divide(const uint8_t* ip, int* wide, int* is_signed) {
    int i = 0;
    int rex = 0;
    while (i < 4 && (ip[i] == 0x2E || ip[i] == 0x3E || ip[i] == 0x26 || ip[i] == 0x36 || ip[i] == 0x64 ||
                     ip[i] == 0x65)) {
        i++;
    }
    if (ip[i] == 0x66) {
        return 0; /* 16-bit: not something the game compiles to */
    }
    if ((ip[i] & 0xF0) == 0x40) {
        rex = ip[i++];
    }
    if (ip[i] != 0xF7) {
        return 0;
    }
    const uint8_t modrm = ip[i + 1];
    const int reg = (modrm >> 3) & 7;
    if (reg != 6 && reg != 7) {
        return 0;
    }
    int length = i + 2;
    const int mod = modrm >> 6;
    const int rm = modrm & 7;
    if (mod != 3) {
        if (rm == 4) {
            const uint8_t sib = ip[length++];
            if (mod == 0 && (sib & 7) == 5) {
                length += 4;
            }
        } else if (mod == 0 && rm == 5) {
            length += 4; /* RIP-relative */
        }
        length += mod == 1 ? 1 : mod == 2 ? 4 : 0;
    }
    *wide = (rex & 8) != 0;
    *is_signed = reg == 7;
    return length;
}

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
    int wide = 0, is_signed = 0;
    const int length = decode_divide((const uint8_t*)context->Rip, &wide, &is_signed);
    if (length == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    /* A zero divisor, or (the overflow trap) idiv's most negative dividend by -1. */
    const int64_t divisor = code == EXCEPTION_INT_OVERFLOW ? -1 : 0;
    if (wide) {
        const int64_t a = (int64_t)context->Rax;
        const int64_t q = is_signed && a < 0 ? -1 : 0;
        context->Rax = (DWORD64)q;
        context->Rdx = (DWORD64)((uint64_t)a - (uint64_t)q * (uint64_t)divisor);
    } else {
        const int32_t a = (int32_t)(uint32_t)context->Rax;
        const int32_t q = is_signed && a < 0 ? -1 : 0;
        context->Rax = (uint32_t)q;
        context->Rdx = (uint32_t)((uint32_t)a - (uint32_t)q * (uint32_t)(int32_t)divisor);
    }
    const uintptr_t offset = context->Rip - base;
    LONG site = 0;
    const LONG sites = sSiteCount < 16 ? sSiteCount : 16;
    while (site < sites && sSites[site].offset != offset) {
        site++;
    }
    if (site == sites && site < 16) {
        site = InterlockedIncrement(&sSiteCount) - 1;
        if (site < 16) {
            sSites[site].offset = offset;
        }
    }
    const LONG count = site < 16 ? InterlockedIncrement(&sSites[site].count) : 0;
    if (count == 1 || count == 600) {
        nebula_log("integer %s at module +0x%llx%s: the console's result taken\n",
                   code == EXCEPTION_INT_OVERFLOW ? "division overflow" : "division by zero",
                   (unsigned long long)offset, count == 600 ? " 600 times" : "");
    }
    context->Rip += (DWORD64)length;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void nebula_divide_init(void) {
    AddVectoredExceptionHandler(1, divide_handler);
}
#else
/* POSIX hosts: a SIGFPE handler with the same decoding (P7.1). */
void nebula_divide_init(void) {
}
#endif
