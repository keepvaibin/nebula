// SPDX-License-Identifier: GPL-3.0-only
// The game's own parts a start with mods needs (contract start-check, section 2): checked at step 1 of a start, only
// when at least one mod is on. Any missing: every mod is off this session, with one mod-log line saying which.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace galaxy::assets {
class AssetStore;
}

namespace galaxy::startcheck {

struct MissingPart {
    const char* row = nullptr;  // its name's text row: Start.Part.Importer, .Compiler, .Placeholders or .Space
    std::string needed;         // Start.Part.Space's {needed} ("214 MB"); "" for the others
    std::string why;            // for the developer's log, in English: what was looked at and what was found
};

// Section 2's parts in its order; the first one missing, or nothing. `store` is the game's store (Nebula's placeholder
// data is in it), `cook_folder` where the mods' content is cooked (it is made when absent), `cook_bytes` the plan's
// estimate of what the cook writes. Runs the shader compiler's version once per install of it (cached in
// cook_folder). The thread that starts mods, before the first scene.
[[nodiscard]] std::optional<MissingPart> missing_part(const galaxy::assets::AssetStore& store,
                                                      const std::filesystem::path& cook_folder, std::uint64_t cook_bytes);

// The session's one mod-log entry for it: mod "nebula", source COOK, Error, Start.PartMissing with the part's row (the
// caller writes `why` to the developer's log).
void report(const MissingPart& part);

}  // namespace galaxy::startcheck
