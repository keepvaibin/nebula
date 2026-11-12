#include "Game/Mii/Mii.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The largest value of each part and setting the game draws.
    enum {
        MaxFaceType = 7, MaxFaceColor = 5, MaxFaceTexture = 11,
        MaxHairType = 71, MaxHairColor = 7,
        MaxEyeType = 47, MaxEyeColor = 5, MaxEyeScale = 7, MaxEyeRotate = 7, MaxEyeX = 12, MaxEyeY = 18,
        MaxEyebrowType = 23, MaxEyebrowColor = 7, MaxEyebrowScale = 8, MaxEyebrowRotate = 11, MaxEyebrowX = 12,
        MinEyebrowY = 3, MaxEyebrowY = 18,
        MaxNoseType = 11, MaxNoseScale = 8, MaxNoseY = 18,
        MaxMouthType = 23, MaxMouthColor = 2, MaxMouthScale = 8, MaxMouthY = 18,
        MaxMustache = 3, MaxBeardType = 3, MaxBeardColor = 7, MaxBeardScale = 8, MaxBeardY = 16,
        MaxGlassType = 8, MaxGlassColor = 5, MaxGlassScale = 7, MaxGlassY = 20,
        MaxMoleType = 1, MaxMoleScale = 8, MaxMoleX = 16, MaxMoleY = 30,
        MaxHeight = 128, MaxBuild = 128
    };

    // A create ID's first byte: "not special" clear for a special Mii, "temporary" set for one made on the spot.
    const u8 cIdNotSpecial = 0x80;
    const u8 cIdTemporary = 0x20;

    const NebulaColor8 cFavoriteColors[Mii::FavoriteColorCount] = {
        {184, 64, 48, 255},   {240, 120, 40, 255}, {248, 216, 32, 255}, {128, 200, 40, 255},
        {0, 116, 40, 255},    {32, 72, 152, 255},  {64, 160, 216, 255}, {232, 96, 120, 255},
        {112, 44, 168, 255},  {72, 56, 24, 255},   {224, 224, 224, 255}, {24, 24, 20, 255},
    };

    const NebulaColor8 cSkinColors[MaxFaceColor + 1] = {
        {240, 216, 196, 255}, {255, 188, 128, 255}, {216, 136, 80, 255},
        {255, 176, 144, 255}, {152, 80, 48, 255},   {82, 46, 28, 255},
    };

    // A default Mii: one of six plain Miis that differ only in these, every other part the default; marked local-only.
    constexpr NebulaMii defaultMii(u8 sex, u8 color, u8 faceColor, u8 hairType, u8 hairColor, u8 eyeType, u8 eyeColor,
                                   u8 eyeRotate, u8 eyebrowType, u8 eyebrowColor, u8 number) {
        NebulaMii m = {};
        m.sex = sex;
        m.favorite_color = color;
        m.local_only = 1;
        m.height = 64;
        m.build = 64;
        m.face_color = faceColor;
        m.hair_type = hairType;
        m.hair_color = hairColor;
        m.eye_type = eyeType;
        m.eye_color = eyeColor;
        m.eye_scale = 4;
        m.eye_rotate = eyeRotate;
        m.eye_x = 2;
        m.eye_y = 12;
        m.eyebrow_type = eyebrowType;
        m.eyebrow_color = eyebrowColor;
        m.eyebrow_scale = 4;
        m.eyebrow_rotate = 6;
        m.eyebrow_x = 2;
        m.eyebrow_y = 10;
        m.nose_type = 1;
        m.nose_scale = 4;
        m.nose_y = 9;
        m.mouth_type = 23;
        m.mouth_scale = 4;
        m.mouth_y = 13;
        m.beard_scale = 4;
        m.beard_y = 10;
        m.glass_scale = 4;
        m.glass_y = 10;
        m.mole_scale = 4;
        m.mole_x = 2;
        m.mole_y = 20;
        const char16_t name[] = u"no name";
        for (u32 i = 0; i + 1 < sizeof(name) / sizeof(name[0]); i++) {
            m.name[i] = name[i];
        }
        m.create_id[0] = 0x80;
        m.create_id[3] = number;
        m.create_id[4] = 0xEC;
        m.create_id[5] = 0xFF;
        m.create_id[6] = 0x82;
        m.create_id[7] = 0xD2;
        return m;
    }

    constexpr NebulaMii cDefaultMiis[Mii::DefaultCount] = {
        defaultMii(0, 4, 4, 68, 0, 2, 0, 4, 6, 0, 0),
        defaultMii(0, 5, 0, 55, 6, 2, 4, 4, 6, 6, 1),
        defaultMii(0, 0, 1, 33, 1, 2, 0, 4, 6, 1, 2),
        defaultMii(1, 2, 2, 24, 0, 4, 0, 3, 0, 0, 3),
        defaultMii(1, 6, 0, 14, 7, 4, 5, 3, 0, 7, 4),
        defaultMii(1, 7, 0, 12, 1, 4, 0, 3, 0, 1, 5),
    };

    bool validBirthday(u8 month, u8 day) {
        static const u8 cDays[13] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

        if ((month == 0) != (day == 0) || month > 12) {
            return false;
        }
        return day <= cDays[month];
    }

    u32 sGeneration = 0;
}  // namespace

