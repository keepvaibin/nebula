// SPDX-License-Identifier: GPL-3.0-only
// The ray tracer's material and stage sun tables (rt_materials.h) and the texture-pack hash.
#include "galaxy/render/rt_materials.h"

#include "galaxy/platform_paths.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "galaxy/path_text.h"
#include <vector>

namespace galaxy::render {

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
    const std::filesystem::path dir = program_directory();
    return {galaxy::path_text(dir / "rt-materials.tsv"), galaxy::path_text(dir / "rt-materials-lights.tsv")};
}

RtMaterialTable load() {
    RtMaterialTable table;
    for (const std::string& path : table_paths()) {
        std::ifstream file(galaxy::text_path(path));
        if (!file) {
            continue;
        }
        const std::size_t rows = table.read(file);
        std::cerr << "[rt-materials] file=" << galaxy::path_text(galaxy::text_path(path).filename()) << " rows=" << rows << " textures=" << table.size()
                  << " skipped=" << table.skipped() << '\n';
    }
    return table;
}

const RtMaterialTable& table() {
    static const RtMaterialTable loaded = load();
    return loaded;
}

// The pak's tables (set_rt_tables): written once before the renderer starts, read only after.
RtStageTable& stage_table() {
    static RtStageTable rows;
    return rows;
}

RtEffectTable& effect_table() {
    static RtEffectTable rows;
    return rows;
}

// A table's lines as rows of fields; the first is the header, its names in lower case with '_' as ' '.
std::vector<std::vector<std::string>> tsv_rows(std::string_view text) {
    std::vector<std::vector<std::string>> rows;
    std::size_t start = 0u;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string line(text.substr(start, end - start));
        start = end + 1u;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        rows.push_back(split(line, '\t'));
    }
    if (!rows.empty()) {
        for (std::string& name : rows[0]) {
            name = lower(name);
            std::replace(name.begin(), name.end(), '_', ' ');
        }
    }
    return rows;
}

