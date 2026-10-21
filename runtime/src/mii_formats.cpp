// SPDX-License-Identifier: GPL-3.0-only
// Mii files of every console as Wii Mii records (mii_formats.h).
//
// Layouts: the Wii's record from the game's own RVLFaceLib (RFLi_Endian.h bit positions); the 3DS/Wii U record
// (Ver3StoreData), the Switch's CharInfo, CoreData and StoreData from MiiPort's structure patterns (Genwald, ISC) and
// 3dbrew; Mii Studio's and Mii Creator's from MiiJS (Stewared, ISC). The Switch's colour and glasses tables are the
// Switch's own downgrade to the 3DS's (MiiPort); the 3DS parts the Wii lacks take MiiJS's nearest Wii part (its own
// visual choices: no console converts that way). THIRD-PARTY-NOTICES.md carries both notices.

#include "galaxy/mii_formats.h"

#include "galaxy/wii_device_data.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace galaxy::host {
namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

constexpr std::size_t kWiiRecord = kMiiRecordSize;  // 0x4A
constexpr std::size_t kVer3Core = 0x5C;              // CFCD / FFCD
constexpr std::size_t kVer3Store = 0x60;             // CFSD / FFSD: + padding + CRC
constexpr std::size_t kCharInfo = 0x58;
constexpr std::size_t kCoreData = 0x30;              // NFCD
constexpr std::size_t kStoreData = 0x44;             // NFSD
constexpr std::size_t kStudio = 0x2E;
constexpr std::size_t kStudioObfuscated = 0x2F;
constexpr std::size_t kMiiCreator = 0x80;
constexpr std::size_t kRemoteBlock = 0x2F0;
constexpr u32 kRemoteMagic = 0x524E4344;  // "RNCD"
constexpr u32 kCflMagic = 0x43464F47;     // "CFOG": the 3DS's CFL_DB.dat
constexpr u32 kFflMagic = 0x46464F43;     // "FFOC": the Wii U's FFL_ODB.dat

u8 byte(std::span<const std::byte> b, std::size_t at) { return static_cast<u8>(b[at]); }
u16 le16(std::span<const std::byte> b, std::size_t at) { return static_cast<u16>(byte(b, at) | byte(b, at + 1) << 8); }
u16 be16(std::span<const std::byte> b, std::size_t at) { return static_cast<u16>(byte(b, at) << 8 | byte(b, at + 1)); }
u32 be32(std::span<const std::byte> b, std::size_t at) { return static_cast<u32>(be16(b, at)) << 16 | be16(b, at + 2); }

// The Wii record's fields: bit position from the record's first bit, most significant first in each big-endian
// 16-bit unit, and width (RVLFaceLib RFLi_Endian.h).
struct WiiField {
    u16 pos;
    u8 len;
};
constexpr WiiField kSex{1, 1}, kBirthMonth{2, 4}, kBirthDay{6, 5}, kFavoriteColor{11, 4}, kFavorite{15, 1};
constexpr WiiField kFaceType{256, 3}, kFaceColor{259, 3}, kFaceTex{262, 4}, kLocalOnly{269, 1};
constexpr WiiField kHairType{272, 7}, kHairColor{279, 3}, kHairFlip{282, 1};
constexpr WiiField kEyebrowType{288, 5}, kEyebrowRotate{293, 5}, kEyebrowColor{304, 3}, kEyebrowScale{307, 4},
    kEyebrowY{311, 5}, kEyebrowX{316, 4};
constexpr WiiField kEyeType{320, 6}, kEyeRotate{326, 5}, kEyeY{331, 5}, kEyeColor{336, 3}, kEyeScale{339, 4},
    kEyeX{343, 4};
constexpr WiiField kNoseType{352, 4}, kNoseScale{356, 4}, kNoseY{360, 5};
constexpr WiiField kMouthType{368, 5}, kMouthColor{373, 2}, kMouthScale{375, 4}, kMouthY{379, 5};
constexpr WiiField kGlassType{384, 4}, kGlassColor{388, 3}, kGlassScale{391, 4}, kGlassY{395, 5};
constexpr WiiField kMustacheType{400, 2}, kBeardType{402, 2}, kBeardColor{404, 3}, kBeardScale{407, 4},
    kBeardY{411, 5};
constexpr WiiField kMoleType{416, 1}, kMoleScale{417, 4}, kMoleY{421, 5}, kMoleX{426, 5};

void set_bits(MiiRecord& r, WiiField f, u32 value) {
    const std::size_t unit = (f.pos >> 4u) * 2u;
    const u32 shift = 16u - (f.pos & 15u) - f.len;
    const u32 mask = ((1u << f.len) - 1u) << shift;
    const u32 word = (((static_cast<u32>(r[unit]) << 8u | static_cast<u32>(r[unit + 1u])) & ~mask) |
                      ((value << shift) & mask));
    r[unit] = static_cast<std::byte>(word >> 8u);
    r[unit + 1u] = static_cast<std::byte>(word);
}

// The 16-bit units the Wii's bitfields fill (byte offsets): a DS record holds the same units little-endian, its names
// little-endian UTF-16 (MiiJS NCD: each unit's bits from its low byte's least significant).
constexpr std::array<u16, 12> kWiiUnits{0x00, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32, 0x34};

