// SPDX-License-Identifier: GPL-3.0-only
// The pak's ray tracing tables' parsing (rt_materials.h): a stage's numbers and the effects as lights, and what a
// hostile or broken table (a mod's) does to them.
#include "galaxy/render/rt_materials.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

bool stage_table() {
    using galaxy::render::RtStage;
    galaxy::render::RtStageTable table;
    const char* text =
        "# rt_stages.tsv\n"
        "stage\tsun\tsun_gain\tsun size\tambient gain\texposure low\texposure high\tshadow sets\tnote\r\n"
        "EggStarGalaxy\t0 3 4\t1.2\t0.02\t\t\t\t\tfixed\n"
        "PhantomGalaxy\tnone\t\t\t0.9\t60\t30\tkeep\tnight\n"
        "HeavenlyBeachGalaxy\tkey\t9\t\t\t\t\t\tclamped\n"
        "BadGalaxy\t0 0 0\n"
        "\tobject\n";
    bool ok = expect(table.read(text) == 4u, "four rows, the nameless one skipped");
    const RtStage* egg = table.find(galaxy::render::rt_stage_hash("EggStarGalaxy"));
    ok &= expect(egg != nullptr && egg->sun == RtStage::Sun::Fixed && near(egg->direction[1], 0.6f) && near(egg->direction[2], 0.8f) &&
                     near(egg->sun_gain, 1.2f) && near(egg->sun_size, 0.02f) && near(egg->ambient_gain, 1.1f) && !egg->shadow_sets_kept,
                 "a fixed sun, normalised; empty fields keep the defaults");
    const RtStage* night = table.find(galaxy::render::rt_stage_hash("PhantomGalaxy"));
    ok &= expect(night != nullptr && night->sun == RtStage::Sun::None && near(night->ambient_gain, 0.9f) &&
                     near(night->exposure_low, 60.0f) && near(night->exposure_high, 60.0f) && night->shadow_sets_kept,
                 "no sun; exposure's high never under its low; shadow sets kept");
    const RtStage* beach = table.find(galaxy::render::rt_stage_hash("HeavenlyBeachGalaxy"));
    ok &= expect(beach != nullptr && beach->sun == RtStage::Sun::Key && near(beach->sun_gain, 4.0f), "the key; gain clamped");
    const RtStage* bad = table.find(galaxy::render::rt_stage_hash("BadGalaxy"));
    ok &= expect(bad != nullptr && bad->sun == RtStage::Sun::Auto, "no direction in three zeros: auto");
    // The air: a number as it is, held to 0.01; empty, below 0 or no row: none.
    galaxy::render::RtStageTable air;
    ok &= expect(air.read("stage\tair\tnote\nHoneyBeeKingdomGalaxy\t0.00002\tshafts\nSandClockGalaxy\t0.5\t\n"
                          "PhantomGalaxy\t\tspace\nStarDustGalaxy\t-1\t\n") == 4u,
                 "four air rows");
    const RtStage* thin = air.find(galaxy::render::rt_stage_hash("HoneyBeeKingdomGalaxy"));
    const RtStage* sand = air.find(galaxy::render::rt_stage_hash("SandClockGalaxy"));
    const RtStage* empty = air.find(galaxy::render::rt_stage_hash("PhantomGalaxy"));
    const RtStage* below = air.find(galaxy::render::rt_stage_hash("StarDustGalaxy"));
    ok &= expect(thin != nullptr && thin->air == 0.00002f, "the air as given");
    ok &= expect(sand != nullptr && sand->air == 0.01f, "the air held to 0.01");
    ok &= expect(empty != nullptr && empty->air == 0.0f && below != nullptr && below->air == 0.0f &&
                     galaxy::render::kRtStageDefaults.air == 0.0f,
                 "empty, below 0 or no row: no air");
    ok &= expect(table.find(galaxy::render::rt_stage_hash("Missing")) == nullptr, "no row");
    ok &= expect(galaxy::render::rt_stage_hash("EggStarGalaxy") == 0x099E9A42u, "FNV-1a of the stage's name, as the game's");
    ok &= expect(galaxy::render::rt_stage_hash("") == 0u, "no stage");
    return ok;
}

