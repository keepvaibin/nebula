// SPDX-License-Identifier: GPL-3.0-only
// The ray tracer's material and stage sun tables (rt_materials.h) and the texture-pack hash.
#include "galaxy/gx/rt_materials.h"

#include "galaxy/gx/rt_surfaces.h"
#include "galaxy/platform_paths.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

std::uint64_t texture_pack_hash(const std::uint8_t* p, std::size_t n) noexcept {
    constexpr std::uint64_t k1 = 0x9E3779B185EBCA87ull, k2 = 0xC2B2AE3D27D4EB4Full, k3 = 0x165667B19E3779F9ull,
                            k4 = 0x85EBCA77C2B2AE63ull, k5 = 0x27D4EB2F165667C5ull;
    const auto read64 = [](const std::uint8_t* q) {
        std::uint64_t v;
        std::memcpy(&v, q, 8u);
        return v;
    };
    const auto round = [](std::uint64_t acc, std::uint64_t lane) { return std::rotl(acc + lane * k2, 31) * k1; };
    const std::uint8_t* const end = p + n;
    std::uint64_t acc = 0u;
    if (n >= 32u) {
        std::uint64_t v1 = k1 + k2, v2 = k2, v3 = 0u, v4 = 0u - k1;
        for (; p + 32u <= end; p += 32u) {
            v1 = round(v1, read64(p));
            v2 = round(v2, read64(p + 8u));
            v3 = round(v3, read64(p + 16u));
            v4 = round(v4, read64(p + 24u));
        }
        acc = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        for (const std::uint64_t v : {v1, v2, v3, v4}) {
            acc = (acc ^ round(0u, v)) * k1 + k4;
        }
    } else {
        acc = k5;
    }
    acc += n;
    for (; p + 8u <= end; p += 8u) {
        acc = std::rotl(acc ^ round(0u, read64(p)), 27) * k1 + k4;
    }
    if (p + 4u <= end) {
        std::uint32_t v;
        std::memcpy(&v, p, 4u);
        acc = std::rotl(acc ^ (static_cast<std::uint64_t>(v) * k1), 23) * k2 + k3;
        p += 4u;
    }
    for (; p < end; ++p) {
        acc = std::rotl(acc ^ (static_cast<std::uint64_t>(*p) * k5), 11) * k1;
    }
    acc ^= acc >> 33u;
    acc *= k2;
    acc ^= acc >> 29u;
    acc *= k3;
    return acc ^ (acc >> 32u);
}

