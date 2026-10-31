#ifndef RVL_FACE_LIBRARY_SYSTEM_H
#define RVL_FACE_LIBRARY_SYSTEM_H
#include "RFL_Types.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

u32 RFLGetWorkSize(BOOL deluxeTex);
// Starts the library, ready when it returns (RFL_System.c).
RFLErrcode RFLInitRes(void* workBuffer, void* resBuffer, u32 resSize,
                      BOOL deluxeTex);
void RFLExit(void);
BOOL RFLAvailable(void);

#ifdef __cplusplus
}
#endif
#endif