MiiRecord wii_from_ds(std::span<const std::byte> ds) {
    MiiRecord r{};
    std::copy_n(ds.begin(), kWiiRecord, r.begin());
    for (const u16 unit : kWiiUnits) {
        std::swap(r[unit], r[unit + 1u]);
    }
    for (const std::size_t text : {std::size_t{0x02}, std::size_t{0x36}}) {
        for (std::size_t i = 0; i < 10u; ++i) {
            std::swap(r[text + 2u * i], r[text + 2u * i + 1u]);
        }
    }
    return r;
}

// A Mii of the 3DS, Wii U or Switch in the 3DS's numbering (Ver3StoreData), or in the Switch's colours (CharInfo).
struct ModernMii {
    bool switch_colors = false;
    bool special = false;
    bool local_only = false;
    std::array<u16, 10> name{};
    std::array<u16, 10> creator{};
    u8 gender = 0, birth_month = 0, birth_day = 0, favorite_color = 0;
    bool favorite = false;
    u8 height = 64, build = 64;
    u8 face_type = 0, face_color = 0, face_wrinkle = 0, face_makeup = 0;
    u8 hair_type = 0, hair_color = 0, hair_flip = 0;
    u8 eye_type = 0, eye_color = 0, eye_scale = 0, eye_rotate = 0, eye_x = 0, eye_y = 0;
    u8 eyebrow_type = 0, eyebrow_color = 0, eyebrow_scale = 0, eyebrow_rotate = 0, eyebrow_x = 0, eyebrow_y = 3;
    u8 nose_type = 0, nose_scale = 0, nose_y = 0;
    u8 mouth_type = 0, mouth_color = 0, mouth_scale = 0, mouth_y = 0;
    u8 mustache_type = 0, beard_type = 0, beard_color = 0, mustache_scale = 0, mustache_y = 0;
    u8 glass_type = 0, glass_color = 0, glass_scale = 0, glass_y = 0;
    u8 mole = 0, mole_scale = 0, mole_x = 0, mole_y = 0;
};

// The Switch's colours and glasses in the 3DS's (MiiPort convert_mii.h: the Switch's own downgrade).
constexpr std::array<u8, 20> kToVer3GlassType{0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 1, 3, 7, 7, 6, 7, 8, 7, 7};
constexpr std::array<u8, 100> kToVer3HairColor{
    0, 1, 2, 3, 4, 5, 6, 7, 0, 4, 3, 5, 4, 4, 6, 2, 0, 6, 4, 3, 2, 2, 7, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 4, 4, 5, 5, 5,
    4, 4, 4, 4, 4, 4, 4, 5, 7, 5, 7, 7, 7, 7, 7, 6, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 0, 4, 4, 4, 4};
constexpr std::array<u8, 100> kToVer3EyeColor{
    0, 2, 2, 2, 1, 3, 2, 3, 0, 1, 2, 3, 4, 5, 2, 2, 4, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 0, 0, 4, 4, 4, 4, 4, 4, 4, 1, 0, 4, 4, 4, 4, 4, 4, 4, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1};
constexpr std::array<u8, 100> kToVer3MouthColor{
    4, 4, 4, 4, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4, 1, 4, 4, 4, 0, 1, 2, 3, 4, 4, 2, 3, 3, 4, 4, 4, 4, 1, 4,
    4, 2, 3, 3, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 4, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 3, 3, 3, 3, 4, 4, 4,
    4, 3, 3, 3, 3, 3, 3, 4, 4, 3, 3, 3, 3, 3, 3, 4, 3, 3, 3, 3, 3, 4, 0, 3, 3, 3, 3, 4, 3, 3, 3, 3};
constexpr std::array<u8, 100> kToVer3GlassColor{
    0, 1, 1, 1, 5, 1, 1, 4, 0, 5, 1, 1, 3, 5, 1, 2, 3, 4, 5, 4, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 5, 5, 5, 5, 5, 5, 0, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5};
constexpr std::array<u8, 10> kToVer3FacelineColor{0, 1, 2, 3, 4, 5, 0, 1, 5, 5};

// The 3DS's parts past the Wii's (MiiJS data.js backTables["3ds"]).
constexpr std::array<u8, 60> kWiiHairForVer3{25, 64, 63, 64, 44, 61, 19, 1,  24, 69, 4,  53, 9,  71, 67,
                                            50, 65, 47, 0,  1,  2,  4,  13, 12, 7,  9,  63, 16, 14, 71,
                                            71, 18, 19, 12, 12, 33, 58, 9,  27, 28, 1,  35, 46, 33, 70,
                                            6,  67, 35, 11, 64, 37, 0,  25, 64, 46, 1,  14, 14, 9,  15};
constexpr std::array<u8, 4> kWiiFaceForVer3{0, 1, 4, 6};
constexpr std::array<u8, 12> kWiiEyeForVer3{32, 37, 27, 9, 44, 12, 40, 31, 17, 2, 40, 40};
constexpr std::array<u8, 6> kWiiNoseForVer3{8, 3, 10, 3, 9, 8};
constexpr std::array<u8, 12> kWiiMouthForVer3{1, 8, 6, 1, 18, 1, 10, 9, 4, 21, 14, 10};
// The Wii has one facial feature for the 3DS's wrinkles and make-up: its wrinkles where the Wii has them, else its
// make-up (-1: none of the Wii's, the make-up decides; then the value given).
constexpr std::array<int, 12> kWiiFeatureForWrinkle{-1, -1, 5, 6, -1, 4, 7, 7, 8, 10, -1, 11};
constexpr std::array<u8, 12> kWiiFeatureForWrinkleFallback{0, 6, 5, 6, 6, 4, 7, 7, 8, 10, 6, 11};
constexpr std::array<u8, 12> kWiiFeatureForMakeup{0, 1, 1, 2, 1, 1, 2, 2, 2, 3, 9, 9};