namespace {

std::vector<std::string> split(const std::string& line, char by) {
    std::vector<std::string> out;
    std::size_t start = 0u;
    for (;;) {
        const std::size_t at = line.find(by, start);
        out.push_back(line.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) {
            return out;
        }
        start = at + 1u;
    }
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (c == '\r') c = ' ';
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

// The 16 hex digits of a texture-pack name (or the hex alone).
bool parse_hash(const std::string& text, std::uint64_t& hash) {
    if (lower(text) == "untextured") {
        hash = kRtUntexturedHash;
        return true;
    }
    if (lower(text) == "effect") {
        hash = kRtEffectHash;
        return true;
    }
    for (const std::string& part : split(text, '_')) {
        const std::string token = lower(part);
        if (token.size() == 16u &&
            std::all_of(token.begin(), token.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
            hash = std::strtoull(token.c_str(), nullptr, 16);
            return true;
        }
    }
    return false;
}

// The column's number; false when the row leaves it empty (or holds no number there).
bool number(const std::vector<std::string>& row, int column, float& out) {
    if (column < 0 || static_cast<std::size_t>(column) >= row.size()) {
        return false;
    }
    const std::string text = lower(row[static_cast<std::size_t>(column)]);
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(v)) {
        return false;
    }
    out = v;
    return true;
}

// The class tokens over `m`: a surface token sets the class, the others add their property.
void apply_class(const std::string& text, RtMaterial& m) {
    const auto set = [&m](RtSurfaceClass c) { m.surface_class = static_cast<std::uint8_t>(c); };
    for (const std::string& part : split(lower(text), '+')) {
        const std::string token = lower(part);
        if (token == "own") m = RtMaterial{};
        else if (token == "emitter" || token == "light") m.emission = RtEmission::Point;
        else if (token == "emissive-area" || token == "area") m.emission = RtEmission::Area;
        else if (token == "skip" || token == "none") m.no_light = true;
        else if (token == "helper") m.helper = true;
        else if (token == "tint") m.tint = true;
        else if (token == "hide") m.hide = true;
        else if (token == "unlit-only") m.unlit_only = true;
        else if (token == "particle") m.particle = true;
        // An ice layer of another class (the rink's additive cracks), a crystal's (a cage's cracks): their tint cleared
        // as ice's and the crystals', their class kept.
        else if (token == "icetint") m.tint_kept = std::min(m.tint_kept, kRtIceTintKept);
        else if (token == "crystaltint") m.tint_kept = std::min(m.tint_kept, kRtCrystalTintKept);
        else if (token == "luminous") m.luminous = true;
        else if (token == "lens") m.lens = true;
        else if (token == "mirror") m.mirror = true;
        else if (token == "star") m.star = true;
        else if (token == "strip") m.strip = true;
        else if (token == "painted") m.painted = true;
        else if (token == "delight") m.delight = true;
        else if (token == "traced") m.traced = true;
        else if (token == "solid") m.solid = true;
        else if (token.rfind("ior", 0) == 0 && token.size() > 3u) {
            char* end = nullptr;
            const float v = std::strtof(token.c_str() + 3, &end);
            if (end != token.c_str() + 3 && std::isfinite(v) && v >= 1.0f && v <= 5.0f) m.ior = v;
        }
        else if (token == "recolour") m.recolour = true;
        else if (token == "near-glow") m.near_glow = true;
        else if (token == "volume") m.volume = true;
        else if (token == "unpainted") m.white = 0.0f;
        else if (token.rfind("white", 0) == 0 && token.size() > 5u) {
            char* end = nullptr;
            const float v = std::strtof(token.c_str() + 5, &end);
            if (end != token.c_str() + 5 && std::isfinite(v)) m.white = std::clamp(v, 0.0f, 1.0f);
        }
        else if (token == "mist") m.mist = true;
        else if (token == "seen") {
            m.seen = true;
            if (m.emission == RtEmission::None) m.emission = RtEmission::Point;
        }
        else if (token == "waterfall") {
            m.waterfall = true;
            set(RtSurfaceClass::Water);
            m.relief = 0.0f;
        }
        else if (token == "metal") { set(RtSurfaceClass::Metal); m.relief = 0.0f; }
        else if (token == "glossy") { set(RtSurfaceClass::Glossy); m.relief = 0.0f; }
        else if (token == "glass" || token == "crystal" || token == "glass/crystal") {
            set(RtSurfaceClass::Crystal);
            m.relief = 0.0f;
        } else if (token == "ice" || token == "water") {
            set(RtSurfaceClass::Water);
            m.relief = 0.0f;
            m.ice = m.ice || token == "ice";
            if (token == "ice") {
                m.tint_kept = std::min(m.tint_kept, kRtIceTintKept);
            }
        } else if (token == "diffuse" || token == "foliage" || token == "fabric" || token == "skin") {
            set(RtSurfaceClass::Diffuse);
            m.relief = token == "diffuse" ? 1.0f : token == "foliage" ? 0.7f : 0.0f;
            m.matte = token == "foliage" || token == "fabric";
            m.foliage = token == "foliage";
            // Skin carries the features (eyes, mouths, brows) the de-lighting would flatten.
            m.painted = m.painted || token == "skin";
        } else if (token == "unlit" || token == "sky") {
            set(RtSurfaceClass::Unlit);
            m.relief = 0.0f;
            m.sky = token == "sky";
        }
    }
}

std::vector<std::string> table_paths() {
    char value[4096]{};
    std::size_t length = 0u;
    std::vector<std::string> paths;
    if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_MATERIALS_FILE") == 0 && length > 1u) {
        for (const std::string& path : split(value, ';')) {
            if (!path.empty()) paths.push_back(path);
        }
        return paths;
    }
    const std::filesystem::path dir = program_directory();
    return {(dir / "rt-materials.tsv").string(), (dir / "rt-materials-lights.tsv").string()};
}

RtMaterialTable load() {
    RtMaterialTable table;
    for (const std::string& path : table_paths()) {
        std::ifstream file(path);
        if (!file) {
            continue;
        }
        const std::size_t rows = table.read(file);
        std::cerr << "[rt-materials] file=" << path << " rows=" << rows << " textures=" << table.size()
                  << " skipped=" << table.skipped() << '\n';
    }
    return table;
}

const RtMaterialTable& table() {
    static const RtMaterialTable loaded = load();
    return loaded;
}

const RtStageSunTable& suns() {
    static const RtStageSunTable loaded = [] {
        RtStageSunTable t;
        char value[4096]{};
        std::size_t length = 0u;
        std::string path;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_SUNS_FILE") == 0 && length > 1u) {
            path = value;
        } else {
            path = (program_directory() / "rt-suns.tsv").string();
        }
        std::ifstream file(path);
        if (file) {
            const std::size_t rows = t.read(file);
            std::cerr << "[rt-suns] file=" << path << " rows=" << rows << '\n';
        }
        return t;
    }();
    return loaded;
}

}  // namespace

