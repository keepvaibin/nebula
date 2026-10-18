#ifndef ARALT_H
#define ARALT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "revolution/types.h"

typedef void (*ARCallback)(void);
typedef void (*ARQCallback)(u32);

typedef struct ARQRequest {
    struct ARQRequest* next;
    u32 owner;
    u32 type;
    u32 priority;
    u32 source;
    u32 dest;
    u32 length;
    ARQCallback callback;
} ARQRequest;

#ifdef TARGET_PC
// type 0: main memory (src, a pointer) to ARAM (dst, an offset); type 1: ARAM (src) to main memory (dst).
void ARStartDMA(u32 type, uintptr_t src, uintptr_t dst, u32 length);
#else
void ARStartDMA(u32, u32, u32, u32) NO_INLINE;
#endif
u32 ARAlloc(u32);
u32 ARInit(u32*, u32);
u32 ARGetBaseAddress(void);
u32 ARGetSize(void);

void ARQInit(void);

#ifdef __cplusplus
}
#endif

#endif  // ARALT_H