template <std::size_t N>
u8 lookup(const std::array<u8, N>& table, unsigned index) {
    return index < N ? table[index] : table[0];
}

u32 clamp_to(unsigned value, unsigned high) { return std::min(value, high); }

MiiRecord wii_from_modern(ModernMii m) {
    if (m.switch_colors) {
        m.hair_color = lookup(kToVer3HairColor, m.hair_color);
        m.eye_color = lookup(kToVer3EyeColor, m.eye_color);
        m.glass_type = lookup(kToVer3GlassType, m.glass_type);
        m.glass_color = lookup(kToVer3GlassColor, m.glass_color);
        m.mouth_color = lookup(kToVer3MouthColor, m.mouth_color);
        m.face_color = lookup(kToVer3FacelineColor, m.face_color);
        m.eyebrow_color = lookup(kToVer3HairColor, m.eyebrow_color);
        m.beard_color = lookup(kToVer3HairColor, m.beard_color);
    }
    // The 3DS's to the Wii's (MiiJS backPort).
    if (m.mouth_color > 2u) m.mouth_color = 0u;
    if (m.beard_type > 3u) m.beard_type = 3u;
    if (m.mustache_type == 4u) {
        m.mustache_type = 2u;
    } else if (m.mustache_type == 5u) {
        m.mustache_type = 0u;
        m.beard_type = 1u;
    }
    u8 feature = 0u;
    if (m.face_wrinkle < 12u && kWiiFeatureForWrinkle[m.face_wrinkle] >= 0) {
        feature = static_cast<u8>(kWiiFeatureForWrinkle[m.face_wrinkle]);
    } else if (m.face_makeup < 12u && kWiiFeatureForMakeup[m.face_makeup] > 0u) {
        feature = kWiiFeatureForMakeup[m.face_makeup];
    } else {
        feature = m.face_wrinkle < 12u ? kWiiFeatureForWrinkleFallback[m.face_wrinkle] : 0u;
    }
    if (m.hair_type > 71u) m.hair_type = lookup(kWiiHairForVer3, m.hair_type - 72u);
    if (m.face_type > 7u) m.face_type = lookup(kWiiFaceForVer3, m.face_type - 8u);
    if (m.eye_type > 47u) m.eye_type = lookup(kWiiEyeForVer3, m.eye_type - 48u);
    if (m.nose_type > 11u) m.nose_type = lookup(kWiiNoseForVer3, m.nose_type - 12u);
    if (m.mouth_type > 23u) m.mouth_type = lookup(kWiiMouthForVer3, m.mouth_type - 24u);

    MiiRecord r{};
    // A placeholder create ID (a fresh one is made on import): its type bits, special or not (RVLFaceLib
    // RFLi_CREATE_ID_MASK_NOT_SPECIAL; MiiJS: 100 normal, 010 special).
    r[0x18] = static_cast<std::byte>(m.special ? 0x40u : 0x80u);
    for (std::size_t i = 0; i < 10u; ++i) {
        r[0x02 + 2 * i] = static_cast<std::byte>(m.name[i] >> 8u);
        r[0x03 + 2 * i] = static_cast<std::byte>(m.name[i]);
        r[0x36 + 2 * i] = static_cast<std::byte>(m.creator[i] >> 8u);
        r[0x37 + 2 * i] = static_cast<std::byte>(m.creator[i]);
    }
    if (m.name[0] == 0u) {
        // The Wii takes no Mii without a name: its Mii Channel's default (Mii Studio's data has none).
        static constexpr char kNoName[] = "no name";
        for (std::size_t i = 0; i + 1u < sizeof(kNoName); ++i) {
            r[0x03 + 2 * i] = static_cast<std::byte>(kNoName[i]);
        }
    }
    r[0x16] = static_cast<std::byte>(clamp_to(m.height, 127u));
    r[0x17] = static_cast<std::byte>(clamp_to(m.build, 127u));
    set_bits(r, kSex, m.gender & 1u);
    const bool birthday = m.birth_month >= 1u && m.birth_month <= 12u && m.birth_day >= 1u && m.birth_day <= 31u;
    set_bits(r, kBirthMonth, birthday ? m.birth_month : 0u);
    set_bits(r, kBirthDay, birthday ? m.birth_day : 0u);
    set_bits(r, kFavoriteColor, clamp_to(m.favorite_color, 11u));
    set_bits(r, kFavorite, m.favorite ? 1u : 0u);
    set_bits(r, kFaceType, clamp_to(m.face_type, 7u));
    set_bits(r, kFaceColor, clamp_to(m.face_color, 5u));
    set_bits(r, kFaceTex, clamp_to(feature, 11u));
    set_bits(r, kLocalOnly, (m.local_only || m.special) ? 1u : 0u);
    set_bits(r, kHairType, clamp_to(m.hair_type, 71u));
    set_bits(r, kHairColor, clamp_to(m.hair_color, 7u));
    set_bits(r, kHairFlip, m.hair_flip & 1u);
    set_bits(r, kEyebrowType, clamp_to(m.eyebrow_type, 23u));
    set_bits(r, kEyebrowRotate, clamp_to(m.eyebrow_rotate, 11u));
    set_bits(r, kEyebrowColor, clamp_to(m.eyebrow_color, 7u));
    set_bits(r, kEyebrowScale, clamp_to(m.eyebrow_scale, 8u));
    set_bits(r, kEyebrowY, std::clamp<u32>(m.eyebrow_y, 3u, 18u));
    set_bits(r, kEyebrowX, clamp_to(m.eyebrow_x, 12u));
    set_bits(r, kEyeType, clamp_to(m.eye_type, 47u));
    set_bits(r, kEyeRotate, clamp_to(m.eye_rotate, 7u));
    set_bits(r, kEyeY, clamp_to(m.eye_y, 18u));
    set_bits(r, kEyeColor, clamp_to(m.eye_color, 5u));
    set_bits(r, kEyeScale, clamp_to(m.eye_scale, 7u));
    set_bits(r, kEyeX, clamp_to(m.eye_x, 12u));
    set_bits(r, kNoseType, clamp_to(m.nose_type, 11u));
    set_bits(r, kNoseScale, clamp_to(m.nose_scale, 8u));
    set_bits(r, kNoseY, clamp_to(m.nose_y, 18u));
    set_bits(r, kMouthType, clamp_to(m.mouth_type, 23u));
    set_bits(r, kMouthColor, clamp_to(m.mouth_color, 2u));
    set_bits(r, kMouthScale, clamp_to(m.mouth_scale, 8u));
    set_bits(r, kMouthY, clamp_to(m.mouth_y, 18u));
    set_bits(r, kGlassType, clamp_to(m.glass_type, 8u));
    set_bits(r, kGlassColor, clamp_to(m.glass_color, 5u));
    set_bits(r, kGlassScale, clamp_to(m.glass_scale, 7u));
    set_bits(r, kGlassY, clamp_to(m.glass_y, 20u));
    set_bits(r, kMustacheType, clamp_to(m.mustache_type, 3u));
    set_bits(r, kBeardType, clamp_to(m.beard_type, 3u));
    set_bits(r, kBeardColor, clamp_to(m.beard_color, 7u));
    set_bits(r, kBeardScale, clamp_to(m.mustache_scale, 8u));
    set_bits(r, kBeardY, clamp_to(m.mustache_y, 16u));
    set_bits(r, kMoleType, m.mole & 1u);
    set_bits(r, kMoleScale, clamp_to(m.mole_scale, 8u));
    set_bits(r, kMoleY, clamp_to(m.mole_y, 30u));
    set_bits(r, kMoleX, clamp_to(m.mole_x, 16u));
    return r;
}

