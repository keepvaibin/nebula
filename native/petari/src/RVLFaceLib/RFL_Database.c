#include "RVLFaceLibInternal.h"

#define ASYNC_CRC_STEP 0x1400

void RFLiInitDatabase(MEMiHeapHead* heap) {
    RFLiDBManager* mgr = RFLiGetDBManager();

    mgr->database =
        (RFLiDatabase*)MEMAllocFromExpHeapEx(heap, sizeof(RFLiDatabase), 32);
    mgr->saveCb = NULL;
    mgr->formatCb = NULL;
    mgr->bootloadCb = NULL;

    RFLiClearDBBuffer();
}

static void initBrokenDatabase_(void) {
    RFLiClearDBBuffer();
}

static void bootloadCheckCRCCb_(uintptr_t crc) {
    RFLiDBManager* mgr = RFLiGetDBManager();

    if (crc != 0) {
        RFLiGetManager()->lastErrCode = RFLErrcode_Loadfail;
        RFLiSetFileBroken(RFLiFileBrokenType_DBBroken);
    } else {
        RFLiGetManager()->lastErrCode = RFLErrcode_Success;
    }

    RFLiCloseAsync(RFLiFileType_Database, mgr->bootloadCb);
}

static void bootloadDBcallback_(void) {
    RFLiDBManager* mgr = RFLiGetDBManager();

    switch (RFLGetAsyncStatus()) {
    case RFLErrcode_Success:
        RFLiCheckHeaderCRCAsync(bootloadCheckCRCCb_);
        break;
    case RFLErrcode_NANDCommandfail:
        switch (RFLiGetLastReason()) {
        case NAND_RESULT_CORRUPT:
            RFLiSetFileBroken(RFLiFileBrokenType_Corrupt);
            break;
        case NAND_RESULT_AUTHENTICATION:
        case NAND_RESULT_ECC_CRIT:
        default:
            RFLiGetManager()->lastErrCode = RFLErrcode_Loadfail;
            RFLiSetFileBroken(RFLiFileBrokenType_DBBroken);
            break;
        }
        RFLiCloseAsync(RFLiFileType_Database, mgr->bootloadCb);
        break;
    case RFLErrcode_NotAvailable:
    default:
        RFLiSetFileBroken(RFLiFileBrokenType_DBBroken);
        RFLiCloseAsync(RFLiFileType_Database, mgr->bootloadCb);
        break;
    }
}

static void bootloadDBopencallback_(void) {
    RFLiDBManager* mgr = RFLiGetDBManager();

    switch (RFLGetAsyncStatus()) {
    case RFLErrcode_Success:
        switch (RFLiReadAsync(RFLiFileType_Database, mgr->database,
                              sizeof(RFLiDatabase), bootloadDBcallback_, 0)) {
        case RFLErrcode_Busy:
        case RFLErrcode_Success:
            break;
        default:
            if (mgr->bootloadCb != NULL) {
                mgr->bootloadCb();
            }
            break;
        }
        break;
    case RFLErrcode_NANDCommandfail:
        switch (RFLiGetLastReason()) {
        case NAND_RESULT_NOEXISTS:
            RFLiSetFileBroken(RFLiFileBrokenType_DBNotFound);
            RFLiGetManager()->lastErrCode = RFLErrcode_Success;
            if (mgr->bootloadCb != NULL) {
                mgr->bootloadCb();
            }
            break;
        case NAND_RESULT_CORRUPT:
            RFLiSetFileBroken(RFLiFileBrokenType_Corrupt);
            break;
        case NAND_RESULT_AUTHENTICATION:
        case NAND_RESULT_ECC_CRIT:
            RFLiSetFileBroken(RFLiFileBrokenType_DBBroken);
            if (mgr->bootloadCb != NULL) {
                mgr->bootloadCb();
            }
            break;
        default:
            break;
        }
        break;
    case RFLErrcode_NotAvailable:
    default:
        break;
    }
}

