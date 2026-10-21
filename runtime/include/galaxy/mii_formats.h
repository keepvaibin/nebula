// SPDX-License-Identifier: GPL-3.0-only
// Mii files of every console as the Wii's own Mii records (RFLiCharData): the Wii's (.mii, .rcd, .mae, .miigx, .rsd,
// its RFL_DB.dat, a Wii Remote's Mii block), the DS's (.ncd, .nsd), the 3DS's and Wii U's (.cfcd, .cfsd, .ffcd, .ffsd,
// their CFL_DB.dat and FFL_ODB.dat), the Switch's (.charinfo, .nfsd, .nfcd), Mii Studio's (its data and the obfuscated
// code, as bytes, hex text or a studio.mii.nintendo.com link) and Mii Creator's (.miic). A newer console's Mii takes
// the Wii's nearest parts and colours where the Wii has fewer (THIRD-PARTY-NOTICES: MiiJS's and MiiPort's tables).
#pragma once

#include "galaxy/mii_import.h"

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

}  // namespace galaxy::host