void read_le_text(std::span<const std::byte> b, std::size_t at, std::array<u16, 10>& out) {
    bool ended = false;
    for (std::size_t i = 0; i < 10u; ++i) {
        const u16 c = le16(b, at + 2u * i);
        ended = ended || c == 0u;
        out[i] = ended ? 0u : c;
    }
}

// Ver3StoreData (0x5C bytes of a CFCD/FFCD/CFSD/FFSD): little-endian units, fields from the least significant bit.
bool ver3(std::span<const std::byte> b, ModernMii& m) {
    if (b.size() < kVer3Core || byte(b, 0) != 3u) {
        return false;
    }
    m.special = (be32(b, 0x0C) & 0x80000000u) == 0u;
    const u16 profile = le16(b, 0x18);
    m.gender = profile & 1u;
    m.birth_month = (profile >> 1u) & 15u;
    m.birth_day = (profile >> 5u) & 31u;
    m.favorite_color = (profile >> 10u) & 15u;
    m.favorite = ((profile >> 14u) & 1u) != 0u;
    read_le_text(b, 0x1A, m.name);
    m.height = byte(b, 0x2E);
    m.build = byte(b, 0x2F);
    u16 w = le16(b, 0x30);
    m.local_only = (w & 1u) != 0u;
    m.face_type = (w >> 1u) & 15u;
    m.face_color = (w >> 5u) & 7u;
    m.face_wrinkle = (w >> 8u) & 15u;
    m.face_makeup = (w >> 12u) & 15u;
    w = le16(b, 0x32);
    m.hair_type = w & 255u;
    m.hair_color = (w >> 8u) & 7u;
    m.hair_flip = (w >> 11u) & 1u;
    w = le16(b, 0x34);
    m.eye_type = w & 63u;
    m.eye_color = (w >> 6u) & 7u;
    m.eye_scale = (w >> 9u) & 15u;
    w = le16(b, 0x36);
    m.eye_rotate = w & 31u;
    m.eye_x = (w >> 5u) & 15u;
    m.eye_y = (w >> 9u) & 31u;
    w = le16(b, 0x38);
    m.eyebrow_type = w & 31u;
    m.eyebrow_color = (w >> 5u) & 7u;
    m.eyebrow_scale = (w >> 8u) & 15u;
    w = le16(b, 0x3A);
    m.eyebrow_rotate = w & 31u;
    m.eyebrow_x = (w >> 5u) & 15u;
    m.eyebrow_y = (w >> 9u) & 31u;
    w = le16(b, 0x3C);
    m.nose_type = w & 31u;
    m.nose_scale = (w >> 5u) & 15u;
    m.nose_y = (w >> 9u) & 31u;
    w = le16(b, 0x3E);
    m.mouth_type = w & 63u;
    m.mouth_color = (w >> 6u) & 7u;
    m.mouth_scale = (w >> 9u) & 15u;
    w = le16(b, 0x40);
    m.mouth_y = w & 31u;
    m.mustache_type = (w >> 5u) & 7u;
    w = le16(b, 0x42);
    m.beard_type = w & 7u;
    m.beard_color = (w >> 3u) & 7u;
    m.mustache_scale = (w >> 6u) & 15u;
    m.mustache_y = (w >> 10u) & 31u;
    w = le16(b, 0x44);
    m.glass_type = w & 15u;
    m.glass_color = (w >> 4u) & 7u;
    m.glass_scale = (w >> 7u) & 15u;
    m.glass_y = (w >> 11u) & 31u;
    w = le16(b, 0x46);
    m.mole = w & 1u;
    m.mole_scale = (w >> 1u) & 15u;
    m.mole_x = (w >> 5u) & 31u;
    m.mole_y = (w >> 10u) & 31u;
    read_le_text(b, 0x48, m.creator);
    return true;
}