std::size_t RtMaterialTable::read(std::istream& in) {
    std::string line;
    bool header_read = false;
    int hash_col = -1, class_col = -1, rough_col = -1, metal_col = -1, r_col = -1, g_col = -1, b_col = -1,
        intensity_col = -1, range_col = -1, pulse_col = -1, cone_col = -1, shadow_col = -1, glow_col = -1,
        stage_col = -1, region_col = -1, object_col = -1, material_col = -1, aeration_col = -1;
    std::size_t rows = 0u;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> row = split(line, '\t');
        if (!header_read) {
            header_read = true;
            for (std::size_t i = 0; i < row.size(); ++i) {
                const std::string name = lower(row[i]);
                int* const slot = name == "hash" ? &hash_col : name == "class" ? &class_col
                                : name == "roughness" ? &rough_col : name == "metallic" ? &metal_col
                                : name == "r" ? &r_col : name == "g" ? &g_col : name == "b" ? &b_col
                                : name == "intensity" ? &intensity_col : name == "range" ? &range_col
                                : name == "pulse" ? &pulse_col : name == "cone" ? &cone_col
                                : name == "shadow" ? &shadow_col : name == "glow" ? &glow_col
                                : name == "stage" ? &stage_col : name == "region" ? &region_col
                                : name == "object" ? &object_col : name == "material" ? &material_col
                                : name == "aeration" ? &aeration_col : nullptr;
                if (slot != nullptr) *slot = static_cast<int>(i);
            }
            continue;
        }
        std::uint64_t hash = 0u;
        if (hash_col < 0 || static_cast<std::size_t>(hash_col) >= row.size() || !parse_hash(row[hash_col], hash)) {
            ++skipped_;
            continue;
        }
        auto it = by_hash_.find(hash);
        if (it == by_hash_.end()) {
            if (materials_.size() >= 0xFFFEu) {
                ++skipped_;
                continue;
            }
            materials_.emplace_back();
            it = by_hash_.emplace(hash, static_cast<std::uint16_t>(materials_.size())).first;
        }
        std::string stage = stage_col >= 0 && static_cast<std::size_t>(stage_col) < row.size()
                                ? row[static_cast<std::size_t>(stage_col)]
                                : std::string();
        while (!stage.empty() && (stage.back() == '\r' || stage.back() == ' ')) stage.pop_back();
        std::string identity = material_col >= 0 && static_cast<std::size_t>(material_col) < row.size()
                                   ? row[static_cast<std::size_t>(material_col)]
                                   : std::string();
        while (!identity.empty() && (identity.back() == '\r' || identity.back() == ' ')) identity.pop_back();
        RtMaterial* target = &materials_[it->second - 1u];
        if (!identity.empty() && identity != "-") {
            // The texture's material so far, with this row on top, for the named models' material.
            if (materials_.size() >= 0xFFFEu) {
                ++skipped_;
                continue;
            }
            materials_.push_back(materials_[it->second - 1u]);
            const auto index = static_cast<std::uint16_t>(materials_.size());
            for (const std::string& name : split(identity, ' ')) {
                if (!name.empty()) {
                    if (stage.empty()) {
                        by_identity_[rt_material_identity(name)] = index;
                    } else {
                        stage_identities_.push_back({stage, rt_material_identity(name), index});
                    }
                    if (rt_log_glows()) {
                        std::fprintf(stderr, "[rt-identity] row %s = %08X -> material %u%s%s\n", name.c_str(),
                                     rt_material_identity(name), static_cast<unsigned>(index),
                                     stage.empty() ? "" : " on ", stage.c_str());
                    }
                }
            }
            target = &materials_.back();
            stage.clear();
        } else if (!stage.empty()) {
            // The texture's material so far, with this row on top, for that stage.
            stage_rows_.push_back({stage, it->second, materials_[it->second - 1u]});
            target = &stage_rows_.back().material;
        }
        RtMaterial& m = *target;
        const RtEmission before = m.emission;
        if (stage.empty() && object_col >= 0 && static_cast<std::size_t>(object_col) < row.size()) {
            // "Model/texture" or "ModelA ModelB": each model this row's texture belongs to.
            for (const std::string& name : split(row[static_cast<std::size_t>(object_col)], ' ')) {
                const std::string model = lower(name.substr(0u, name.find('/')));
                if (!model.empty() && model != "-") {
                    std::vector<std::uint16_t>& indices = model_rows_[model];
                    if (std::find(indices.begin(), indices.end(), it->second) == indices.end()) {
                        indices.push_back(it->second);
                    }
                }
            }
        }
        if (class_col >= 0 && static_cast<std::size_t>(class_col) < row.size()) {
            apply_class(row[static_cast<std::size_t>(class_col)], m);
        }
        float v = 0.0f;
        if (number(row, rough_col, v)) m.roughness = std::clamp(v, 0.0f, 1.0f);
        if (number(row, metal_col, v)) m.metallic = std::clamp(v, 0.0f, 1.0f);
        if (number(row, r_col, v)) m.color[0] = std::clamp(v, 0.0f, 1.0f);
        if (number(row, g_col, v)) m.color[1] = std::clamp(v, 0.0f, 1.0f);
        if (number(row, b_col, v)) m.color[2] = std::clamp(v, 0.0f, 1.0f);
        if (number(row, intensity_col, v)) {
            m.intensity = std::clamp(v, 0.0f, 100.0f);
        } else if (before == RtEmission::None && m.emission != RtEmission::None && m.intensity <= 0.0f) {
            m.intensity = 1.0f;
        }
        if (number(row, range_col, v)) m.range = std::clamp(v, 0.0f, 1.0e5f);
        if (number(row, aeration_col, v)) m.aeration = std::clamp(v, 0.0f, 1.0f);
        if (number(row, pulse_col, v)) m.pulse = std::clamp(v, 0.0f, 30.0f);
        if (number(row, cone_col, v)) m.cone = std::clamp(v, 0.0f, 90.0f);
        if (number(row, shadow_col, v)) m.shadow = v != 0.0f;
        if (number(row, glow_col, v)) m.glow = std::clamp(v, 0.0f, 16.0f);
        if (region_col >= 0 && static_cast<std::size_t>(region_col) < row.size()) {
            // u0,v0,u1,v1
            float r[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            const char* cursor = row[static_cast<std::size_t>(region_col)].c_str();
            int n = 0;
            for (; n < 4; ++n) {
                char* end = nullptr;
                r[n] = std::strtof(cursor, &end);
                if (end == cursor) {
                    break;
                }
                cursor = *end == ',' ? end + 1 : end;
            }
            if (n == 4) {
                for (int k = 0; k < 4; ++k) {
                    m.region[k] = std::clamp(r[k], 0.0f, 1.0f);
                }
            }
        }
        ++rows;
    }
    return rows;
}

