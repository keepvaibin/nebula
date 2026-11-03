#ifndef RVL_FACE_LIBRARY_SYSTEM_H
#define RVL_FACE_LIBRARY_SYSTEM_H
#include "MiiTypes.h"
#ifdef __cplusplus
extern "C" {
#endif

// Starts the library, ready when it returns (MiiSystem.c).
MiiErrcode MiiInitRes(void* resBuffer, u32 resSize, BOOL deluxeTex);
void MiiExit(void);
BOOL MiiAvailable(void);

#ifdef __cplusplus
}
#endif
#endif