// The Switch's CharInfo (0x58): one byte a field.
bool char_info(std::span<const std::byte> b, ModernMii& m) {
    if (b.size() != kCharInfo || byte(b, 0x57) != 0u || le16(b, 0x24) != 0u) {
        return false;
    }
    m.switch_colors = true;
    read_le_text(b, 0x10, m.name);
    m.favorite_color = byte(b, 0x27);
    m.gender = byte(b, 0x28);
    m.height = byte(b, 0x29);
    m.build = byte(b, 0x2A);
    m.special = byte(b, 0x2B) != 0u;
    m.face_type = byte(b, 0x2D);
    m.face_color = byte(b, 0x2E);
    m.face_wrinkle = byte(b, 0x2F);
    m.face_makeup = byte(b, 0x30);
    m.hair_type = byte(b, 0x31);
    m.hair_color = byte(b, 0x32);
    m.hair_flip = byte(b, 0x33);
    m.eye_type = byte(b, 0x34);
    m.eye_color = byte(b, 0x35);
    m.eye_scale = byte(b, 0x36);
    m.eye_rotate = byte(b, 0x38);
    m.eye_x = byte(b, 0x39);
    m.eye_y = byte(b, 0x3A);
    m.eyebrow_type = byte(b, 0x3B);
    m.eyebrow_color = byte(b, 0x3C);
    m.eyebrow_scale = byte(b, 0x3D);
    m.eyebrow_rotate = byte(b, 0x3F);
    m.eyebrow_x = byte(b, 0x40);
    m.eyebrow_y = byte(b, 0x41);
    m.nose_type = byte(b, 0x42);
    m.nose_scale = byte(b, 0x43);
    m.nose_y = byte(b, 0x44);
    m.mouth_type = byte(b, 0x45);
    m.mouth_color = byte(b, 0x46);
    m.mouth_scale = byte(b, 0x47);
    m.mouth_y = byte(b, 0x49);
    m.beard_color = byte(b, 0x4A);
    m.beard_type = byte(b, 0x4B);
    m.mustache_type = byte(b, 0x4C);
    m.mustache_scale = byte(b, 0x4D);
    m.mustache_y = byte(b, 0x4E);
    m.glass_type = byte(b, 0x4F);
    m.glass_color = byte(b, 0x50);
    m.glass_scale = byte(b, 0x51);
    m.glass_y = byte(b, 0x52);
    m.mole = byte(b, 0x53);
    m.mole_scale = byte(b, 0x54);
    m.mole_x = byte(b, 0x55);
    m.mole_y = byte(b, 0x56);
    // Every field in the Switch's range: a 0x58-byte file that is no CharInfo is refused.
    return m.gender <= 1u && m.favorite_color <= 11u && m.height <= 127u && m.build <= 127u && m.face_type <= 11u &&
           m.face_color <= 9u && m.face_wrinkle <= 11u && m.face_makeup <= 11u && m.hair_type <= 131u &&
           m.hair_color <= 99u && m.hair_flip <= 1u && m.eye_type <= 59u && m.eye_color <= 99u &&
           m.eye_scale <= 7u && m.eye_rotate <= 7u && m.eye_x <= 12u && m.eye_y <= 18u && m.eyebrow_type <= 24u &&
           m.eyebrow_color <= 99u && m.eyebrow_scale <= 8u && m.eyebrow_rotate <= 11u && m.eyebrow_x <= 12u &&
           m.eyebrow_y >= 3u && m.eyebrow_y <= 18u && m.nose_type <= 17u && m.nose_scale <= 8u && m.nose_y <= 18u &&
           m.mouth_type <= 35u && m.mouth_color <= 99u && m.mouth_scale <= 8u && m.mouth_y <= 18u &&
           m.beard_color <= 99u && m.beard_type <= 5u && m.mustache_type <= 5u && m.mustache_scale <= 8u &&
           m.mustache_y <= 16u && m.glass_type <= 19u && m.glass_color <= 99u && m.glass_scale <= 7u &&
           m.glass_y <= 20u && m.mole <= 1u && m.mole_scale <= 8u && m.mole_x <= 16u && m.mole_y <= 30u;
}