RFLErrcode RFLiBootLoadDatabaseAsync(RFLiCallback callback) {
    RFLiDBManager* mgr;

    if (!RFLAvailable()) {
        return RFLErrcode_Fatal;
    }

    if (RFLiIsWorking()) {
        return RFLErrcode_Fatal;
    }

    mgr = RFLiGetDBManager();
    if (mgr == NULL) {
        return RFLErrcode_Fatal;
    }

    mgr->bootloadCb = callback;

    switch (RFLiOpenAsync(RFLiFileType_Database, 1,
                          bootloadDBopencallback_)) {
    case RFLErrcode_NANDCommandfail:
        if (RFLiGetLastReason() == NAND_RESULT_NOEXISTS) {
            initBrokenDatabase_();
            RFLiGetManager()->lastErrCode = RFLErrcode_Success;

            if (mgr->bootloadCb != NULL) {
                mgr->bootloadCb();
            }
        }
        break;
    case RFLErrcode_Success:
    case RFLErrcode_Busy:
    default:
        break;
    }

    return RFLGetAsyncStatus();
}

static void saveDBcallback_(void) {
    RFLiCloseAsync(RFLiFileType_Database, RFLiGetDBManager()->saveCb);
}

static RFLErrcode saveCore_(RFLiCallback callback) {
    return RFLiWriteAsync(RFLiFileType_Database, RFLiGetDBManager()->database,
                          sizeof(RFLiDatabase), callback, 0);
}

static void saveDBopencallback_(void) {
    (void)RFLiGetDBManager();

    if (RFLGetAsyncStatus() == RFLErrcode_Success) {
        switch (saveCore_(saveDBcallback_)) {
        case RFLErrcode_Success:
        case RFLErrcode_Busy:
            break;
        default:
            RFLiCloseAsync(RFLiFileType_Database, NULL);
            break;
        }
    }
}

static void saveDBmultiopencallback_(void) {
    (void)RFLiGetDBManager();

    if (RFLGetAsyncStatus() == RFLErrcode_Success) {
        switch (saveCore_(RFLiGetDBManager()->saveCb)) {
        case RFLErrcode_Success:
        case RFLErrcode_Busy:
            break;
        default:
            RFLiCloseAsync(RFLiFileType_Database, NULL);
            break;
        }
    }
}

static void saveDatabase_(uintptr_t arg) {
    switch (RFLiGetDBManager()->saveType) {
    case RFLiOpenType_Multi:
        RFLiOpenAsync(RFLiFileType_Database, 2,
                      saveDBmultiopencallback_);
        break;
    case RFLiOpenType_Single:
        RFLiOpenAsync(RFLiFileType_Database, 2,
                      saveDBopencallback_);
        break;
    case RFLiOpenType_Opened:
        saveCore_(RFLiGetDBManager()->saveCb);
        break;
    default:
        break;
    }
}

static RFLErrcode RFLiSaveDatabaseAsync(RFLiCallback callback) {
    RFLiDBManager* mgr;

    if (!RFLAvailable()) {
        return RFLErrcode_NotAvailable;
    }

    mgr = RFLiGetDBManager();
    if (mgr == NULL || !RFLiDBIsLoaded()) {
        return RFLErrcode_NotAvailable;
    }

    mgr->saveCb = callback;
    mgr->saveType = RFLiOpenType_Opened;

    RFLiCreateHeaderCRCAsync(saveDatabase_);
    return RFLGetAsyncStatus();
}

RFLiCharData* RFLiGetCharData(u16 index) {
    RFLiDBManager* mgr;
    RFLiCharData* data;
    RFLiCharInfo info;

    if (index >= RFL_DB_CHAR_MAX) {
        return NULL;
    }

    if (!RFLAvailable()) {
        return NULL;
    }

    mgr = RFLiGetDBManager();
    if (mgr == NULL || !RFLiDBIsLoaded()) {
        return NULL;
    }

    data = &mgr->database->rawData[index];

    if (!RFLiIsValidID(&data->createID)) {
        return NULL;
    }

    RFLiConvertRaw2Info(data, &info);
    if (!RFLiCheckValidInfo(&info)) {
        return NULL;
    }

    if (!RFLiIsValidOnNAND(&info)) {
        return NULL;
    }

    return data;
}

