#ifndef OSCONTEXT_H
#define OSCONTEXT_H

#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OSContext {
    u32 gpr[32];
    u32 cr;
    u32 lr;
    u32 ctr;
    u32 xer;
    f64 fpr[32];
    u32 fpscr_pad;
    u32 fpscr;
    u32 srr0;
    u32 srr1;
    u16 mode;
    u16 state;
    u32 gqr[8];
    u32 psf_pad;
    f64 psf[32];
    // The thread's native stack pointer while it is switched out (the platform layer's stack switch).
    void* nativeSp;
} OSContext;

void OSSetCurrentContext(OSContext *);
OSContext* OSGetCurrentContext(void);
u32 OSSaveContext(OSContext *);
void OSLoadContext(OSContext *);
uintptr_t OSGetStackPointer(void);
int OSSwitchFiber(u32, u32);
int OSSwitchFiberEx(u32, u32, u32, u32, u32, u32);
void OSClearContext(OSContext *);
void OSInitContext(OSContext *, u32, u32);
void OSDumpContext(OSContext *);

void OSLoadFPUContext(OSContext *);
void OSSaveFPUContext(OSContext *);

void __OSContextInit(void);

void OSFillFPUContext(OSContext* context);

#ifdef __cplusplus
}
#endif

#endif // OSCONTEXT_H