bool effect_table() {
    galaxy::render::RtEffectTable table;
    const char* text =
        "effect\tclass\tluminance\tbehaviour\n"
        "2PGlowActiveLoop00\tglow or lamp\t300\tsteady\n"
        "Smoke\tsmoke\t0\tsteady\n"
        "Fuse\tfuse\t1500\tflash\n"
        "Nameless\tx\t\tsteady\n";
    bool ok = expect(table.read(text) == 3u, "three effects, the one without a luminance skipped");
    ok &= expect(galaxy::render::rt_effect_hash("2PGlowActiveLoop00") == 0x29FEECAEu, "the hash the game's draws carry");
    const auto* glow = table.find(0x29FEECAEu);
    ok &= expect(glow != nullptr && near(glow->luminance, 300.0f) && glow->behaviour == 0u, "a lamp's glow");
    const auto* fuse = table.find(galaxy::render::rt_effect_hash("FUSE"));
    ok &= expect(fuse != nullptr && fuse->behaviour == 3u, "a flash; names in any case");
    const auto* smoke = table.find(galaxy::render::rt_effect_hash("Smoke"));
    ok &= expect(smoke != nullptr && smoke->luminance == 0.0f, "smoke is no light");
    return ok;
}

bool broken_tables() {
    using galaxy::render::RtStage;
    bool ok = true;
    // A byte order mark before the header is not part of its first name.
    galaxy::render::RtStageTable marked;
    ok &= expect(marked.read("\xEF\xBB\xBF" "stage\tsun gain\nEggStarGalaxy\t2\n") == 1u, "a byte order mark is not a name");
    const RtStage* egg = marked.find(galaxy::render::rt_stage_hash("EggStarGalaxy"));
    ok &= expect(egg != nullptr && near(egg->sun_gain, 2.0f), "the row under a marked header");
    // Numbers that are not finite, words where numbers go, rows shorter or longer than the header: the defaults hold.
    galaxy::render::RtStageTable odd;
    ok &= expect(odd.read("stage\tsun\tsun gain\tair\texposure low\texposure high\twater clear\n"
                          "A\tnan 1 0\tinf\tnan\t-inf\t1e30\tx\n"
                          "B\n"
                          "C\t1 2\t\t\t\t\t\t\t\t\t\textra\n") == 3u,
                 "three named rows");
    const RtStage* a = odd.find(galaxy::render::rt_stage_hash("A"));
    ok &= expect(a != nullptr && a->sun == RtStage::Sun::Auto && near(a->sun_gain, 1.0f) && a->air == 0.0f &&
                     near(a->exposure_low, 40.0f) && near(a->exposure_high, 10000.0f) && near(a->water_clear, 600.0f),
                 "non-finite numbers are not taken; a huge one is held");
    const RtStage* b = odd.find(galaxy::render::rt_stage_hash("B"));
    ok &= expect(b != nullptr && b->sun == RtStage::Sun::Auto &&
                     near(b->ambient_gain, galaxy::render::kRtStageDefaults.ambient_gain),
                 "a row of its name alone: the defaults");
    const RtStage* c = odd.find(galaxy::render::rt_stage_hash("C"));
    ok &= expect(c != nullptr && c->sun == RtStage::Sun::Auto, "two numbers are no direction");
    // No header it knows, nothing at all, a header alone.
    galaxy::render::RtStageTable none;
    ok &= expect(none.read("name\tsun\nA\tkey\n") == 0u && none.read("") == 0u && none.read("stage\n") == 0u,
                 "no stage column, no text, a header alone: no rows");
    galaxy::render::RtEffectTable effects;
    ok &= expect(effects.read("effect\tluminance\nA\tnan\nB\tinf\nC\t-5\nD\t1e9\n") == 2u, "non-finite luminances skipped");
    const auto* low = effects.find(galaxy::render::rt_effect_hash("C"));
    const auto* high = effects.find(galaxy::render::rt_effect_hash("D"));
    ok &= expect(low != nullptr && low->luminance == 0.0f && high != nullptr && near(high->luminance, 100000.0f),
                 "a luminance held to 0 to 100,000");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= stage_table();
    ok &= effect_table();
    ok &= broken_tables();
    if (!ok) {
        return 1;
    }
    std::cout << "rt tables tests passed\n";
    return 0;
}