static void convertRaw2InfoCore_(const RFLiCharData* data, RFLiCharInfo* info) {
    info->faceline.type = RFLI_GET(RFLiCharData, data, faceType);
    info->faceline.color = RFLI_GET(RFLiCharData, data, faceColor);
    info->faceline.texture = RFLI_GET(RFLiCharData, data, faceTex);

    info->hair.type = RFLI_GET(RFLiCharData, data, hairType);
    info->hair.color = RFLI_GET(RFLiCharData, data, hairColor);
    info->hair.flip = RFLI_GET(RFLiCharData, data, hairFlip);

    info->eye.type = RFLI_GET(RFLiCharData, data, eyeType);
    info->eye.color = RFLI_GET(RFLiCharData, data, eyeColor);
    info->eye.scale = RFLI_GET(RFLiCharData, data, eyeScale);
    info->eye.rotate = RFLI_GET(RFLiCharData, data, eyeRotate);
    info->eye.x = RFLI_GET(RFLiCharData, data, eyeX);
    info->eye.y = RFLI_GET(RFLiCharData, data, eyeY);

    info->eyebrow.type = RFLI_GET(RFLiCharData, data, eyebrowType);
    info->eyebrow.color = RFLI_GET(RFLiCharData, data, eyebrowColor);
    info->eyebrow.scale = RFLI_GET(RFLiCharData, data, eyebrowScale);
    info->eyebrow.rotate = RFLI_GET(RFLiCharData, data, eyebrowRotate);
    info->eyebrow.x = RFLI_GET(RFLiCharData, data, eyebrowX);
    info->eyebrow.y = RFLI_GET(RFLiCharData, data, eyebrowY);

    info->nose.type = RFLI_GET(RFLiCharData, data, noseType);
    info->nose.scale = RFLI_GET(RFLiCharData, data, noseScale);
    info->nose.y = RFLI_GET(RFLiCharData, data, noseY);

    info->mouth.type = RFLI_GET(RFLiCharData, data, mouthType);
    info->mouth.color = RFLI_GET(RFLiCharData, data, mouthColor);
    info->mouth.scale = RFLI_GET(RFLiCharData, data, mouthScale);
    info->mouth.y = RFLI_GET(RFLiCharData, data, mouthY);

    info->beard.mustache = RFLI_GET(RFLiCharData, data, mustacheType);
    info->beard.type = RFLI_GET(RFLiCharData, data, beardType);
    info->beard.color = RFLI_GET(RFLiCharData, data, beardColor);
    info->beard.scale = RFLI_GET(RFLiCharData, data, beardScale);
    info->beard.y = RFLI_GET(RFLiCharData, data, beardY);

    info->glass.type = RFLI_GET(RFLiCharData, data, glassType);
    info->glass.color = RFLI_GET(RFLiCharData, data, glassColor);
    info->glass.scale = RFLI_GET(RFLiCharData, data, glassScale);
    info->glass.y = RFLI_GET(RFLiCharData, data, glassY);

    info->mole.type = RFLI_GET(RFLiCharData, data, moleType);
    info->mole.scale = RFLI_GET(RFLiCharData, data, moleScale);
    info->mole.x = RFLI_GET(RFLiCharData, data, moleX);
    info->mole.y = RFLI_GET(RFLiCharData, data, moleY);

    info->body.height = data->height;
    info->body.build = data->build;

    RFLI_COPY_NAME(info->personal.name, data->name, RFL_NAME_LEN);
    info->personal.name[RFL_NAME_LEN] = L'\0';

    memcpy(&info->createID, &data->createID, sizeof(RFLCreateID));

    info->personal.sex = RFLI_GET(RFLiCharData, data, sex);
    info->personal.bmonth = RFLI_GET(RFLiCharData, data, birthMonth);
    info->personal.bday = RFLI_GET(RFLiCharData, data, birthDay);
    info->personal.color = RFLI_GET(RFLiCharData, data, favoriteColor);
    info->personal.favorite = RFLI_GET(RFLiCharData, data, favorite);
    info->personal.localOnly = RFLI_GET(RFLiCharData, data, localonly);
}

