// SPDX-License-Identifier: GPL-3.0-only
// Mii files of every console as the Wii's own Mii records (RFLiCharData): the Wii's (.mii, .rcd, .mae, .miigx, .rsd,
// its RFL_DB.dat, a Wii Remote's Mii block), the DS's (.ncd, .nsd), the 3DS's and Wii U's (.cfcd, .cfsd, .ffcd, .ffsd,
// their CFL_DB.dat and FFL_ODB.dat), the Switch's (.charinfo, .nfsd, .nfcd), Mii Studio's (its data and the obfuscated
// code, as bytes, hex text or a studio.mii.nintendo.com link) and Mii Creator's (.miic). A newer console's Mii takes
// the Wii's nearest parts and colours where the Wii has fewer (THIRD-PARTY-NOTICES: MiiJS's and MiiPort's tables).
#pragma once

#include "galaxy/mii_import.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::host {

struct MiiFileContents {
    std::vector<MiiRecord> miis;  // as the Wii stores them; a Mii made elsewhere has a placeholder create ID
    std::string format;           // what the file held
    unsigned converted = 0;       // made from a newer console's Mii (its extra parts and colours: the nearest)
    std::string error;            // why none
};

// `extension`: the file's, lower case without the dot ("ncd" tells a DS Mii from a Wii one of the same size).
[[nodiscard]] MiiFileContents read_mii_file(std::span<const std::byte> bytes, std::string_view extension);

// A create ID no console made (all of its time or console bits zero: tools write 80000000 00000000).
[[nodiscard]] bool mii_placeholder_id(const MiiRecord& record) noexcept;

// A Wii Mii record's fields as numbers (its bitfields, names in host byte order, create ID as stored).
struct MiiFields {
    std::uint8_t sex = 0, birth_month = 0, birth_day = 0, favorite_color = 0, favorite = 0, local_only = 0;
    std::uint8_t height = 0, build = 0;
    std::uint8_t face_type = 0, face_color = 0, face_texture = 0;
    std::uint8_t hair_type = 0, hair_color = 0, hair_flip = 0;
    std::uint8_t eye_type = 0, eye_color = 0, eye_scale = 0, eye_rotate = 0, eye_x = 0, eye_y = 0;
    std::uint8_t eyebrow_type = 0, eyebrow_color = 0, eyebrow_scale = 0, eyebrow_rotate = 0, eyebrow_x = 0,
                 eyebrow_y = 0;
    std::uint8_t nose_type = 0, nose_scale = 0, nose_y = 0;
    std::uint8_t mouth_type = 0, mouth_color = 0, mouth_scale = 0, mouth_y = 0;
    std::uint8_t mustache = 0, beard_type = 0, beard_color = 0, beard_scale = 0, beard_y = 0;
    std::uint8_t glass_type = 0, glass_color = 0, glass_scale = 0, glass_y = 0;
    std::uint8_t mole_type = 0, mole_scale = 0, mole_x = 0, mole_y = 0;
    std::array<std::uint16_t, 10> name{}, creator{};
    std::array<std::uint8_t, 8> create_id{};
};
[[nodiscard]] MiiFields decode_wii_mii(const MiiRecord& record) noexcept;
// What the console fixes before it keeps a Mii: a birthday it does not take cleared, a special Mii kept local, a
// favourite colour out of range reset, no name "no name".
void fix_wii_mii(MiiRecord& record) noexcept;

}  // namespace galaxy::host
