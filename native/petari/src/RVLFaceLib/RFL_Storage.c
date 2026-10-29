#include "RVLFaceLibInternal.h"
#include <nebula_game_api.h>
#include <string.h>

const NebulaHostServices* nebula_host(void);

// The host's store for each of the library's files.
static const u32 scStores[RFLiFileType_Max] = {
    NEBULA_MII_DATABASE, // RFLiFileType_Database
    NEBULA_MII_PARTS     // RFLiFileType_Resource
};

static s32 store_(RFLiFileType type, u32 op, u32 offset, void* data, u32 size) {
    const NebulaHostServices* host = nebula_host();
    return host->mii_storage(host->user, scStores[type], op, offset, data, size);
}

void RFLiInitAccessInfo(void) {
    u16 i;

    for (i = 0; i < RFLiFileType_Max; i++) {
        RFLiAccessInfo* info = RFLiGetAccInfo((RFLiFileType)i);
        memset(info, 0, sizeof(RFLiAccessInfo));
        OSCreateAlarm(&info->alarm);
    }
}

void RFLiExitAccessInfo(void) {
    u16 i;

    for (i = 0; i < RFLiFileType_Max; i++) {
        OSCancelAlarm(&RFLiGetAccInfo((RFLiFileType)i)->alarm);
    }
}

BOOL RFLiIsWorking(void) {
    return RFLiGetWorking();
}

void RFLiStartWorking(void) {
    BOOL enabled = OSDisableInterrupts();

    RFLiSetWorking(TRUE);
    RFLiGetManager()->lastErrCode = RFLErrcode_Busy;

    OSRestoreInterrupts(enabled);
}

// A close keeps the result of the operations before it (a close after a failed read still reports the failure).
static void startWorkingClose_(void) {
    BOOL enabled = OSDisableInterrupts();

    RFLiSetWorking(TRUE);
    RFLiGetManager()->beforeCloseErr = RFLiGetManager()->lastErrCode;
    RFLiGetManager()->beforeCloseReason = RFLiGetManager()->lastReason;
    RFLiGetManager()->lastErrCode = RFLErrcode_Busy;

    OSRestoreInterrupts(enabled);
}

static void endWorkingClose_(void) {
    BOOL enabled = OSDisableInterrupts();

    RFLiSetWorking(FALSE);
    RFLiGetManager()->lastErrCode = RFLiGetManager()->beforeCloseErr;
    RFLiGetManager()->lastReason = RFLiGetManager()->beforeCloseReason;

    OSRestoreInterrupts(enabled);
}

void RFLiEndWorkingReason(RFLErrcode err, s32 reason) {
    BOOL enabled;

    switch (RFLiGetManager()->lastErrCode) {
    case RFLErrcode_Busy:
    case RFLErrcode_Success:
        enabled = OSDisableInterrupts();

        RFLiSetWorking(FALSE);
        RFLiGetManager()->lastErrCode = err;
        RFLiGetManager()->lastReason = reason;

        OSRestoreInterrupts(enabled);
        break;
    default:
        break;
    }
}

void RFLiEndWorking(RFLErrcode err) {
    RFLiEndWorkingReason(err, 0);
}