void RFLiConvertRaw2Info(const RFLiCharData* data, RFLiCharInfo* out) {
    convertRaw2InfoCore_(data, out);

    RFLI_COPY_NAME(out->personal.creator, data->creatorName, RFL_CREATOR_LEN);
    out->personal.creator[RFL_CREATOR_LEN] = '\0';
}

void RFLiConvertHRaw2Info(const RFLiHiddenCharData* data, RFLiCharInfo* out) {
    convertRaw2InfoCore_((RFLiCharData*)data, out);

    memset(out->personal.creator, 0, RFL_CREATOR_LEN * sizeof(wchar_t));
    out->personal.creator[RFL_CREATOR_LEN] = '\0';

    out->personal.bmonth = 0;
    out->personal.bday = 0;
}

static void convertInfo2RawCore_(const RFLiCharInfo* info, RFLiCharData* data) {
    RFLI_SET(RFLiCharData, data, faceType, info->faceline.type);
    RFLI_SET(RFLiCharData, data, faceColor, info->faceline.color);
    RFLI_SET(RFLiCharData, data, faceTex, info->faceline.texture);

    RFLI_SET(RFLiCharData, data, hairType, info->hair.type);
    RFLI_SET(RFLiCharData, data, hairColor, info->hair.color);
    RFLI_SET(RFLiCharData, data, hairFlip, info->hair.flip);

    RFLI_SET(RFLiCharData, data, eyeType, info->eye.type);
    RFLI_SET(RFLiCharData, data, eyeColor, info->eye.color);
    RFLI_SET(RFLiCharData, data, eyeScale, info->eye.scale);
    RFLI_SET(RFLiCharData, data, eyeRotate, info->eye.rotate);
    RFLI_SET(RFLiCharData, data, eyeX, info->eye.x);
    RFLI_SET(RFLiCharData, data, eyeY, info->eye.y);

    RFLI_SET(RFLiCharData, data, eyebrowType, info->eyebrow.type);
    RFLI_SET(RFLiCharData, data, eyebrowColor, info->eyebrow.color);
    RFLI_SET(RFLiCharData, data, eyebrowScale, info->eyebrow.scale);
    RFLI_SET(RFLiCharData, data, eyebrowRotate, info->eyebrow.rotate);
    RFLI_SET(RFLiCharData, data, eyebrowX, info->eyebrow.x);
    RFLI_SET(RFLiCharData, data, eyebrowY, info->eyebrow.y);

    RFLI_SET(RFLiCharData, data, noseType, info->nose.type);
    RFLI_SET(RFLiCharData, data, noseScale, info->nose.scale);
    RFLI_SET(RFLiCharData, data, noseY, info->nose.y);

    RFLI_SET(RFLiCharData, data, mouthType, info->mouth.type);
    RFLI_SET(RFLiCharData, data, mouthColor, info->mouth.color);
    RFLI_SET(RFLiCharData, data, mouthScale, info->mouth.scale);
    RFLI_SET(RFLiCharData, data, mouthY, info->mouth.y);

    RFLI_SET(RFLiCharData, data, mustacheType, info->beard.mustache);
    RFLI_SET(RFLiCharData, data, beardType, info->beard.type);
    RFLI_SET(RFLiCharData, data, beardColor, info->beard.color);
    RFLI_SET(RFLiCharData, data, beardScale, info->beard.scale);
    RFLI_SET(RFLiCharData, data, beardY, info->beard.y);

    RFLI_SET(RFLiCharData, data, glassType, info->glass.type);
    RFLI_SET(RFLiCharData, data, glassColor, info->glass.color);
    RFLI_SET(RFLiCharData, data, glassScale, info->glass.scale);
    RFLI_SET(RFLiCharData, data, glassY, info->glass.y);

    RFLI_SET(RFLiCharData, data, moleType, info->mole.type);
    RFLI_SET(RFLiCharData, data, moleScale, info->mole.scale);
    RFLI_SET(RFLiCharData, data, moleX, info->mole.x);
    RFLI_SET(RFLiCharData, data, moleY, info->mole.y);

    data->height = info->body.height;
    data->build = info->body.build;

    RFLI_COPY_NAME(data->name, info->personal.name, RFL_NAME_LEN);
    memcpy(&data->createID, &info->createID, sizeof(RFLCreateID));

    RFLI_SET(RFLiCharData, data, sex, info->personal.sex);
    RFLI_SET(RFLiCharData, data, birthMonth, info->personal.bmonth);
    RFLI_SET(RFLiCharData, data, birthDay, info->personal.bday);
    RFLI_SET(RFLiCharData, data, favoriteColor, info->personal.color);
    RFLI_SET(RFLiCharData, data, favorite, info->personal.favorite);
    RFLI_SET(RFLiCharData, data, localonly, info->personal.localOnly);
}

