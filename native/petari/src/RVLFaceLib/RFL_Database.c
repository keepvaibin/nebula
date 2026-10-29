#include "RVLFaceLibInternal.h"
#include <nebula_game_api.h>
#include <string.h>

const NebulaHostServices* nebula_host(void);

// The official database is the host's (nebula_game_api.h mii_record): each slot's Mii as a typed record.

// Changes each time the host's database changes (MiiSelect reads its icons again).
u32 __NebulaMiiGeneration = 0;

static void infoFromMii_(const NebulaMii* m, RFLiCharInfo* info) {
    u32 i;

    memset(info, 0, sizeof(*info));
    info->faceline.type = m->face_type;
    info->faceline.color = m->face_color;
    info->faceline.texture = m->face_texture;

    info->hair.type = m->hair_type;
    info->hair.color = m->hair_color;
    info->hair.flip = m->hair_flip;

    info->eye.type = m->eye_type;
    info->eye.color = m->eye_color;
    info->eye.scale = m->eye_scale;
    info->eye.rotate = m->eye_rotate;
    info->eye.x = m->eye_x;
    info->eye.y = m->eye_y;

    info->eyebrow.type = m->eyebrow_type;
    info->eyebrow.color = m->eyebrow_color;
    info->eyebrow.scale = m->eyebrow_scale;
    info->eyebrow.rotate = m->eyebrow_rotate;
    info->eyebrow.x = m->eyebrow_x;
    info->eyebrow.y = m->eyebrow_y;

    info->nose.type = m->nose_type;
    info->nose.scale = m->nose_scale;
    info->nose.y = m->nose_y;

    info->mouth.type = m->mouth_type;
    info->mouth.color = m->mouth_color;
    info->mouth.scale = m->mouth_scale;
    info->mouth.y = m->mouth_y;

    info->beard.mustache = m->mustache;
    info->beard.type = m->beard_type;
    info->beard.color = m->beard_color;
    info->beard.scale = m->beard_scale;
    info->beard.y = m->beard_y;

    info->glass.type = m->glass_type;
    info->glass.color = m->glass_color;
    info->glass.scale = m->glass_scale;
    info->glass.y = m->glass_y;

    info->mole.type = m->mole_type;
    info->mole.scale = m->mole_scale;
    info->mole.x = m->mole_x;
    info->mole.y = m->mole_y;

    info->body.height = m->height;
    info->body.build = m->build;

    for (i = 0; i < RFL_NAME_LEN; i++) {
        info->personal.name[i] = (wchar_t)m->name[i];
    }
    for (i = 0; i < RFL_CREATOR_LEN; i++) {
        info->personal.creator[i] = (wchar_t)m->creator[i];
    }
    info->personal.sex = m->sex;
    info->personal.bmonth = m->birth_month;
    info->personal.bday = m->birth_day;
    info->personal.color = m->favorite_color;
    info->personal.favorite = m->favorite;
    info->personal.localOnly = m->local_only;

    memcpy(&info->createID, m->create_id, sizeof(RFLCreateID));
}

static BOOL officialMii_(u16 index, NebulaMii* m) {
    const NebulaHostServices* host = nebula_host();
    return index < RFL_DB_CHAR_MAX && host->mii_record(host->user, index, m) != 0;
}

RFLErrcode RFLiGetCharInfo(RFLiCharInfo* out, u16 index) {
    NebulaMii m;

    if (out == NULL || index >= RFL_DB_CHAR_MAX) {
        return RFLErrcode_WrongParam;
    }
    if (!officialMii_(index, &m)) {
        return RFLErrcode_NotAvailable;
    }
    infoFromMii_(&m, out);
    return RFLiCheckValidInfo(out) ? RFLErrcode_Success : RFLErrcode_Broken;
}

BOOL RFLIsAvailableOfficialData(u16 index) {
    return officialMii_(index, NULL);
}

BOOL RFLSearchOfficialData(const RFLCreateID* id, u16* index) {
    NebulaMii m;
    u16 i;

    if (id == NULL || !RFLAvailable()) {
        return FALSE;
    }
    for (i = 0; i < RFL_DB_CHAR_MAX; i++) {
        if (officialMii_(i, &m) && RFLiIsSameID((const RFLCreateID*)m.create_id, id)) {
            if (index != NULL) {
                *index = i;
            }
            return TRUE;
        }
    }
    return FALSE;
}

// A create ID's flags are its first byte's top bits: clear "not special" for a special Mii, "temporary" for one made
// on the spot.
#define CREATE_ID_NOT_SPECIAL 0x80
#define CREATE_ID_TEMPORARY 0x20

BOOL RFLiIsValidID(const RFLCreateID* id) {
    u32 i;

    if (id == NULL) {
        return FALSE;
    }
    for (i = 0; i < sizeof(id->data); i++) {
        if (id->data[i] != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL RFLiIsTemporaryID(const RFLCreateID* id) {
    return RFLiIsValidID(id) && (id->data[0] & CREATE_ID_TEMPORARY) != 0;
}

BOOL RFLiIsSpecialID(const RFLCreateID* id) {
    return RFLiIsValidID(id) && !RFLiIsTemporaryID(id) && (id->data[0] & CREATE_ID_NOT_SPECIAL) == 0;
}

BOOL RFLiIsSameID(const RFLCreateID* id1, const RFLCreateID* id2) {
    if (!RFLiIsValidID(id1) || !RFLiIsValidID(id2) || RFLiIsTemporaryID(id1) || RFLiIsTemporaryID(id2)) {
        return FALSE;
    }
    return memcmp(id1->data, id2->data, sizeof(id1->data)) == 0;
}

BOOL RFLiIsValidName2(const RFLiCharInfo* info) {
    return info != NULL && info->personal.name[0] != L'\0';
}

NEBULA_GAME_API uint32_t nebula_game_mii_valid(const NebulaMii* mii) {
    RFLiCharInfo info;

    infoFromMii_(mii, &info);
    if (!RFLiIsValidID(&info.createID) || RFLiIsTemporaryID(&info.createID)) {
        return 0;
    }
    return RFLiCheckValidInfo(&info) && RFLiIsValidOnNAND(&info) ? 1 : 0;
}

NEBULA_GAME_API void nebula_game_mii_changed(void) {
    __NebulaMiiGeneration++;
}