// The Switch's CoreData (0x30, the first part of its StoreData): byte-wide groups, fields from the least significant
// bit; its eyebrow height 3 under the true value.
bool core_data(std::span<const std::byte> b, ModernMii& m) {
    if (b.size() < kCoreData) {
        return false;
    }
    m.switch_colors = true;
    const auto lo = [&](std::size_t at, u32 bits) { return static_cast<u8>(byte(b, at) & ((1u << bits) - 1u)); };
    const auto hi = [&](std::size_t at, u32 from) { return static_cast<u8>(byte(b, at) >> from); };
    m.hair_type = byte(b, 0);
    m.height = lo(1, 7);
    m.mole = hi(1, 7);
    m.build = lo(2, 7);
    m.hair_flip = hi(2, 7);
    m.hair_color = lo(3, 7);
    m.special = hi(3, 7) != 0u;
    m.eye_color = lo(4, 7);
    m.gender = hi(4, 7);
    m.eyebrow_color = lo(5, 7);
    m.mouth_color = lo(6, 7);
    m.beard_color = lo(7, 7);
    m.glass_color = lo(8, 7);
    m.eye_type = lo(9, 6);
    m.mouth_type = lo(10, 6);
    m.eye_y = lo(11, 5);
    m.glass_scale = hi(11, 5);
    m.eyebrow_type = lo(12, 5);
    m.mustache_type = hi(12, 5);
    m.nose_type = lo(13, 5);
    m.beard_type = hi(13, 5);
    m.nose_y = lo(14, 5);
    m.mouth_y = lo(15, 5);
    m.mustache_y = lo(16, 5);
    m.eye_rotate = hi(16, 5);
    m.glass_y = lo(17, 5);
    m.mole_x = lo(18, 5);
    m.eye_scale = hi(18, 5);
    m.mole_y = lo(19, 5);
    m.glass_type = lo(20, 5);
    m.favorite_color = lo(21, 4);
    m.face_type = hi(21, 4);
    m.face_color = lo(22, 4);
    m.face_wrinkle = hi(22, 4);
    m.face_makeup = lo(23, 4);
    m.eye_x = hi(23, 4);
    m.eyebrow_scale = lo(24, 4);
    m.eyebrow_rotate = hi(24, 4);
    m.eyebrow_x = lo(25, 4);
    m.eyebrow_y = static_cast<u8>(hi(25, 4) + 3u);
    m.nose_scale = lo(26, 4);
    m.mouth_scale = hi(26, 4);
    m.mustache_scale = lo(27, 4);
    m.mole_scale = hi(27, 4);
    read_le_text(b, 0x1C, m.name);
    return m.hair_type <= 131u && m.hair_color <= 99u && m.eye_color <= 99u && m.eyebrow_color <= 99u &&
           m.mouth_color <= 99u && m.beard_color <= 99u && m.glass_color <= 99u && m.eye_type <= 59u &&
           m.mouth_type <= 35u && m.eye_y <= 18u && m.nose_type <= 17u && m.beard_type <= 5u &&
           m.mustache_type <= 5u && m.nose_y <= 18u && m.mouth_y <= 18u && m.mustache_y <= 16u &&
           m.glass_y <= 20u && m.mole_x <= 16u && m.mole_y <= 30u && m.glass_type <= 19u &&
           m.favorite_color <= 11u && m.face_type <= 11u && m.face_color <= 9u && m.face_wrinkle <= 11u &&
           m.face_makeup <= 11u && m.eye_x <= 12u && m.eyebrow_scale <= 8u && m.eyebrow_rotate <= 11u &&
           m.eyebrow_x <= 12u && m.eyebrow_y <= 18u && m.nose_scale <= 8u && m.mouth_scale <= 8u &&
           m.mustache_scale <= 8u && m.mole_scale <= 8u;
}

// Mii Studio's data (0x2E): one byte a field, its fields in alphabetical order (MiiJS MNMS).
bool studio(std::span<const std::byte> b, ModernMii& m) {
    if (b.size() != kStudio) {
        return false;
    }
    m.switch_colors = true;
    std::array<u8, kStudio> v{};
    for (std::size_t i = 0; i < kStudio; ++i) v[i] = byte(b, i);
    m.beard_color = v[0];
    m.beard_type = v[1];
    m.build = v[2];
    m.eye_color = v[4];
    m.eye_rotate = v[5];
    m.eye_scale = v[6];
    m.eye_type = v[7];
    m.eye_x = v[8];
    m.eye_y = v[9];
    m.eyebrow_color = v[11];
    m.eyebrow_rotate = v[12];
    m.eyebrow_scale = v[13];
    m.eyebrow_type = v[14];
    m.eyebrow_x = v[15];
    m.eyebrow_y = v[16];
    m.face_color = v[17];
    m.face_makeup = v[18];
    m.face_type = v[19];
    m.face_wrinkle = v[20];
    m.favorite_color = v[21];
    m.gender = v[22];
    m.glass_color = v[23];
    m.glass_scale = v[24];
    m.glass_type = v[25];
    m.glass_y = v[26];
    m.hair_color = v[27];
    m.hair_flip = v[28];
    m.hair_type = v[29];
    m.height = v[30];
    m.mole_scale = v[31];
    m.mole = v[32];
    m.mole_x = v[33];
    m.mole_y = v[34];
    m.mouth_color = v[36];
    m.mouth_scale = v[37];
    m.mouth_type = v[38];
    m.mouth_y = v[39];
    m.mustache_scale = v[40];
    m.mustache_type = v[41];
    m.mustache_y = v[42];
    m.nose_scale = v[43];
    m.nose_type = v[44];
    m.nose_y = v[45];
    return v[0] <= 99u && v[1] <= 5u && v[2] <= 127u && v[3] <= 6u && v[4] <= 99u && v[5] <= 7u && v[6] <= 7u &&
           v[7] <= 59u && v[8] <= 12u && v[9] <= 18u && v[10] <= 6u && v[11] <= 99u && v[12] <= 11u &&
           v[13] <= 8u && v[14] <= 24u && v[15] <= 12u && v[16] >= 3u && v[16] <= 18u && v[17] <= 9u &&
           v[18] <= 11u && v[19] <= 11u && v[20] <= 11u && v[21] <= 11u && v[22] <= 1u && v[23] <= 99u &&
           v[24] <= 7u && v[25] <= 19u && v[26] <= 20u && v[27] <= 99u && v[28] <= 1u && v[29] <= 131u &&
           v[30] <= 127u && v[31] <= 8u && v[32] <= 1u && v[33] <= 16u && v[34] <= 30u && v[35] <= 6u &&
           v[36] <= 99u && v[37] <= 8u && v[38] <= 35u && v[39] <= 18u && v[40] <= 8u && v[41] <= 5u &&
           v[42] <= 16u && v[43] <= 8u && v[44] <= 17u && v[45] <= 18u;
}