static void RFLiConvertInfo2Raw(const RFLiCharInfo* info, RFLiCharData* out) {
    convertInfo2RawCore_(info, out);
    RFLI_COPY_NAME(out->creatorName, info->personal.creator, RFL_CREATOR_LEN);
}

static void RFLiConvertInfo2HRaw(const RFLiCharInfo* info, RFLiHiddenCharData* out) {
    convertInfo2RawCore_(info, (RFLiCharData*)out);
    RFLI_SET(RFLiHiddenCharData, out, birthPadding, 0);
}

static void RFLiConvertRaw2HRaw(const RFLiCharData* data, RFLiHiddenCharData* out) {
    memset(out, 0, sizeof(RFLiHiddenCharData));
    // Copy everything that also exists in hidden (just no creator name)
    memcpy(out, data, sizeof(RFLiCharData) - RFL_CREATOR_LEN * sizeof(wchar_t));
    RFLI_SET(RFLiHiddenCharData, out, birthPadding, 0);
}

static RFLErrcode RFLiGetCharRawData(RFLiCharData* out, u16 index) {
    RFLiCharData* data;

    if (out == NULL) {
        return RFLErrcode_WrongParam;
    }

    if (index >= RFL_DB_CHAR_MAX) {
        return RFLErrcode_WrongParam;
    }

    data = RFLiGetCharData(index);
    if (data == NULL) {
        return RFLErrcode_Broken;
    }

    memcpy(out, data, sizeof(RFLiCharData));
    return RFLErrcode_Success;
}

RFLErrcode RFLiGetCharInfo(RFLiCharInfo* out, u16 index) {
    RFLiCharData* data;

    if (out == NULL) {
        return RFLErrcode_WrongParam;
    }

    if (index >= RFL_DB_CHAR_MAX) {
        return RFLErrcode_WrongParam;
    }

    data = RFLiGetCharData(index);
    if (data == NULL) {
        // Shouldn't this return RFLErrcode_Broken?
        return RFLErrcode_NotAvailable;
    }

    RFLiConvertRaw2Info(data, out);
    if (!RFLiCheckValidInfo(out)) {
        return RFLErrcode_Broken;
    }

    return RFLErrcode_Success;
}

BOOL RFLIsAvailableOfficialData(u16 index) {
    if (index >= RFL_DB_CHAR_MAX) {
        return FALSE;
    }

    return RFLiGetCharData(index) != NULL;
}

static void RFLiSetTemporaryID(RFLiCharInfo* info) {
    u32* dst = (u32*)&info->createID.data;
    // The create ID's words are big-endian (its flag bits are in the first byte).
    RFLI_ST32(dst[0], RFLi_CREATE_ID_MASK_TEMPORARY);
    dst[1] = 0;
}

