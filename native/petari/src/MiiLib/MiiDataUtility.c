#include "MiiLibInternal.h"
#include <stdio.h>

// For masking out padding when comparing fields
#define PADMASK(t, size) ((t)(~((1 << size) - 1)))
// Padding bitmask constants (see Mii_CharInfo structure)
#define FACELINE_PAD_MASK (PADMASK(u16, 6))
#define HAIR_PAD_MASK (PADMASK(u16, 5))
#define EYE_PAD_MASK (PADMASK(u32, 5))
#define EYEBROW_PAD_MASK (PADMASK(u32, 6))
#define NOSE_PAD_MASK (PADMASK(u16, 3))
#define MOUTH_PAD_MASK (PADMASK(u16, 0))
#define BEARD_PAD_MASK (PADMASK(u16, 0))
#define GLASS_PAD_MASK (PADMASK(u16, 1))
#define MOLE_PAD_MASK (PADMASK(u16, 1))

BOOL Mii_CheckValidInfo(const Mii_CharInfo* info) {
    if (info->faceline.type > MII_I_MAX_FACELINE_TYPE) {
        return FALSE;
    }
    if (info->faceline.color > MII_I_MAX_FACELINE_COLOR) {
        return FALSE;
    }
    if (info->faceline.texture > MII_I_MAX_FACELINE_TEXTURE) {
        return FALSE;
    }

    if (info->hair.type > MII_I_MAX_HAIR_TYPE) {
        return FALSE;
    }
    if (info->hair.color > MII_I_MAX_HAIR_COLOR) {
        return FALSE;
    }
    if (info->hair.flip > TRUE) {
        return FALSE;
    }

    if (info->eye.type > MII_I_MAX_EYE_TYPE) {
        return FALSE;
    }
    if (info->eye.color > MII_I_MAX_EYE_COLOR) {
        return FALSE;
    }
    if (info->eye.scale > MII_I_MAX_EYE_SCALE) {
        return FALSE;
    }
    if (info->eye.rotate > MII_I_MAX_EYE_ROTATE) {
        return FALSE;
    }
    if (info->eye.x > MII_I_MAX_EYE_X) {
        return FALSE;
    }
    if (info->eye.y > MII_I_MAX_EYE_Y) {
        return FALSE;
    }

    if (info->eyebrow.type > MII_I_MAX_EYEBROW_TYPE) {
        return FALSE;
    }
    if (info->eyebrow.color > MII_I_MAX_EYEBROW_COLOR) {
        return FALSE;
    }
    if (info->eyebrow.scale > MII_I_MAX_EYEBROW_SCALE) {
        return FALSE;
    }
    if (info->eyebrow.rotate > MII_I_MAX_EYEBROW_ROTATE) {
        return FALSE;
    }
    if (info->eyebrow.x > MII_I_MAX_EYEBROW_X) {
        return FALSE;
    }
    if (info->eyebrow.y < MII_I_MIN_EYEBROW_Y) {
        return FALSE;
    }
    if (info->eyebrow.y > MII_I_MAX_EYEBROW_Y) {
        return FALSE;
    }

    if (info->nose.type > MII_I_MAX_NOSE_TYPE) {
        return FALSE;
    }
    if (info->nose.scale > MII_I_MAX_NOSE_SCALE) {
        return FALSE;
    }
    if (info->nose.y > MII_I_MAX_NOSE_Y) {
        return FALSE;
    }

    if (info->mouth.type > MII_I_MAX_MOUTH_TYPE) {
        return FALSE;
    }
    if (info->mouth.color > MII_I_MAX_MOUTH_COLOR) {
        return FALSE;
    }
    if (info->mouth.scale > MII_I_MAX_MOUTH_SCALE) {
        return FALSE;
    }
    if (info->mouth.y > MII_I_MAX_MOUTH_Y) {
        return FALSE;
    }

    if (info->beard.mustache > MII_I_MAX_BEARD_MUSTACHE) {
        return FALSE;
    }
    if (info->beard.type > MII_I_MAX_BEARD_TYPE) {
        return FALSE;
    }
    if (info->beard.color > MII_I_MAX_BEARD_COLOR) {
        return FALSE;
    }
    if (info->beard.scale > MII_I_MAX_BEARD_SCALE) {
        return FALSE;
    }
    if (info->beard.y > MII_I_MAX_BEARD_Y) {
        return FALSE;
    }

    if (info->glass.type > MII_I_MAX_GLASS_TYPE) {
        return FALSE;
    }
    if (info->glass.color > MII_I_MAX_GLASS_COLOR) {
        return FALSE;
    }
    if (info->glass.scale > MII_I_MAX_GLASS_SCALE) {
        return FALSE;
    }
    if (info->glass.y > MII_I_MAX_GLASS_Y) {
        return FALSE;
    }

    if (info->mole.type > MII_I_MAX_MOLE_TYPE) {
        return FALSE;
    }
    if (info->mole.scale > MII_I_MAX_MOLE_SCALE) {
        return FALSE;
    }
    if (info->mole.x > MII_I_MAX_MOLE_X) {
        return FALSE;
    }
    if (info->mole.y > MII_I_MAX_MOLE_Y) {
        return FALSE;
    }

    if (!Mii_IsValidName2(info)) {
        return FALSE;
    }

    if (info->body.height > MII_I_MAX_BODY_HEIGHT) {
        return FALSE;
    }
    if (info->body.build > MII_I_MAX_BODY_BUILD) {
        return FALSE;
    }

    if (info->personal.sex > MiiSex_Female) {
        return FALSE;
    }
    if (!Mii_CheckBirthday(info->personal.bmonth, info->personal.bday)) {
        return FALSE;
    }
    if (info->personal.color >= MiiFavoriteColor_Max) {
        return FALSE;
    }

    if (Mii_IsSpecialID(&info->createID) && !info->personal.localOnly) {
        return FALSE;
    }

    return TRUE;
}