namespace Mii {
    u32 libraryCount() {
        const NebulaHostServices* pHost = nebula_host();
        return pHost->mii_count(pHost->user);
    }

    bool character(const Ref& rRef, Character* pOut) {
        Character mii;

        if (rRef.source == SourceDefault) {
            if (rRef.index >= DefaultCount) {
                return false;
            }
            mii = cDefaultMiis[rRef.index];
        } else {
            const NebulaHostServices* pHost = nebula_host();
            if (pHost->mii_record(pHost->user, rRef.index, &mii) == 0) {
                return false;
            }
        }
        if (!isValidId(idOf(mii)) || !isValid(mii)) {
            return false;
        }
        if (pOut != nullptr) {
            *pOut = mii;
        }
        return true;
    }

    bool find(const Id& rId, u32* pIndex) {
        const u32 count = libraryCount();
        Character mii;

        for (u32 i = 0; i < count; i++) {
            if (character(Ref{SourceLibrary, i}, &mii) && isSameId(idOf(mii), rId)) {
                if (pIndex != nullptr) {
                    *pIndex = i;
                }
                return true;
            }
        }
        return false;
    }

    u32 generation() {
        return sGeneration;
    }

    const Id& idOf(const Character& rMii) {
        return *reinterpret_cast< const Id* >(rMii.create_id);
    }

    bool isValidId(const Id& rId) {
        for (u32 i = 0; i < sizeof(rId.data); i++) {
            if (rId.data[i] != 0) {
                return true;
            }
        }
        return false;
    }

    bool isTemporaryId(const Id& rId) {
        return isValidId(rId) && (rId.data[0] & cIdTemporary) != 0;
    }

    bool isSpecialId(const Id& rId) {
        return isValidId(rId) && !isTemporaryId(rId) && (rId.data[0] & cIdNotSpecial) == 0;
    }

    bool isSameId(const Id& rA, const Id& rB) {
        if (!isValidId(rA) || !isValidId(rB) || isTemporaryId(rA) || isTemporaryId(rB)) {
            return false;
        }
        return memcmp(rA.data, rB.data, sizeof(rA.data)) == 0;
    }

    bool isValid(const Character& m) {
        return m.face_type <= MaxFaceType && m.face_color <= MaxFaceColor && m.face_texture <= MaxFaceTexture &&
               m.hair_type <= MaxHairType && m.hair_color <= MaxHairColor && m.hair_flip <= 1 &&
               m.eye_type <= MaxEyeType && m.eye_color <= MaxEyeColor && m.eye_scale <= MaxEyeScale &&
               m.eye_rotate <= MaxEyeRotate && m.eye_x <= MaxEyeX && m.eye_y <= MaxEyeY &&
               m.eyebrow_type <= MaxEyebrowType && m.eyebrow_color <= MaxEyebrowColor &&
               m.eyebrow_scale <= MaxEyebrowScale && m.eyebrow_rotate <= MaxEyebrowRotate && m.eyebrow_x <= MaxEyebrowX &&
               m.eyebrow_y >= MinEyebrowY && m.eyebrow_y <= MaxEyebrowY &&
               m.nose_type <= MaxNoseType && m.nose_scale <= MaxNoseScale && m.nose_y <= MaxNoseY &&
               m.mouth_type <= MaxMouthType && m.mouth_color <= MaxMouthColor && m.mouth_scale <= MaxMouthScale &&
               m.mouth_y <= MaxMouthY &&
               m.mustache <= MaxMustache && m.beard_type <= MaxBeardType && m.beard_color <= MaxBeardColor &&
               m.beard_scale <= MaxBeardScale && m.beard_y <= MaxBeardY &&
               m.glass_type <= MaxGlassType && m.glass_color <= MaxGlassColor && m.glass_scale <= MaxGlassScale &&
               m.glass_y <= MaxGlassY &&
               m.mole_type <= MaxMoleType && m.mole_scale <= MaxMoleScale && m.mole_x <= MaxMoleX && m.mole_y <= MaxMoleY &&
               m.name[0] != 0 && m.height <= MaxHeight && m.build <= MaxBuild && m.sex <= 1 &&
               validBirthday(m.birth_month, m.birth_day) && m.favorite_color < FavoriteColorCount &&
               !(isSpecialId(idOf(m)) && m.local_only == 0);
    }

    void copyName(const Character& rMii, char16_t* pOut) {
        for (u32 i = 0; i < NameLength; i++) {
            pOut[i] = static_cast< char16_t >(rMii.name[i]);
        }
        pOut[NameLength] = u'\0';
    }

    NebulaColor8 favoriteColor(u32 color) {
        return cFavoriteColors[color < FavoriteColorCount ? color : 0];
    }

    NebulaColor8 skinColor(const Character& rMii) {
        return cSkinColors[rMii.face_color <= MaxFaceColor ? rMii.face_color : 0];
    }
}  // namespace Mii

NEBULA_GAME_API uint32_t nebula_game_mii_valid(const NebulaMii* pMii) {
    const Mii::Id& rId = Mii::idOf(*pMii);
    return Mii::isValidId(rId) && !Mii::isTemporaryId(rId) && Mii::isValid(*pMii) ? 1 : 0;
}

NEBULA_GAME_API void nebula_game_mii_changed(void) {
    sGeneration++;
}