BOOL RFLiIsSameID(const RFLCreateID* id1, const RFLCreateID* id2) {
    const u32* ptr1 = (u32*)id1->data;
    const u32* ptr2 = (u32*)id2->data;

    if (id1 == NULL) {
        return FALSE;
    }

    if (id2 == NULL) {
        return FALSE;
    }

    if (!RFLiIsValidID(id1)) {
        return FALSE;
    }

    if (!RFLiIsValidID(id2)) {
        return FALSE;
    }

    if (RFLiIsTemporaryID(id1)) {
        return FALSE;
    }

    if (RFLiIsTemporaryID(id2)) {
        return FALSE;
    }

    if (ptr1[0] != ptr2[0]) {
        return FALSE;
    }

    if (ptr1[1] != ptr2[1]) {
        return FALSE;
    }

    return TRUE;
}

BOOL RFLiIsValidID(const RFLCreateID* id) {
    const u32* ptr = (u32*)id->data;

    if (id == NULL) {
        return FALSE;
    }

    return ptr[0] != 0 || ptr[1] != 0;
}

BOOL RFLiIsSpecialID(const RFLCreateID* id) {
    const u32* ptr = (u32*)id->data;

    if (id == NULL) {
        return FALSE;
    }

    if (!RFLiIsValidID(id)) {
        return FALSE;
    }

    if (RFLiIsTemporaryID(id)) {
        return FALSE;
    }

    return (RFLI_LD32(*ptr) & RFLi_CREATE_ID_MASK_NOT_SPECIAL) == 0 ? TRUE : FALSE;
}

BOOL RFLiIsTemporaryID(const RFLCreateID* id) {
    const u32* ptr = (u32*)id->data;

    if (id == NULL) {
        return FALSE;
    }

    if (!RFLiIsValidID(id)) {
        return FALSE;
    }

    return (RFLI_LD32(*ptr) & RFLi_CREATE_ID_MASK_TEMPORARY) != 0 ? TRUE : FALSE;
}

BOOL RFLSearchOfficialData(const RFLCreateID* id, u16* index) {
    u16 i;
    BOOL success;
    RFLiDBManager* mgr;
    RFLiCharData* head;

    success = FALSE;

    if (id == NULL) {
        return FALSE;
    }

    if (!RFLAvailable()) {
        return FALSE;
    }

    mgr = RFLiGetDBManager();
    if (mgr == NULL) {
        return FALSE;
    }

    head = mgr->database->rawData;
    for (i = 0; i < RFL_DB_CHAR_MAX; i++) {
        if (RFLIsAvailableOfficialData(i) &&
            RFLiIsSameID(&head[i].createID, id)) {
            success = TRUE;
            break;
        }
    }

    if (success == TRUE && index != NULL) {
        *index = i;
    }

    return success;
}

static BOOL RFLiIsValidName(const RFLiCharData* data) {
    if (data == NULL) {
        return FALSE;
    }

    return data->name[0] != L'\0';
}

BOOL RFLiIsValidName2(const RFLiCharInfo* info) {
    RFLiCharData data;

    if (info == NULL) {
        return FALSE;
    }

    RFLiConvertInfo2Raw(info, &data);
    return RFLiIsValidName(&data);
}

static BOOL RFLiGetIsolation(void) {
    return !RFLiDBIsLoaded() ? TRUE : RFLI_GET(RFLiDatabase, RFLiGetDBManager()->database, isolation);
}

static RFLiHiddenDB* RFLiGetHiddenHeader(void) {
    if (!RFLiDBIsLoaded()) {
        return NULL;
    }

    return &RFLiGetDBManager()->database->hidden;
}

static BOOL RFLiDBIsLoaded(void) {
    if (!RFLAvailable()) {
        return FALSE;
    }

    if (RFLiGetDBManager() == NULL) {
        return FALSE;
    }

    return !RFLiNeedRepairError() && !RFLiNotFoundError();
}

static u16 RFLiCalculateCRC(const void* p, u32 len) {
    int i = 0;
    u16 crc = 0;
    const u8* current = (u8*)p;

    for (; i < len; current++, i++) {
        u8 data = *current;
        int j;

        for (j = 0; j < 8; j++, data <<= 1) {
            if (crc & 0x8000) {
                crc <<= 1;
                crc ^= 0x1021;
            } else {
                crc <<= 1;
            }

            if (data & 0x80) {
                crc ^= 0x1;
            }
        }
    }

    return crc;
}

