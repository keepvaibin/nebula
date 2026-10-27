// SPDX-License-Identifier: GPL-3.0-only
// The material and stage sun tables' parsing (rt_materials.h) and the texture-pack hash against XXH64 test vectors.
#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/rt_surfaces.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {

using galaxy::gx::kRtCrystalTintKept;
using galaxy::gx::kRtIceTintKept;
using galaxy::gx::RtEmission;
using galaxy::gx::RtMaterial;
using galaxy::gx::RtMaterialTable;
using galaxy::gx::RtSurfaceClass;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

std::uint64_t hash_of(const char* text) {
    return galaxy::gx::texture_pack_hash(reinterpret_cast<const std::uint8_t*>(text), std::strlen(text));
}

const RtMaterial* find(const RtMaterialTable& table, std::uint64_t hash) {
    return table.material(table.index(hash));
}

bool xxh64_vectors() {
    bool ok = true;
    ok &= expect(hash_of("") == 0xEF46DB3751D8E999ull, "XXH64 of nothing");
    ok &= expect(hash_of("abc") == 0x44BC2CF5AD770999ull, "XXH64 of 3 bytes");
    ok &= expect(hash_of("Nobody inspects the spammish repetition") == 0xFBCEA83C8A378BF1ull,
                 "XXH64 of 39 bytes (the 32-byte stripes)");
    return ok;
}

bool columns_by_header() {
    std::istringstream file(
        "# surfaces\n"
        "notes\tshadow\tmetallic\troughness\tclass\thash\n"
        "a pipe\t1\t1\t0.35\tmetal\ttex1_64x64_00000000000000a1_14\n"
        "glass\t0\t0\t0.05\tglass\t00000000000000A2\n"
        "no hash\t1\t0\t0.5\tdiffuse\ttex1_8x8_xyz_14\n"
        "sky\t0\t0\t1\tsky\ttex1_8x8_00000000000000a3_14\n");
    RtMaterialTable table;
    bool ok = expect(table.read(file) == 3u, "three rows read");
    ok &= expect(table.skipped() == 1u, "the row without a hash is skipped");
    const RtMaterial* pipe = find(table, 0xA1u);
    ok &= expect(pipe != nullptr && pipe->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Metal) &&
                     near(pipe->roughness, 0.35f) && near(pipe->metallic, 1.0f) && pipe->shadow,
                 "metal row by its pack name");
    const RtMaterial* glass = find(table, 0xA2u);
    ok &= expect(glass != nullptr && glass->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Crystal) &&
                     !glass->shadow && glass->emission == RtEmission::None && glass->intensity == 0.0f,
                 "glass row by its hex alone");
    const RtMaterial* sky = find(table, 0xA3u);
    ok &= expect(sky != nullptr && sky->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Unlit),
                 "sky is unlit");
    ok &= expect(table.index(0xA4u) == 0u && find(table, 0xA4u) == nullptr, "unknown texture");
    return ok;
}