// The pending operation, done through the host's store; then the library's callback.
static void finish_(OSAlarm* alarm, OSContext* ctx) {
    const RFLiFileType type = *(const RFLiFileType*)OSGetAlarmUserData(alarm);
    RFLiAccessInfo* info;
    s32 result;
    (void)ctx;

    if (!RFLAvailable()) {
        return;
    }
    info = RFLiGetAccInfo(type);

    switch (info->pending) {
    case RFLiAsyncTag_OpenAsync:
        // Opening for reading needs the file; opening to write creates an empty one.
        result = info->openMode == 1 ? store_(type, NEBULA_MII_LENGTH, 0, NULL, 0)
                                     : store_(type, NEBULA_MII_CREATE, 0, NULL, 0);
        info->opened = result >= 0;
        if (result >= 0) {
            RFLiEndWorking(RFLErrcode_Success);
        } else {
            RFLiEndWorkingReason(result == NEBULA_MII_MISSING ? RFLErrcode_NANDCommandfail : RFLErrcode_Fatal, result);
        }
        break;
    case RFLiAsyncTag_ReadAsync:
        result = store_(type, NEBULA_MII_READ, (u32)info->offset, info->data, info->size);
        if (result == (s32)info->size) {
            RFLiEndWorking(RFLErrcode_Success);
        } else {
            memset(info->data, 0, info->size);
            if (result >= 0) {
                RFLiEndWorking(RFLErrcode_Loadfail);
            } else {
                RFLiEndWorkingReason(RFLErrcode_NANDCommandfail, result);
            }
        }
        break;
    case RFLiAsyncTag_WriteAsync:
        result = store_(type, NEBULA_MII_WRITE, (u32)info->offset, info->data, info->size);
        if (result == (s32)info->size) {
            RFLiEndWorking(RFLErrcode_Success);
        } else if (result >= 0) {
            RFLiEndWorking(RFLErrcode_Savefail);
        } else {
            RFLiEndWorkingReason(RFLErrcode_NANDCommandfail, result);
        }
        break;
    case RFLiAsyncTag_GetLengthAsync:
        result = store_(type, NEBULA_MII_LENGTH, 0, NULL, 0);
        if (result >= 0) {
            *info->lengthOut = (u32)result;
            RFLiEndWorking(RFLErrcode_Success);
        } else {
            RFLiEndWorkingReason(RFLErrcode_NANDCommandfail, result);
        }
        break;
    case RFLiAsyncTag_CloseAsync:
    default:
        info->opened = FALSE;
        endWorkingClose_();
        break;
    }

    if (info->callback != NULL) {
        info->callback();
    }
}

static RFLErrcode start_(RFLiFileType type, RFLiAsyncTag op, RFLiCallback callback) {
    RFLiAccessInfo* info = RFLiGetAccInfo(type);

    info->callback = callback;
    info->pending = op;
    info->alarmData = type;
    OSCancelAlarm(&info->alarm);
    OSSetAlarmUserData(&info->alarm, &info->alarmData);
    OSSetAlarm(&info->alarm, 0, finish_);
    return RFLGetAsyncStatus();
}

RFLErrcode RFLiOpenAsync(RFLiFileType type, u8 openMode, RFLiCallback callback) {
    RFLiStartWorking();
    RFLiGetAccInfo(type)->openMode = openMode;
    return start_(type, RFLiAsyncTag_OpenAsync, callback);
}

RFLErrcode RFLiReadAsync(RFLiFileType type, void* dst, u32 size, RFLiCallback callback, s32 offset) {
    RFLiAccessInfo* info = RFLiGetAccInfo(type);

    RFLiStartWorking();
    info->data = dst;
    info->size = size;
    info->offset = offset;
    return start_(type, RFLiAsyncTag_ReadAsync, callback);
}

RFLErrcode RFLiWriteAsync(RFLiFileType type, const void* src, u32 size, RFLiCallback callback, s32 offset) {
    RFLiAccessInfo* info = RFLiGetAccInfo(type);

    RFLiStartWorking();
    info->data = (void*)src;
    info->size = size;
    info->offset = offset;
    return start_(type, RFLiAsyncTag_WriteAsync, callback);
}

RFLErrcode RFLiCloseAsync(RFLiFileType type, RFLiCallback callback) {
    startWorkingClose_();
    return start_(type, RFLiAsyncTag_CloseAsync, callback);
}

RFLErrcode RFLiGetLengthAsync(RFLiFileType type, u32* out, RFLiCallback callback) {
    RFLiStartWorking();
    RFLiGetAccInfo(type)->lengthOut = out;
    return start_(type, RFLiAsyncTag_GetLengthAsync, callback);
}

s32 RFLiReadParts(void* dst, u32 size, u32 offset) {
    return store_(RFLiFileType_Resource, NEBULA_MII_READ, offset, dst, size);
}