int column_of(const std::vector<std::string>& header, std::string_view name) {
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (header[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string field(const std::vector<std::string>& row, int column) {
    return column >= 0 && static_cast<std::size_t>(column) < row.size() ? lower(row[static_cast<std::size_t>(column)]) : std::string();
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

std::size_t RtStageTable::read(std::string_view text) {
    const std::vector<std::vector<std::string>> rows = tsv_rows(text);
    if (rows.empty()) {
        return 0u;
    }
    const std::vector<std::string>& head = rows[0];
    const int stage_col = column_of(head, "stage"), sun_col = column_of(head, "sun"), gain_col = column_of(head, "sun gain"),
              size_col = column_of(head, "sun size"), ambient_col = column_of(head, "ambient gain"),
              sky_col = column_of(head, "sky gain"), low_col = column_of(head, "exposure low"),
              high_col = column_of(head, "exposure high"), air_col = column_of(head, "air"),
              water_col = column_of(head, "water clear"), sets_col = column_of(head, "shadow sets");
    std::size_t read = 0u;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const std::vector<std::string>& row = rows[r];
        std::string name = stage_col >= 0 && static_cast<std::size_t>(stage_col) < row.size() ? row[static_cast<std::size_t>(stage_col)] : std::string();
        while (!name.empty() && (name.back() == ' ' || name.back() == '\r')) {
            name.pop_back();
        }
        if (name.empty()) {
            continue;
        }
        RtStage stage{};
        const std::string sun = field(row, sun_col);
        const std::vector<std::string> parts = split(sun, ' ');
        float d[3]{};
        char* end = nullptr;
        bool direction = parts.size() == 3u;
        for (std::size_t i = 0; i < 3u && direction; ++i) {
            d[i] = std::strtof(parts[i].c_str(), &end);
            direction = end != parts[i].c_str() && std::isfinite(d[i]);
        }
        const float length = direction ? std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) : 0.0f;
        if (direction && length > 1e-4f) {
            stage.sun = RtStage::Sun::Fixed;
            for (unsigned i = 0; i < 3u; ++i) {
                stage.direction[i] = d[i] / length;
            }
        } else if (sun == "object") {
            stage.sun = RtStage::Sun::Object;
        } else if (sun == "key") {
            stage.sun = RtStage::Sun::Key;
        } else if (sun == "none") {
            stage.sun = RtStage::Sun::None;
        }
        float v = 0.0f;
        if (number(row, gain_col, v)) stage.sun_gain = std::clamp(v, 0.0f, 4.0f);
        if (number(row, size_col, v)) stage.sun_size = std::clamp(v, 0.0f, 0.2f);
        if (number(row, ambient_col, v)) stage.ambient_gain = std::clamp(v, 0.0f, 4.0f);
        if (number(row, sky_col, v)) stage.sky_gain = std::clamp(v, 0.0f, 4.0f);
        if (number(row, low_col, v)) stage.exposure_low = std::clamp(v, 1.0f, 10000.0f);
        if (number(row, high_col, v)) stage.exposure_high = std::clamp(v, stage.exposure_low, 10000.0f);
        stage.exposure_high = std::max(stage.exposure_high, stage.exposure_low);
        if (number(row, air_col, v)) stage.air = std::clamp(v, 0.0f, 0.01f);
        if (number(row, water_col, v)) stage.water_clear = std::clamp(v, 1.0f, 100000.0f);
        stage.shadow_sets_kept = field(row, sets_col) == "keep";
        rows_[rt_stage_hash(name)] = stage;
        ++read;
    }
    return read;
}

const RtStage* RtStageTable::find(std::uint32_t hash) const noexcept {
    const auto it = rows_.find(hash);
    return it != rows_.end() ? &it->second : nullptr;
}

std::uint32_t rt_stage_hash(std::string_view name) noexcept {
    if (name.empty()) {
        return 0u;
    }
    std::uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return hash != 0u ? hash : 1u;
}

std::size_t RtEffectTable::read(std::string_view text) {
    const std::vector<std::vector<std::string>> rows = tsv_rows(text);
    if (rows.empty()) {
        return 0u;
    }
    const std::vector<std::string>& head = rows[0];
    const int effect_col = column_of(head, "effect"), luminance_col = column_of(head, "luminance"),
              behaviour_col = column_of(head, "behaviour");
    std::size_t read = 0u;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const std::vector<std::string>& row = rows[r];
        std::string name = effect_col >= 0 && static_cast<std::size_t>(effect_col) < row.size() ? row[static_cast<std::size_t>(effect_col)] : std::string();
        while (!name.empty() && (name.back() == ' ' || name.back() == '\r')) {
            name.pop_back();
        }
        float luminance = 0.0f;
        if (name.empty() || !number(row, luminance_col, luminance)) {
            continue;
        }
        RtEffectRow effect{};
        effect.luminance = std::clamp(luminance, 0.0f, 100000.0f);
        const std::string behaviour = field(row, behaviour_col);
        effect.behaviour = behaviour == "flicker" ? 1u : behaviour == "pulse" ? 2u : behaviour == "flash" ? 3u
                         : behaviour == "streak" ? 4u : behaviour == "own" ? 5u : 0u;
        rows_[rt_effect_hash(name)] = effect;
        ++read;
    }
    return read;
}

const RtEffectRow* RtEffectTable::find(std::uint32_t hash) const noexcept {
    const auto it = rows_.find(hash);
    return it != rows_.end() ? &it->second : nullptr;
}

std::uint32_t rt_effect_hash(std::string_view name) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const char c : std::string_view("Effect/")) {
        hash = (hash ^ static_cast<std::uint8_t>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c)) * 16777619u;
    }
    for (const char c : name) {
        hash = (hash ^ static_cast<std::uint8_t>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c)) * 16777619u;
    }
    return hash != 0u ? hash : 1u;
}

void set_rt_tables(std::string_view stages, std::string_view effects) {
    const std::size_t stage_rows = stage_table().read(stages);
    const std::size_t effect_rows = effect_table().read(effects);
    std::cerr << "[rt] tables of the pak: " << stage_rows << " stage rows, " << effect_rows << " effect rows\n";
}

const RtStage* rt_stage(std::uint32_t hash) noexcept {
    return hash != 0u ? stage_table().find(hash) : nullptr;
}

const RtEffectRow* rt_effect(std::uint32_t hash) noexcept {
    return effect_table().find(hash);
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
    // Only a stage row names untextured surfaces: elsewhere they stay.
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

}  // namespace galaxy::render