bool lights_fill_their_fields() {
    std::istringstream surfaces(
        "hash\tgalaxy\tobject\tclass\troughness\tmetallic\tr\tg\tb\tintensity\trange\tpulse\tshadow\tnotes\n"
        "tex1_32x32_00000000000000b1_14\t-\tTico\tglossy\t0.4\t0\t\t\t\t\t\t\t1\tluma body\n"
        "tex1_32x32_00000000000000b2_14\t-\tLight\tparticle+helper\t1\t0\t\t\t\t\t\t\t0\tbeam\n");
    std::istringstream lights(
        "hash\tclass\tr\tg\tb\tintensity\trange\tpulse\tcone\n"
        "tex1_32x32_00000000000000b1_14\temitter+tint\t\t\t\t\t\t0.5\t\n"
        "tex1_32x32_00000000000000b2_14\tskip+hide+unlit-only\t\t\t\t\t\t\t\n"
        "tex1_32x32_00000000000000b5_14\tcrystal+traced+solid+ior2.65\t\t\t\t\t\t\t\n"
        "tex1_32x32_00000000000000b6_14\tcrystal+traced+solid+ior9\t\t\t\t\t\t\t\n"
        "tex1_256x256_00000000000000b3_14\temitter\t1\t0.95\t0.8\t3\t2500\t\t28\n"
        "tex1_256x256_m_00000000000000b4_14\temissive-area\t1\t0.4\t0.1\t1\t\t\t\n");
    RtMaterialTable table;
    table.read(surfaces);
    table.read(lights);
    bool ok = expect(table.size() == 6u, "six textures");
    const RtMaterial* moissanite = find(table, 0xB5u);
    ok &= expect(moissanite != nullptr && moissanite->solid && moissanite->traced && near(moissanite->ior, 2.65f),
                 "a solid's index of refraction");
    const RtMaterial* beyond = find(table, 0xB6u);
    ok &= expect(beyond != nullptr && beyond->solid && beyond->ior == 0.0f, "an index past 5 is not taken");
    const RtMaterial* luma = find(table, 0xB1u);
    ok &= expect(luma != nullptr && luma->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Glossy) &&
                     near(luma->roughness, 0.4f) && luma->emission == RtEmission::Point && luma->tint &&
                     near(luma->intensity, 1.0f) && near(luma->pulse, 0.5f) && luma->cone == 0.0f,
                 "a lights row keeps the surface it leaves empty; emitters default to a Luma's intensity");
    const RtMaterial* beam = find(table, 0xB2u);
    ok &= expect(beam != nullptr && beam->helper && beam->particle && beam->no_light && beam->hide && beam->unlit_only &&
                     !beam->shadow,
                 "tokens add up across files");
    const RtMaterial* lamp = find(table, 0xB3u);
    ok &= expect(lamp != nullptr && lamp->emission == RtEmission::Point && near(lamp->color[1], 0.95f) &&
                     near(lamp->intensity, 3.0f) && near(lamp->range, 2500.0f) && near(lamp->cone, 28.0f) &&
                     lamp->surface_class == 0xFFu,
                 "a spot light without a surface row");
    const RtMaterial* lava = find(table, 0xB4u);
    ok &= expect(lava != nullptr && lava->emission == RtEmission::Area && near(lava->color[2], 0.1f) &&
                     near(lava->intensity, 1.0f),
                 "an area light");
    return ok;
}

bool values_are_clamped() {
    std::istringstream file(
        "hash\tclass\troughness\tmetallic\tr\tintensity\tcone\n"
        "00000000000000c1\temitter\t1.5\t-1\t2\t1000\t120\n"
        "00000000000000c2\temitter\tx\tnan\t\t0\t\n");
    RtMaterialTable table;
    table.read(file);
    const RtMaterial* a = find(table, 0xC1u);
    bool ok = expect(a != nullptr && near(a->roughness, 1.0f) && near(a->metallic, 0.0f) && near(a->color[0], 1.0f) &&
                         near(a->intensity, 100.0f) && near(a->cone, 90.0f),
                     "out-of-range values clamp");
    const RtMaterial* b = find(table, 0xC2u);
    ok &= expect(b != nullptr && b->roughness < 0.0f && b->metallic < 0.0f && b->intensity == 0.0f,
                 "unreadable numbers leave the field as it was; an explicit 0 stays 0");
    return ok;
}