std::uint16_t RtMaterialTable::index(std::uint64_t hash) const noexcept {
    const auto it = by_hash_.find(hash);
    return it != by_hash_.end() ? it->second : 0u;
}

std::uint16_t RtMaterialTable::identity_index(std::uint32_t identity) const noexcept {
    const auto it = by_identity_.find(identity);
    return it != by_identity_.end() ? it->second : 0u;
}

const RtMaterial* RtMaterialTable::material(std::uint16_t index) const noexcept {
    return index != 0u && index <= materials_.size() ? &materials_[index - 1u] : nullptr;
}

float RtMaterialTable::model_light(std::string_view model) const {
    const auto it = model_rows_.find(lower(std::string(model)));
    if (it == model_rows_.end()) {
        return 0.0f;
    }
    float light = 0.0f;
    for (const std::uint16_t index : it->second) {
        const RtMaterial* const m = material(index);
        // As the physical emitters take it (rt_lighting.hlsl EmitTriangleCS): its colour, at most white, times the
        // table's intensity where that passes 1.
        if (m != nullptr && m->emission != RtEmission::None && !m->no_light && !m->hide) {
            light = std::max(light, std::max(m->intensity, 1.0f));
        }
    }
    return light;
}

std::vector<std::pair<std::uint16_t, const RtMaterial*>> RtMaterialTable::stage_materials(
    std::string_view stage) const {
    std::vector<std::pair<std::uint16_t, const RtMaterial*>> out;
    for (const StageRow& row : stage_rows_) {
        if (row.stage == stage) {
            out.emplace_back(row.index, &row.material);
        }
    }
    return out;
}

