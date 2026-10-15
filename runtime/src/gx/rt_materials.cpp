// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/rt_materials.h"

#include "galaxy/gx/rt_surfaces.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
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
        if (token == "emitter" || token == "light") m.emission = RtEmission::Point;
        else if (token == "emissive-area" || token == "area") m.emission = RtEmission::Area;
        else if (token == "skip" || token == "none") m.no_light = true;
        else if (token == "helper") m.helper = true;
        else if (token == "tint") m.tint = true;
        else if (token == "hide") m.hide = true;
        else if (token == "unlit-only") m.unlit_only = true;
        else if (token == "particle") m.particle = true;
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
        } else if (token == "diffuse" || token == "foliage" || token == "fabric" || token == "skin") {
            set(RtSurfaceClass::Diffuse);
            m.relief = token == "diffuse" ? 1.0f : token == "foliage" ? 0.7f : 0.0f;
            m.matte = token == "foliage" || token == "fabric";
        } else if (token == "unlit" || token == "sky") {
            set(RtSurfaceClass::Unlit);
            m.relief = 0.0f;
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
    char exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir(exe, n);
    const std::size_t slash = dir.find_last_of("\\/");
    dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
    return {dir + "\\rt-materials.tsv", dir + "\\rt-materials-lights.tsv"};
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
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        std::string path;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_SUNS_FILE") == 0 && length > 1u) {
            path = value;
        } else {
            char exe[MAX_PATH]{};
            const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
            std::string dir(exe, n);
            const std::size_t slash = dir.find_last_of("\\/");
            path = (slash == std::string::npos ? std::string(".") : dir.substr(0, slash)) + "\\rt-suns.tsv";
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
        intensity_col = -1, range_col = -1, pulse_col = -1, cone_col = -1, shadow_col = -1;
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
                                : name == "shadow" ? &shadow_col : nullptr;
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
        RtMaterial& m = materials_[it->second - 1u];
        const RtEmission before = m.emission;
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
        if (number(row, pulse_col, v)) m.pulse = std::clamp(v, 0.0f, 30.0f);
        if (number(row, cone_col, v)) m.cone = std::clamp(v, 0.0f, 90.0f);
        if (number(row, shadow_col, v)) m.shadow = v != 0.0f;
        ++rows;
    }
    return rows;
}

std::uint16_t RtMaterialTable::index(std::uint64_t hash) const noexcept {
    const auto it = by_hash_.find(hash);
    return it != by_hash_.end() ? it->second : 0u;
}

const RtMaterial* RtMaterialTable::material(std::uint16_t index) const noexcept {
    return index != 0u && index <= materials_.size() ? &materials_[index - 1u] : nullptr;
}

std::size_t RtStageSunTable::read(std::istream& in) {
    std::string line;
    bool header_read = false;
    int stage_col = -1, source_col = -1, x_col = -1, y_col = -1, z_col = -1, r_col = -1, g_col = -1, b_col = -1,
        strength_col = -1, size_col = -1, shafts_col = -1;
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
                                : name == "shafts" ? &shafts_col : nullptr;
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
        if (source == "table" && direction && length > 1e-4f) {
            sun.source = RtStageSun::Source::Table;
            for (unsigned i = 0; i < 3u; ++i) sun.direction[i] = d[i] / length;
        } else if (source == "none") {
            sun.source = RtStageSun::Source::None;
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

const RtMaterial* rt_material(std::uint16_t index) noexcept {
    return table().material(index);
}

}  // namespace galaxy::gx