static void alarmCreateCb_(OSAlarm* alarm, OSContext* ctx) {
#pragma unused(alarm)
#pragma unused(ctx)

    RFLiDBManager* mgr = RFLiGetDBManager();
    u8* current = mgr->crcInfo.current;
    u32 count = mgr->crcInfo.count;
    u16 crc = mgr->crcInfo.crc;
    u32 size = mgr->crcInfo.size;
    u32 curCount = 0;

    while (count < size) {
        u8 data = *current;
        int i;

        if (curCount >= ASYNC_CRC_STEP) {
            mgr->crcInfo.current = current;
            mgr->crcInfo.count = count;
            mgr->crcInfo.crc = crc;

            OSCancelAlarm(&mgr->crcInfo.alarm);
            OSSetAlarmUserData(&mgr->crcInfo.alarm, &mgr->crcInfo);
            OSSetAlarm(&mgr->crcInfo.alarm, OSMillisecondsToTicks(19),
                       alarmCreateCb_);
            return;
        }

        for (i = 0; i < 8; i++, data <<= 1) {
            if (crc & 0x8000) {
                crc <<= 1;
                crc ^= 0x1021;
            } else {
                crc <<= 1;
            }

            if (data & 0x80) {
                crc ^= 0x1;
            }
        }

        current++;
        count++;
        curCount++;
    }

    OSCancelAlarm(&mgr->crcInfo.alarm);
    mgr->crcInfo.callback(crc);
    RFLI_ST16(mgr->database->hidden.crc, crc);
}

static void RFLiCreateHeaderCRCAsync(RFLiExCallback callback) {
    RFLiDBManager* mgr = RFLiGetDBManager();

    if (!RFLiIsWorking()) {
        RFLiStartWorking();
    }

    RFLI_ST16(mgr->database->hidden.crc, 0);

    mgr->crcInfo.head = mgr->database;
    mgr->crcInfo.size = sizeof(RFLiDatabase);
    mgr->crcInfo.current = mgr->crcInfo.head;
    mgr->crcInfo.count = 0;
    mgr->crcInfo.crc = 0;
    mgr->crcInfo.callback = callback;

    OSCancelAlarm(&mgr->crcInfo.alarm);
    OSSetAlarmUserData(&mgr->crcInfo.alarm, &mgr->crcInfo);
    OSSetAlarm(&mgr->crcInfo.alarm, OSMillisecondsToTicks(19), alarmCreateCb_);
}

static void alarmCheckCb_(OSAlarm* alarm, OSContext* ctx) {
#pragma unused(alarm)
#pragma unused(ctx)

    RFLiDBManager* mgr = RFLiGetDBManager();
    u8* current = mgr->crcInfo.current;
    u32 count = mgr->crcInfo.count;
    u16 crc = mgr->crcInfo.crc;
    u32 size = mgr->crcInfo.size;
    u32 curCount = 0;

    while (count < size) {
        u8 data = *current;
        int i;

        if (curCount >= ASYNC_CRC_STEP) {
            mgr->crcInfo.current = current;
            mgr->crcInfo.count = count;
            mgr->crcInfo.crc = crc;

            OSCancelAlarm(&mgr->crcInfo.alarm);
            OSSetAlarmUserData(&mgr->crcInfo.alarm, &mgr->crcInfo);
            OSSetAlarm(&mgr->crcInfo.alarm, OSMillisecondsToTicks(19),
                       alarmCheckCb_);
            return;
        }

        for (i = 0; i < 8; i++, data <<= 1) {
            if (crc & 0x8000) {
                crc <<= 1;
                crc ^= 0x1021;
            } else {
                crc <<= 1;
            }

            if (data & 0x80) {
                crc ^= 0x1;
            }
        }

        current++;
        count++;
        curCount++;
    }

    OSCancelAlarm(&mgr->crcInfo.alarm);
    mgr->crcInfo.callback(crc);
}