std::vector<std::pair<std::uint32_t, std::uint16_t>> RtMaterialTable::stage_identities(std::string_view stage) const {
    std::vector<std::pair<std::uint32_t, std::uint16_t>> out;
    for (const StageIdentity& row : stage_identities_) {
        if (row.stage == stage) {
            out.emplace_back(row.identity, row.index);
        }
    }
    return out;
}

std::size_t RtStageSunTable::read(std::istream& in) {
    std::string line;
    bool header_read = false;
    int stage_col = -1, source_col = -1, x_col = -1, y_col = -1, z_col = -1, r_col = -1, g_col = -1, b_col = -1,
        strength_col = -1, size_col = -1, shafts_col = -1, day_col = -1, ambient_col = -1, core_col = -1;
    std::size_t rows = 0u;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> row = split(line, '\t');
        if (!header_read) {
            header_read = true;
            for (std::size_t i = 0; i < row.size(); ++i) {
                const std::string name = lower(row[i]);
                int* const slot = name == "stage" ? &stage_col : name == "source" ? &source_col
                                : name == "x" ? &x_col : name == "y" ? &y_col : name == "z" ? &z_col
                                : name == "r" ? &r_col : name == "g" ? &g_col : name == "b" ? &b_col
                                : name == "strength" ? &strength_col : name == "size" ? &size_col
                                : name == "shafts" ? &shafts_col : name == "day" ? &day_col
                                : name == "ambient" ? &ambient_col : name == "core" ? &core_col : nullptr;
                if (slot != nullptr) *slot = static_cast<int>(i);
            }
            continue;
        }
        if (stage_col < 0 || static_cast<std::size_t>(stage_col) >= row.size()) {
            continue;
        }
        std::string stage = row[static_cast<std::size_t>(stage_col)];
        while (!stage.empty() && (stage.back() == '\r' || stage.back() == ' ')) stage.pop_back();
        if (stage.empty()) {
            continue;
        }
        RtStageSun sun{};
        const std::string source = source_col >= 0 && static_cast<std::size_t>(source_col) < row.size()
                                       ? lower(row[static_cast<std::size_t>(source_col)])
                                       : std::string();
        float d[3]{};
        const bool direction = number(row, x_col, d[0]) && number(row, y_col, d[1]) && number(row, z_col, d[2]);
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if ((source == "table" || source == "moon") && direction && length > 1e-4f) {
            sun.source = source == "moon" ? RtStageSun::Source::Moon : RtStageSun::Source::Table;
            for (unsigned i = 0; i < 3u; ++i) sun.direction[i] = d[i] / length;
        } else if (source == "none") {
            sun.source = RtStageSun::Source::None;
        } else if (source == "core") {
            sun.source = RtStageSun::Source::Core;
        }
        float c[3]{};
        if (number(row, r_col, c[0]) && number(row, g_col, c[1]) && number(row, b_col, c[2])) {
            sun.has_color = true;
            for (unsigned i = 0; i < 3u; ++i) sun.color[i] = std::clamp(c[i], 0.0f, 1.0f);
        }
        float v = 0.0f;
        if (number(row, strength_col, v)) sun.strength = std::clamp(v, 0.0f, 4.0f);
        if (number(row, size_col, v)) sun.size = std::clamp(v, 0.0f, 30.0f);
        if (number(row, shafts_col, v)) sun.shafts = std::clamp(v, 0.0f, 4.0f);
        if (number(row, day_col, v)) sun.day = std::clamp(v, 0.0f, 1.0f);
        if (number(row, ambient_col, v)) sun.ambient = std::clamp(v, 0.0f, 2.0f);
        if (number(row, core_col, v)) sun.core = std::clamp(v, 0.0f, 1000.0f);
        suns_[stage] = sun;
        ++rows;
    }
    return rows;
}