// Mii Studio's code (0x2F): each byte after the first XORs the one before it, 7 added (MiiJS decryptStudio2).
std::vector<std::byte> studio_plain(std::span<const std::byte> b) {
    std::vector<std::byte> out(kStudio);
    for (std::size_t i = 1; i < kStudioObfuscated; ++i) {
        out[i - 1u] = static_cast<std::byte>(byte(b, i - 1u) ^ static_cast<u8>(byte(b, i) - 7u));
    }
    return out;
}

// Mii Creator's (.miic, 0x80, version 4): one byte a field (MiiJS MIIC); its clothes, hat and extra colours have no
// Wii counterpart.
bool mii_creator(std::span<const std::byte> b, ModernMii& m) {
    if (b.size() != kMiiCreator || byte(b, 0) != 4u) {
        return false;
    }
    m.switch_colors = true;
    read_le_text(b, 20, m.creator);
    read_le_text(b, 40, m.name);
    m.beard_color = byte(b, 60);
    m.beard_type = byte(b, 61);
    m.birth_day = byte(b, 62);
    m.birth_month = byte(b, 63);
    m.build = byte(b, 66);
    m.eyebrow_color = byte(b, 69);
    m.eyebrow_rotate = byte(b, 70);
    m.eyebrow_scale = byte(b, 71);
    m.eyebrow_type = byte(b, 72);
    m.eyebrow_x = byte(b, 73);
    m.eyebrow_y = byte(b, 74);
    m.eye_color = byte(b, 75);
    m.eye_rotate = byte(b, 76);
    m.eye_scale = byte(b, 77);
    m.eye_type = byte(b, 78);
    m.eye_x = byte(b, 79);
    m.eye_y = byte(b, 80);
    m.face_color = byte(b, 81);
    m.face_makeup = byte(b, 82);
    m.face_type = byte(b, 83);
    m.face_wrinkle = byte(b, 84);
    m.favorite = byte(b, 86) != 0u;
    m.favorite_color = byte(b, 87);
    m.gender = byte(b, 89);
    m.glass_color = byte(b, 90);
    m.glass_scale = byte(b, 91);
    m.glass_type = byte(b, 92);
    m.glass_y = byte(b, 93);
    m.hair_color = byte(b, 94);
    m.hair_flip = byte(b, 95);
    m.hair_type = byte(b, 96);
    m.height = byte(b, 100);
    m.mole_scale = byte(b, 102);
    m.mole = byte(b, 103);
    m.mole_x = byte(b, 104);
    m.mole_y = byte(b, 105);
    m.mouth_color = byte(b, 107);
    m.mouth_scale = byte(b, 108);
    m.mouth_type = byte(b, 109);
    m.mouth_y = byte(b, 110);
    m.mustache_scale = byte(b, 111);
    m.mustache_type = byte(b, 112);
    m.mustache_y = byte(b, 113);
    m.nose_scale = byte(b, 114);
    m.nose_type = byte(b, 115);
    m.nose_y = byte(b, 116);
    m.special = byte(b, 121) != 0u;
    // Its colours past the Switch's palette (Mii Creator's own): the Switch's first.
    for (u8* colour : {&m.hair_color, &m.eye_color, &m.eyebrow_color, &m.mouth_color, &m.beard_color,
                       &m.glass_color}) {
        if (*colour > 99u) *colour = 0u;
    }
    if (m.face_color > 9u) m.face_color = 0u;
    if (m.glass_type > 19u) m.glass_type = 0u;
    return true;
}

// Hex text (a Mii Studio code, a hex dump), or a studio.mii.nintendo.com link whose `data=` holds it.
bool hex_text(std::span<const std::byte> b, std::vector<std::byte>& out) {
    std::string text;
    for (const std::byte c : b) {
        const char ch = static_cast<char>(c);
        if (ch == '\0') return false;
        if (static_cast<unsigned char>(ch) >= 0x80u) return false;
        text.push_back(ch);
    }
    if (const std::size_t data = text.find("data="); data != std::string::npos) {
        std::size_t end = data + 5u;
        while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end]))) ++end;
        text = text.substr(data + 5u, end - data - 5u);
    }
    std::string digits;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == ':' || ch == '-' || ch == ',') continue;
        if (ch == '0' && i + 1u < text.size() && (text[i + 1u] == 'x' || text[i + 1u] == 'X')) {
            ++i;
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        digits.push_back(ch);
    }
    if (digits.empty() || digits.size() % 2u != 0u) return false;
    out.clear();
    for (std::size_t i = 0; i < digits.size(); i += 2u) {
        out.push_back(static_cast<std::byte>(std::stoi(digits.substr(i, 2u), nullptr, 16)));
    }
    return true;
}

