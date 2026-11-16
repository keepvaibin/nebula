// SPDX-License-Identifier: GPL-3.0-only
// The ray tracer's tables of the pak (rt_materials.h): a stage's numbers and the particle effects as lights.
#include "galaxy/render/rt_materials.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::render {

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

// The column's number; false when the row leaves it empty (or holds no finite number there).
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

// The pak's tables (set_rt_tables): written once before the renderer starts, read only after.
RtStageTable& stage_table() {
    static RtStageTable rows;
    return rows;
}

RtEffectTable& effect_table() {
    static RtEffectTable rows;
    return rows;
}

// A table's lines as rows of fields; the first is the header, its names in lower case with '_' as ' '. A UTF-8 byte
// order mark before the header (an editor's) is not part of its first name.
std::vector<std::vector<std::string>> tsv_rows(std::string_view text) {
    if (text.substr(0u, 3u) == "\xEF\xBB\xBF") {
        text.remove_prefix(3u);
    }
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

}  // namespace galaxy::render