const RtStageSun* RtStageSunTable::find(std::string_view stage) const noexcept {
    const auto it = suns_.find(std::string(stage));
    return it != suns_.end() ? &it->second : nullptr;
}

const RtStageSun* rt_stage_sun(std::string_view stage) noexcept {
    return suns().find(stage);
}

bool rt_materials_loaded() noexcept {
    return !table().empty();
}

std::uint16_t rt_material_index(std::uint64_t hash) noexcept {
    return table().index(hash);
}

std::uint32_t rt_material_identity(std::string_view name) noexcept {
    // "Model.bdl/material": the model's extension is not part of it.
    const std::size_t slash = name.find('/');
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (i < slash && name[i] == '.') {
            i = slash - 1u;
            continue;
        }
        const char c = name[i];
        hash = (hash ^ static_cast<std::uint8_t>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c)) * 16777619u;
    }
    return hash;
}

namespace {
// The loaded stage's own materials (rt_materials_set_stage), by material index.
std::vector<std::pair<std::uint16_t, const RtMaterial*>> g_stage_materials;
// Its identity rows: per identity, its material index.
std::vector<std::pair<std::uint32_t, std::uint16_t>> g_stage_identities;
// Its untextured surfaces' material (0: none).
std::uint16_t g_stage_untextured = 0u;
}  // namespace

namespace {
bool log_glows() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_RT_LOG_GLOWS") == 0 && length > 1u && value[0] == '1';
    }();
    return enabled;
}
struct TextureName {
    std::uint64_t pack;
    std::uint32_t width, height, format;
};
std::unordered_map<std::uint32_t, TextureName> g_texture_names;
std::unordered_map<std::uint32_t, bool> g_glows_logged;
}  // namespace

void note_rt_texture(std::uint32_t guest_addr, std::uint64_t pack, std::uint32_t width, std::uint32_t height,
                     std::uint32_t format) {
    if (log_glows() && g_texture_names.size() < 65536u) {
        g_texture_names[guest_addr] = {pack, width, height, format};
    }
}