std::uint16_t xmodem(std::span<const std::byte> block) {
    std::vector<std::byte> bytes(block.begin(), block.end());
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    return rfl_crc(bytes);
}

bool has_id(std::span<const std::byte> record) {
    return std::any_of(record.begin() + 0x18, record.begin() + 0x20, [](std::byte v) { return v != std::byte{0}; });
}

MiiFileContents read_bytes(std::span<const std::byte> b, std::string_view extension, int depth) {
    MiiFileContents out;
    const auto wii = [&](std::size_t first, std::size_t count, std::size_t stride, const char* format) {
        out.format = format;
        for (std::size_t i = 0; i < count && first + i * stride + kWiiRecord <= b.size(); ++i) {
            const auto record = b.subspan(first + i * stride, kWiiRecord);
            if (has_id(record)) {
                MiiRecord r{};
                std::copy(record.begin(), record.end(), r.begin());
                out.miis.push_back(r);
            }
        }
    };
    const auto modern = [&](ModernMii m) {
        out.miis.push_back(wii_from_modern(m));
        ++out.converted;
    };
    const auto ver3_list = [&](std::size_t first, std::size_t count, const char* format) {
        out.format = format;
        for (std::size_t i = 0; i < count && first + (i + 1u) * kVer3Core <= b.size(); ++i) {
            ModernMii m;
            if (ver3(b.subspan(first + i * kVer3Core, kVer3Core), m)) modern(m);
        }
    };
    const bool ds = extension == "ncd" || extension == "nsd";
    const u32 magic = b.size() >= 4u ? be32(b, 0) : 0u;
    ModernMii m;
    if (b.size() == kRflDatabaseSize && magic == kRflOfficialMagic) {
        wii(0x04, kMiiDatabaseSlots, kWiiRecord, "Wii Mii database (RFL_DB.dat)");
    } else if (b.size() == kRemoteBlock && magic == kRemoteMagic) {
        wii(0x08, 10u, kWiiRecord, "Wii Remote Mii data");
    } else if (magic == kCflMagic) {
        ver3_list(0x08, 100u, "3DS Mii database (CFL_DB.dat)");
    } else if (magic == kFflMagic) {
        ver3_list(0x08, 3000u, "Wii U Mii database (FFL_ODB.dat)");
    } else if ((b.size() == kWiiRecord || b.size() == kWiiRecord + 2u) && ds) {
        out.format = b.size() == kWiiRecord ? "DS Mii (.ncd)" : "DS Mii (.nsd)";
        const MiiRecord r = wii_from_ds(b);
        if (has_id(r)) out.miis.push_back(r);
    } else if (b.size() == kWiiRecord || b.size() == kWiiRecord + 2u) {
        // A Wii Mii (.mii, .rcd, .mae, .miigx; .rsd with its CRC). A CRC that does not match is no reason to refuse
        // one: the game's own checks decide.
        out.format = b.size() == kWiiRecord ? "Wii Mii" : "Wii Mii (.rsd)";
        wii(0, 1u, kWiiRecord, out.format.c_str());
        if (b.size() == kWiiRecord + 2u && xmodem(b.first(kWiiRecord)) != be16(b, kWiiRecord)) {
            out.format += ", CRC mismatch";
        }
    } else if ((b.size() == kVer3Core || b.size() == kVer3Store) && ver3(b, m)) {
        out.format = byte(b, 3) >> 4u == 4u ? "Wii U or Switch Mii (Ver3)" : "3DS Mii (Ver3)";
        modern(m);
    } else if (b.size() == kCharInfo && char_info(b, m)) {
        out.format = "Switch Mii (CharInfo)";
        modern(m);
    } else if ((b.size() == kStoreData || b.size() == kCoreData) && core_data(b, m)) {
        out.format = b.size() == kStoreData ? "Switch Mii (StoreData)" : "Switch Mii (CoreData)";
        modern(m);
    } else if (b.size() == kStudio && studio(b, m)) {
        out.format = "Mii Studio data";
        modern(m);
    } else if (b.size() == kStudioObfuscated && studio(studio_plain(b), m)) {
        out.format = "Mii Studio code";
        modern(m);
    } else if (b.size() == kMiiCreator && mii_creator(b, m)) {
        out.format = "Mii Creator Mii (.miic)";
        modern(m);
    } else if (std::vector<std::byte> decoded; depth == 0 && hex_text(b, decoded)) {
        out = read_bytes(decoded, extension, depth + 1);
        if (!out.miis.empty()) out.format += " (hex)";
    } else {
        out.error = "NOT A KNOWN MII FILE";
    }
    if (out.miis.empty() && out.error.empty()) {
        out.error = "NO MIIS";
    }
    return out;
}

}  // namespace

bool mii_placeholder_id(const MiiRecord& r) noexcept {
    const u32 mii = static_cast<u32>(r[0x18]) << 24u | static_cast<u32>(r[0x19]) << 16u |
                    static_cast<u32>(r[0x1A]) << 8u | static_cast<u32>(r[0x1B]);
    const u32 system = static_cast<u32>(r[0x1C]) << 24u | static_cast<u32>(r[0x1D]) << 16u |
                       static_cast<u32>(r[0x1E]) << 8u | static_cast<u32>(r[0x1F]);
    return (mii & 0x1FFFFFFFu) == 0u || system == 0u;
}

MiiFileContents read_mii_file(std::span<const std::byte> bytes, std::string_view extension) {
    return read_bytes(bytes, extension, 0);
}

}  // namespace galaxy::host
