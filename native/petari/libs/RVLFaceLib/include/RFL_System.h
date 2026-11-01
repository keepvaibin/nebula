#ifndef RVL_FACE_LIBRARY_SYSTEM_H
#define RVL_FACE_LIBRARY_SYSTEM_H
#include "RFL_Types.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

// Starts the library, ready when it returns (RFL_System.c).
MiiErrcode MiiInitRes(void* resBuffer, u32 resSize, BOOL deluxeTex);
void MiiExit(void);
BOOL MiiAvailable(void);

#ifdef __cplusplus
}
#endif
#endif