bool rt_log_glows() noexcept {
    return log_glows();
}

namespace {

// Once per texture and kind (the key: its 32-byte-aligned guest address, bit 0 the kind).
void note_unnamed(const char* kind, std::uint32_t guest_addr, std::uint32_t key) {
    if (!log_glows() || !g_glows_logged.emplace(key, true).second) {
        return;
    }
    const auto it = g_texture_names.find(guest_addr);
    if (it == g_texture_names.end()) {
        std::fprintf(stderr, "[rt-%s] unnamed addr=%08X (not hashed)\n", kind, guest_addr);
        return;
    }
    std::fprintf(stderr, "[rt-%s] unnamed addr=%08X tex1_%ux%u_%016llx_%u\n", kind, guest_addr, it->second.width,
                 it->second.height, static_cast<unsigned long long>(it->second.pack), it->second.format);
}

}  // namespace

void note_rt_unnamed_glow(std::uint32_t guest_addr) {
    note_unnamed("glow", guest_addr, guest_addr);
}

void note_rt_unnamed_surface(std::uint32_t guest_addr) {
    note_unnamed("surface", guest_addr, guest_addr | 1u);
}

std::uint16_t rt_stage_untextured_material() noexcept {
    return g_stage_untextured;
}

std::uint16_t rt_material_by_identity(std::uint32_t identity) noexcept {
    if (identity == 0u) {
        return 0u;
    }
    for (const auto& [own, index] : g_stage_identities) {
        if (own == identity) {
            return index;
        }
    }
    return table().identity_index(identity);
}

namespace {
std::atomic<std::uint32_t> g_stage_serial{0u};
}  // namespace

std::uint32_t rt_materials_stage_serial() noexcept {
    return g_stage_serial.load(std::memory_order_relaxed);
}

void rt_materials_set_stage(std::string_view stage) {
    g_stage_serial.fetch_add(1u, std::memory_order_relaxed);
    g_stage_materials = table().stage_materials(stage);
    g_stage_identities = table().stage_identities(stage);
    // Only a stage row names untextured surfaces: elsewhere they stay as the GX state shows them.
    const std::uint16_t untextured = table().index(kRtUntexturedHash);
    g_stage_untextured = 0u;
    for (const auto& [index, material] : g_stage_materials) {
        if (index == untextured) {
            g_stage_untextured = index;
        }
    }
    if (!g_stage_materials.empty() || !g_stage_identities.empty()) {
        std::cerr << "[rt-materials] stage=" << stage << " rows=" << g_stage_materials.size();
        for (const auto& [index, material] : g_stage_materials) {
            std::cerr << " material=" << index;
        }
        for (const auto& [identity, index] : g_stage_identities) {
            std::cerr << " identity=" << std::hex << identity << std::dec << ':' << index;
        }
        std::cerr << '\n';
    }
}

const RtMaterial* rt_material(std::uint16_t index) noexcept {
    for (const auto& [own, material] : g_stage_materials) {
        if (own == index) {
            return material;
        }
    }
    return table().material(index);
}

float rt_light_reach(const char* model, float radius) noexcept {
    if (model == nullptr || !(radius > 0.0f)) {
        return 0.0f;
    }
    // The model's resource name ("Tico.bdl", maybe with a folder): its name alone.
    std::string_view name(model);
    if (const std::size_t slash = name.find_last_of("/\\"); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1u);
    }
    name = name.substr(0u, name.find('.'));
    const float light = table().model_light(name);
    if (!(light > 0.0f)) {
        return 0.0f;
    }
    // A ball of radius r and radiance L lights a surface facing it at distance d with L (r / d)^2 of a white
    // surface's full light. Below 1/4096 of it (under one level of 255 once sRGB-encoded) it shows nothing: it reaches
    // r sqrt(4096 L).
    return std::min(radius * 64.0f * std::sqrt(light), 1.0e5f);
}

}  // namespace galaxy::gx
