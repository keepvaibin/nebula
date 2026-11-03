#include "MiiLibInternal.h"
#include <nebula_game_api.h>
#include <string.h>

const NebulaHostServices* nebula_host(void);

// The official database is the host's (nebula_game_api.h mii_record): each slot's Mii as a typed record.

// Changes each time the host's database changes (MiiSelect reads its icons again).
u32 __NebulaMiiGeneration = 0;

static void infoFromMii_(const NebulaMii* m, Mii_CharInfo* info) {
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

    for (i = 0; i < MII_NAME_LEN; i++) {
        info->personal.name[i] = (char16_t)m->name[i];
    }
    for (i = 0; i < MII_CREATOR_LEN; i++) {
        info->personal.creator[i] = (char16_t)m->creator[i];
    }
    info->personal.sex = m->sex;
    info->personal.bmonth = m->birth_month;
    info->personal.bday = m->birth_day;
    info->personal.color = m->favorite_color;
    info->personal.favorite = m->favorite;
    info->personal.localOnly = m->local_only;

    memcpy(&info->createID, m->create_id, sizeof(MiiCreateID));
}

static BOOL officialMii_(u16 index, NebulaMii* m) {
    const NebulaHostServices* host = nebula_host();
    return index < MII_DB_CHAR_MAX && host->mii_record(host->user, index, m) != 0;
}

MiiErrcode Mii_GetCharInfo(Mii_CharInfo* out, u16 index) {
    NebulaMii m;

    if (out == NULL || index >= MII_DB_CHAR_MAX) {
        return MiiErrcode_WrongParam;
    }
    if (!officialMii_(index, &m)) {
        return MiiErrcode_NotAvailable;
    }
    infoFromMii_(&m, out);
    return Mii_CheckValidInfo(out) ? MiiErrcode_Success : MiiErrcode_Broken;
}

BOOL MiiIsAvailableOfficialData(u16 index) {
    return officialMii_(index, NULL);
}

BOOL MiiSearchOfficialData(const MiiCreateID* id, u16* index) {
    NebulaMii m;
    u16 i;

    if (id == NULL || !MiiAvailable()) {
        return FALSE;
    }
    for (i = 0; i < MII_DB_CHAR_MAX; i++) {
        if (officialMii_(i, &m) && Mii_IsSameID((const MiiCreateID*)m.create_id, id)) {
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

BOOL Mii_IsValidID(const MiiCreateID* id) {
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

BOOL Mii_IsTemporaryID(const MiiCreateID* id) {
    return Mii_IsValidID(id) && (id->data[0] & CREATE_ID_TEMPORARY) != 0;
}

BOOL Mii_IsSpecialID(const MiiCreateID* id) {
    return Mii_IsValidID(id) && !Mii_IsTemporaryID(id) && (id->data[0] & CREATE_ID_NOT_SPECIAL) == 0;
}

BOOL Mii_IsSameID(const MiiCreateID* id1, const MiiCreateID* id2) {
    if (!Mii_IsValidID(id1) || !Mii_IsValidID(id2) || Mii_IsTemporaryID(id1) || Mii_IsTemporaryID(id2)) {
        return FALSE;
    }
    return memcmp(id1->data, id2->data, sizeof(id1->data)) == 0;
}

BOOL Mii_IsValidName2(const Mii_CharInfo* info) {
    return info != NULL && info->personal.name[0] != u'\0';
}

NEBULA_GAME_API uint32_t nebula_game_mii_valid(const NebulaMii* mii) {
    Mii_CharInfo info;

    infoFromMii_(mii, &info);
    if (!Mii_IsValidID(&info.createID) || Mii_IsTemporaryID(&info.createID)) {
        return 0;
    }
    return Mii_CheckValidInfo(&info) && Mii_IsValidOnNAND(&info) ? 1 : 0;
}

NEBULA_GAME_API void nebula_game_mii_changed(void) {
    __NebulaMiiGeneration++;
}