static void RFLiCheckHeaderCRCAsync(RFLiExCallback callback) {
    RFLiDBManager* mgr = RFLiGetDBManager();

    if (!RFLiIsWorking()) {
        RFLiStartWorking();
    }

    mgr->crcInfo.head = mgr->database;
    mgr->crcInfo.size = sizeof(RFLiDatabase);
    mgr->crcInfo.current = mgr->crcInfo.head;
    mgr->crcInfo.count = 0;
    mgr->crcInfo.crc = 0;
    mgr->crcInfo.callback = callback;

    OSCancelAlarm(&mgr->crcInfo.alarm);
    OSSetAlarmUserData(&mgr->crcInfo.alarm, &mgr->crcInfo);
    OSSetAlarm(&mgr->crcInfo.alarm, OSMillisecondsToTicks(19), alarmCheckCb_);
}

#ifdef TARGET_PC
#include <nebula_game_api.h>

/* Mii import (the host's settings and its import folder). Records keep the console's bytes in memory (RFLi_Endian.h):
 * a stored Mii is copied as it is. */

/* The database's Miis changed (an import while the game runs): MiiSelect collects them again when it next opens. */
u32 __NebulaMiiGeneration = 0;

/* A Mii from another console or tool made acceptable to this one, then checked as RFLiGetCharData checks it: a
 * birthday the console would not take is cleared, a special Mii stays on this console (RFLiCheckValidInfo refuses one
 * that may travel), a favourite colour past the twelve becomes the first, and a Mii without a name takes the Mii
 * Channel's "no name". Pure: any thread. */
NEBULA_GAME_API uint32_t nebula_game_mii_prepare(uint8_t* record) {
    static const u16 scDayMax[12 + 1] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    static const char scNoName[] = "no name";
    RFLiCharData* const data = (RFLiCharData*)record;
    RFLiCharInfo info;
    u32 month;
    u32 day;
    u32 i;

    if (!RFLiIsValidID(&data->createID) || RFLiIsTemporaryID(&data->createID)) {
        return 0;
    }
    month = RFLI_GET(RFLiCharData, data, birthMonth);
    day = RFLI_GET(RFLiCharData, data, birthDay);
    if ((month == 0) != (day == 0) || month > 12 || day > scDayMax[month]) {
        RFLI_SET(RFLiCharData, data, birthMonth, 0);
        RFLI_SET(RFLiCharData, data, birthDay, 0);
    }
    if (RFLiIsSpecialID(&data->createID)) {
        RFLI_SET(RFLiCharData, data, localonly, 1);
    }
    if (RFLI_GET(RFLiCharData, data, favoriteColor) >= RFLFavoriteColor_Max) {
        RFLI_SET(RFLiCharData, data, favoriteColor, 0);
    }
    if (data->name[0] == 0) {
        for (i = 0; scNoName[i] != '\0'; i++) {
            RFLI_ST16(data->name[i], scNoName[i]);
        }
    }
    RFLiConvertRaw2Info(data, &info);
    return RFLiCheckValidInfo(&info) && RFLiIsValidOnNAND(&info) ? 1 : 0;
}

NEBULA_GAME_API uint32_t nebula_game_mii_install(uint32_t slot, const uint8_t* record) {
    RFLiDBManager* mgr;
    RFLiCharData* data;

    if (slot >= RFL_DB_CHAR_MAX || !RFLiDBIsLoaded()) {
        return 0;
    }
    mgr = RFLiGetDBManager();
    if (mgr == NULL || mgr->database == NULL) {
        return 0;
    }
    data = &mgr->database->rawData[slot];
    /* A free slot, or the same Mii (its create ID) changed. */
    if (RFLiIsValidID(&data->createID) && !RFLiIsSameID(&data->createID, &((const RFLiCharData*)record)->createID)) {
        return 0;
    }
    memcpy(data, record, sizeof(*data));
    __NebulaMiiGeneration++;
    return 1;
}
#endif