bool identities_by_stage() {
    std::istringstream file(
        "hash\tclass\tr\tg\tb\tintensity\tstage\tmaterial\n"
        "effect\t\t\t\t\t\t\t\n"
        "effect\temitter+tint\t\t\t\t2\t\tEffect/Fire00\n"
        "effect\tparticle+emitter+recolour\t1\t0.9\t0.7\t4\tAstroGalaxy\tEffect/CommonGlow00\n");
    RtMaterialTable table;
    table.read(file);
    const std::uint32_t fire = galaxy::gx::rt_material_identity("Effect/Fire00");
    const std::uint32_t glow = galaxy::gx::rt_material_identity("Effect/CommonGlow00");
    const RtMaterial* everywhere = table.material(table.identity_index(fire));
    bool ok = expect(everywhere != nullptr && everywhere->tint && near(everywhere->intensity, 2.0f),
                     "an identity row without a stage holds everywhere");
    ok &= expect(table.identity_index(glow) == 0u, "a stage's identity row is not everywhere's");
    const auto there = table.stage_identities("AstroGalaxy");
    const RtMaterial* lamp = there.size() == 1u && there[0].first == glow ? table.material(there[0].second) : nullptr;
    ok &= expect(lamp != nullptr && lamp->recolour && lamp->particle && near(lamp->color[2], 0.7f) &&
                     near(lamp->intensity, 4.0f),
                 "the stage's identity row");
    ok &= expect(table.stage_identities("HeavenlyBeachGalaxy").empty(), "another stage has none");
    ok &= expect(table.stage_materials("AstroGalaxy").empty(), "an identity row is no texture's stage row");
    return ok;
}

bool luminous_effects() {
    std::istringstream file(
        "hash\tclass\n"
        "00000000000000e1\tparticle+luminous\n"
        "00000000000000e2\tparticle\n");
    RtMaterialTable table;
    table.read(file);
    const RtMaterial* sparkle = find(table, 0xE1u);
    const RtMaterial* smoke = find(table, 0xE2u);
    bool ok = expect(sparkle != nullptr && sparkle->particle && sparkle->luminous &&
                         sparkle->emission == RtEmission::None,
                     "a luminous effect is light, not a light source");
    ok &= expect(smoke != nullptr && smoke->particle && !smoke->luminous, "other effects take the traced light");
    return ok;
}

bool clear_tints() {
    std::istringstream file(
        "hash\tclass\n"
        "00000000000000f1\tice+helper+ior1.31\n"
        "00000000000000f2\tparticle+luminous+icetint\n"
        "00000000000000f3\tparticle+luminous+crystaltint\n"
        "00000000000000f4\tcrystal+helper\n");
    RtMaterialTable table;
    table.read(file);
    const RtMaterial* ice = find(table, 0xF1u);
    const RtMaterial* cracks = find(table, 0xF2u);
    const RtMaterial* crystal_cracks = find(table, 0xF3u);
    const RtMaterial* crystal = find(table, 0xF4u);
    bool ok = expect(ice != nullptr && ice->ice && near(ice->tint_kept, kRtIceTintKept), "ice keeps its share of tint");
    ok &= expect(cracks != nullptr && !cracks->ice && cracks->particle && near(cracks->tint_kept, kRtIceTintKept),
                 "an ice layer of another class keeps it, not ice");
    ok &= expect(crystal_cracks != nullptr && near(crystal_cracks->tint_kept, kRtCrystalTintKept),
                 "a crystal's layer keeps the crystals' share");
    ok &= expect(crystal != nullptr && near(crystal->tint_kept, 1.0f), "other materials keep their tint");
    return ok;
}

bool glow_apart_from_light() {
    std::istringstream file(
        "hash\tclass\tr\tg\tb\tintensity\tglow\n"
        "00000000000000d1\temissive-area\t1\t0.6\t0.2\t1\t0.3\n"
        "00000000000000d2\temitter\t1\t1\t1\t2\t\n"
        "00000000000000d3\temitter\t1\t1\t1\t2\t40\n");
    RtMaterialTable table;
    table.read(file);
    const RtMaterial* core = find(table, 0xD1u);
    bool ok = expect(core != nullptr && near(core->intensity, 1.0f) && near(core->glow, 0.3f),
                     "glow apart from the intensity");
    const RtMaterial* star = find(table, 0xD2u);
    ok &= expect(star != nullptr && star->glow < 0.0f, "no glow: the intensity's");
    const RtMaterial* big = find(table, 0xD3u);
    ok &= expect(big != nullptr && near(big->glow, 16.0f), "glow clamps");
    return ok;
}