BOOL Mii_IsValidOnNAND(const Mii_CharInfo* info) {
    return !Mii_IsTemporaryID(&info->createID);
}

MiiErrcode Mii_PickupCharInfo(Mii_CharInfo* info, MiiDataSource source,
                              MiiMiddleDB* db, u16 index) {
    MiiErrcode err;

    if (!MiiAvailable()) {
        return MiiErrcode_NotAvailable;
    }

    switch (source) {
    case MiiDataSource_Official:
        err = Mii_GetCharInfo(info, index);
        break;
    case MiiDataSource_Default:
        Mii_GetDefaultData(info, index);
        err = MiiErrcode_Success;
        break;
    default:
        // The game keeps its Miis in the official database (and the library's defaults): no remote's Miis, no
        // middle database.
        (void)db;
        err = MiiErrcode_Broken;
        break;
    }

    if (err == MiiErrcode_Success) {
        if (!Mii_IsValidID(&info->createID)) {
            err = MiiErrcode_Broken;
        } else if (!Mii_CheckValidInfo(info)) {
            err = MiiErrcode_Broken;
        }
    }

    return err;
}

static void copyChar2Additional_(MiiAdditionalInfo* dst,
                                 const Mii_CharInfo* src) {
    u8 height;
    u8 build;

    memcpy(dst->name, src->personal.name, sizeof(char16_t) * MII_NAME_LEN);
    dst->name[MII_NAME_LEN] = '\0';

    memcpy(dst->creator, src->personal.creator,
           sizeof(char16_t) * MII_CREATOR_LEN);
    // @bug Copy paste error
    dst->name[MII_CREATOR_LEN] = '\0';

    dst->sex = src->personal.sex;

    if (Mii_CheckBirthday(src->personal.bmonth, src->personal.bday)) {
        dst->bmonth = src->personal.bmonth;
        dst->bday = src->personal.bday;
    } else {
        dst->bmonth = 0;
        dst->bday = 0;
    }

    dst->color = src->personal.color;
    dst->favorite = src->personal.favorite;

    height = src->body.height;
    build = src->body.build;

    // @bug Max height is 128, not 127
    if (height > MII_I_MAX_BODY_HEIGHT - 1) {
        height = MII_I_MAX_BODY_HEIGHT - 1;
    }
    // @bug Max build is 128, not 127
    if (build > MII_I_MAX_BODY_BUILD - 1) {
        build = MII_I_MAX_BODY_BUILD - 1;
    }

    dst->height = height;
    dst->build = build;
    memcpy(&dst->createID, &src->createID, sizeof(MiiCreateID));
    dst->reserved = 0;
    dst->skinColor = Mii_GetFacelineColor(src);
}

MiiErrcode MiiGetAdditionalInfo(MiiAdditionalInfo* addi, MiiDataSource source,
                                MiiMiddleDB* db, u16 index) {
    Mii_CharInfo info;
    MiiErrcode err;

    err = Mii_PickupCharInfo(&info, source, db, index);
    if (err == MiiErrcode_Success) {
        copyChar2Additional_(addi, &info);
    }

    return err;
}

static BOOL Mii_CheckBirthday(u8 month, u8 day) {
    // (One-indexed)
    static const u8 scDayMax[12 + 1] = {0,  31, 29, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};

    if (month == 0 && day != 0) {
        return FALSE;
    }

    if (month != 0 && day == 0) {
        return FALSE;
    }

    if (month > 12 || day > 31) {
        return FALSE;
    }

    return day <= scDayMax[month];
}
