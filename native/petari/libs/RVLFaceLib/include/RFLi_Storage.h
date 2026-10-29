#ifndef RVL_FACE_LIBRARY_INTERNAL_STORAGE_H
#define RVL_FACE_LIBRARY_INTERNAL_STORAGE_H
#include "RFLi_Types.h"
#include <revolution/os.h>
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

// The library's database and parts archive through the host's Mii stores (nebula_game_api.h mii_storage). Each
// operation finishes after the call that starts it returns, from the file's alarm, as the console's NAND requests
// did: the library's callbacks chain the next step from there.
typedef struct RFLiAccessInfo {
    RFLiCallback callback;
    RFLiAsyncTag pending; // the operation the alarm finishes
    void* data;
    u32 size;
    s32 offset;
    u32* lengthOut;
    u8 openMode;
    u8 opened;
    OSAlarm alarm;
    RFLiFileType alarmData;
} RFLiAccessInfo;

void RFLiInitAccessInfo(void);
void RFLiExitAccessInfo(void);
BOOL RFLiIsWorking(void);
void RFLiStartWorking(void);
void RFLiEndWorkingReason(RFLErrcode err, s32 reason);
void RFLiEndWorking(RFLErrcode err);
RFLErrcode RFLiOpenAsync(RFLiFileType type, u8 openMode, RFLiCallback callback);
RFLErrcode RFLiReadAsync(RFLiFileType type, void* dst, u32 size, RFLiCallback callback, s32 offset);
RFLErrcode RFLiWriteAsync(RFLiFileType type, const void* src, u32 size, RFLiCallback callback, s32 offset);
RFLErrcode RFLiCloseAsync(RFLiFileType type, RFLiCallback callback);
RFLErrcode RFLiGetLengthAsync(RFLiFileType type, u32* out, RFLiCallback callback);

#ifdef __cplusplus
}
#endif
#endif
