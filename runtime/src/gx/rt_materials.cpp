// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/rt_materials.h"

#include "galaxy/gx/rt_surfaces.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
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

struct Table {
    std::vector<RtMaterial> materials;
    std::unordered_map<std::uint64_t, std::uint16_t> by_hash;
};

std::string table_path() {
    char value[MAX_PATH]{};
    std::size_t length = 0u;
    if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_MATERIALS_FILE") == 0 && length > 1u) {
        return value;
    }
    char exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string path(exe, n);
    const std::size_t slash = path.find_last_of("\\/");
    return (slash == std::string::npos ? std::string(".") : path.substr(0, slash)) + "\\rt-materials.tsv";
}

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

float number(const std::vector<std::string>& row, int column, float fallback) {
    if (column < 0 || static_cast<std::size_t>(column) >= row.size() || row[column].empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(row[column].c_str(), &end);
    return end != row[column].c_str() ? v : fallback;
}

Table load() {
    Table table;
    const std::string path = table_path();
    std::ifstream file(path);
    if (!file) {
        return table;
    }
    std::string line;
    std::vector<std::string> header;
    int hash_col = -1, class_col = -1, r_col = -1, g_col = -1, b_col = -1, intensity_col = -1, range_col = -1,
        pulse_col = -1, shadow_col = -1;
    std::size_t rows = 0u, skipped = 0u;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> row = split(line, '\t');
        if (header.empty()) {
            header = row;
            for (std::size_t i = 0; i < header.size(); ++i) {
                const std::string name = lower(header[i]);
                int* const slot = name == "hash" ? &hash_col : name == "class" ? &class_col : name == "r" ? &r_col
                                : name == "g" ? &g_col : name == "b" ? &b_col : name == "intensity" ? &intensity_col
                                : name == "range" ? &range_col : name == "pulse" ? &pulse_col
                                : name == "shadow" ? &shadow_col : nullptr;
                if (slot != nullptr) *slot = static_cast<int>(i);
            }
            continue;
        }
        std::uint64_t hash = 0u;
        if (hash_col < 0 || static_cast<std::size_t>(hash_col) >= row.size() || !parse_hash(row[hash_col], hash) ||
            class_col < 0 || static_cast<std::size_t>(class_col) >= row.size()) {
            ++skipped;
            continue;
        }
        RtMaterial m{};
        for (const std::string& part : split(lower(row[class_col]), '+')) {
            const std::string token = lower(part);
            if (token == "emitter" || token == "light") m.emission = RtEmission::Point;
            else if (token == "emissive-area" || token == "area") m.emission = RtEmission::Area;
            else if (token == "skip" || token == "none") m.no_light = true;
            else if (token == "metal") m.surface_class = static_cast<std::uint8_t>(RtSurfaceClass::Metal);
            else if (token == "glossy") m.surface_class = static_cast<std::uint8_t>(RtSurfaceClass::Glossy);
            else if (token == "glass" || token == "crystal" || token == "glass/crystal") {
                m.surface_class = static_cast<std::uint8_t>(RtSurfaceClass::Crystal);
            } else if (token == "ice" || token == "water") m.surface_class = static_cast<std::uint8_t>(RtSurfaceClass::Water);
            else if (token == "diffuse") m.surface_class = static_cast<std::uint8_t>(RtSurfaceClass::Diffuse);
            else if (token == "unlit") m.surface_class = static_cast<std::uint8_t>(RtSurfaceClass::Unlit);
        }
        m.color[0] = std::clamp(number(row, r_col, 1.0f), 0.0f, 1.0f);
        m.color[1] = std::clamp(number(row, g_col, 1.0f), 0.0f, 1.0f);
        m.color[2] = std::clamp(number(row, b_col, 1.0f), 0.0f, 1.0f);
        m.intensity = std::clamp(number(row, intensity_col, m.emission != RtEmission::None ? 1.0f : 0.0f), 0.0f, 100.0f);
        m.range = std::clamp(number(row, range_col, 0.0f), 0.0f, 1.0e5f);
        m.pulse = std::clamp(number(row, pulse_col, 0.0f), 0.0f, 30.0f);
        m.shadow = number(row, shadow_col, 1.0f) != 0.0f;
        if (table.materials.size() >= 0xFFFEu) {
            break;
        }
        // A later row for the same texture replaces the earlier one.
        const auto [it, inserted] = table.by_hash.try_emplace(hash, static_cast<std::uint16_t>(table.materials.size() + 1u));
        if (inserted) {
            table.materials.push_back(m);
        } else {
            table.materials[it->second - 1u] = m;
        }
        ++rows;
    }
    std::cerr << "[rt-materials] file=" << path << " rows=" << rows << " textures=" << table.by_hash.size()
              << " skipped=" << skipped << '\n';
    return table;
}

const Table& table() {
    static const Table loaded = load();
    return loaded;
}

}  // namespace

bool rt_materials_loaded() noexcept {
    return !table().materials.empty();
}

std::uint16_t rt_material_index(std::uint64_t hash) noexcept {
    const Table& t = table();
    const auto it = t.by_hash.find(hash);
    return it != t.by_hash.end() ? it->second : 0u;
}

const RtMaterial* rt_material(std::uint16_t index) noexcept {
    const Table& t = table();
    return index != 0u && index <= t.materials.size() ? &t.materials[index - 1u] : nullptr;
}

}  // namespace galaxy::gx