bool stage_suns() {
    std::istringstream file(
        "stage\tsource\tx\ty\tz\tr\tg\tb\tstrength\tsize\tshafts\tnotes\n"
        "EggStarGalaxy\tgame\t\t\t\t1\t0.95\t0.85\t1.2\t3\t0.8\tBrightSun\n"
        "HoneyBeeKingdomGalaxy\ttable\t0\t3\t4\t\t\t\t\t\t\tsky sun\n"
        "BadGalaxy\ttable\t0\t0\t0\t\t\t\t9\t\t\tno direction\n"
        "HellProminenceGalaxy\tnone\t\t\t\t\t\t\t\t\t\tlava\n"
        "PeachCastleGardenGalaxy\tmoon\t3\t4\t0\t0.78\t0.85\t1\t0.4\t1.5\t\tnight\n"
        "MoonlessGalaxy\tmoon\t\t\t\t\t\t\t\t\t\tno direction\n");
    galaxy::gx::RtStageSunTable table;
    bool ok = expect(table.read(file) == 6u, "six suns");
    using Source = galaxy::gx::RtStageSun::Source;
    const auto* egg = table.find("EggStarGalaxy");
    ok &= expect(egg != nullptr && egg->source == Source::Game && egg->has_color && near(egg->color[2], 0.85f) &&
                     near(egg->strength, 1.2f) && near(egg->size, 3.0f) && near(egg->shafts, 0.8f),
                 "a game sun with its look");
    const auto* honey = table.find("HoneyBeeKingdomGalaxy");
    ok &= expect(honey != nullptr && honey->source == Source::Table && near(honey->direction[1], 0.6f) &&
                     near(honey->direction[2], 0.8f) && !honey->has_color && honey->size < 0.0f,
                 "a table sun, its direction normalised");
    const auto* bad = table.find("BadGalaxy");
    ok &= expect(bad != nullptr && bad->source == Source::Game && near(bad->strength, 4.0f),
                 "no direction: the game's sun; strength clamped");
    const auto* lava = table.find("HellProminenceGalaxy");
    ok &= expect(lava != nullptr && lava->source == Source::None, "no sun");
    const auto* moon = table.find("PeachCastleGardenGalaxy");
    ok &= expect(moon != nullptr && moon->source == Source::Moon && near(moon->direction[0], 0.6f) &&
                     near(moon->direction[1], 0.8f) && near(moon->strength, 0.4f) && near(moon->size, 1.5f),
                 "a moon, its direction normalised");
    const auto* moonless = table.find("MoonlessGalaxy");
    ok &= expect(moonless != nullptr && moonless->source == Source::Game, "a moon without a direction: the game's");
    ok &= expect(table.find("Missing") == nullptr, "no row");
    ok &= expect(egg->day < 0.0f, "no day column: by the sky");
    std::istringstream with_day(
        "stage\tsource\tday\tnotes\n"
        "BattleShipGalaxy\tgame\t0\tspace\n"
        "EggStarGalaxy\tgame\t1.5\tday\n"
        "PhantomGalaxy\tgame\t\tby sky\n");
    galaxy::gx::RtStageSunTable days;
    days.read(with_day);
    const auto* space = days.find("BattleShipGalaxy");
    const auto* day = days.find("EggStarGalaxy");
    const auto* by_sky = days.find("PhantomGalaxy");
    ok &= expect(space != nullptr && space->day == 0.0f, "day 0: the night model");
    ok &= expect(day != nullptr && near(day->day, 1.0f), "day clamps to 1");
    ok &= expect(by_sky != nullptr && by_sky->day < 0.0f, "empty day: by the sky");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= xxh64_vectors();
    ok &= columns_by_header();
    ok &= lights_fill_their_fields();
    ok &= values_are_clamped();
    ok &= glow_apart_from_light();
    ok &= luminous_effects();
    ok &= clear_tints();
    ok &= identities_by_stage();
    ok &= stage_suns();
    if (!ok) {
        return 1;
    }
    std::cout << "rt materials tests passed\n";
    return 0;
}
