// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A mod's `<model>.rayhit` (contract materials 3.4 layer 3; crates/nebula-import/src/cook/gltf.rs ray_hit_table): an
// NTBL (cook/table.rs) of a row for each material with a hand-written surface shader: `material` (text, the material's
// name as the model names it) and `text` (text, its ray-hit form, programs.rs ray_hit_form: HLSL that runs the shader's
// surface() at a ray's hit). The form's input is the material's hit record, its base by the vertex colour where flagged
// and never by a map (the material's maps are the shader's own); until the forms are built into the hit-read programs
// that record is what a ray sees. A mod's file: every offset, count and word is checked against the bytes given.

#include <nebula_model.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace galaxy::render::core {

struct RayForm {
    std::string_view material;  // both inside the table's bytes, which outlive the rows
    std::string_view text;
};

// A mesh names its form in a byte (GpuMesh::ray_form, 1 + the row): a table of more rows is not read.
inline constexpr std::size_t kRayFormsMost = 255u;

// A column's name as the cook hashes it (table.rs column_hash): x31 over the bytes, each sign-extended.
constexpr std::uint32_t ray_form_column_hash(std::string_view name) {
    std::uint32_t hash = 0u;
    for (const char c : name) {
        hash = hash * 31u + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(c)));
    }
    return hash;
}

namespace ray_forms_detail {

inline constexpr std::size_t kHeader = 32u;
inline constexpr std::size_t kColumn = 12u;
inline constexpr std::uint8_t kText = 3u;  // table.rs Kind::Text

inline std::uint32_t le32(const unsigned char* at) {
    return std::uint32_t{at[0]} | std::uint32_t{at[1]} << 8u | std::uint32_t{at[2]} << 16u | std::uint32_t{at[3]} << 24u;
}

// The NUL-ended word at `offset` of the text, or false where it starts or runs past the text's end.
inline bool word(const unsigned char* text, std::uint64_t text_size, std::uint64_t offset, std::string_view& out) {
    if (offset >= text_size) {
        return false;
    }
    const unsigned char* start = text + static_cast<std::size_t>(offset);
    const void* end = std::memchr(start, 0, static_cast<std::size_t>(text_size - offset));
    if (end == nullptr) {
        return false;
    }
    out = std::string_view(reinterpret_cast<const char*>(start), static_cast<std::size_t>(static_cast<const unsigned char*>(end) - start));
    return true;
}

}  // namespace ray_forms_detail

// The forms of the `size` bytes at `data`, or false with `forms` empty where the bytes are not the cook's table: no
// "NTBL", a part (columns, cells, text) past the end, no `material` or `text` column of text, a word that starts or
// runs past the text, a material with no name, more than kRayFormsMost rows. A material named twice keeps its first.
inline bool read_ray_forms(const void* data, std::size_t size, std::vector<RayForm>& forms) {
    namespace d = ray_forms_detail;
    forms.clear();
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes == nullptr || size < d::kHeader || std::memcmp(bytes, "NTBL", 4u) != 0) {
        return false;
    }
    // Every sum in 64 bits: each term is a 32-bit word or a product of a count kept small first.
    const std::uint64_t rows = d::le32(bytes + 4u), columns = d::le32(bytes + 8u);
    const std::uint64_t rows_at = d::le32(bytes + 12u), text_at = d::le32(bytes + 16u), text_size = d::le32(bytes + 20u);
    if (rows > kRayFormsMost || columns == 0u || columns > (size - d::kHeader) / d::kColumn ||
        rows_at < d::kHeader + columns * d::kColumn || rows_at > size || rows * columns * 4u > size - rows_at ||
        text_at > size || text_size > size - text_at) {
        return false;
    }
    // Past these checks every offset below is inside `size`, so it fits a std::size_t.
    std::uint64_t material = columns, text = columns;
    for (std::uint64_t c = 0u; c < columns; ++c) {
        const unsigned char* column = bytes + static_cast<std::size_t>(d::kHeader + c * d::kColumn);
        const std::uint32_t hash = d::le32(column);
        if (hash == ray_form_column_hash("material") && material == columns) {
            material = column[8] == d::kText ? c : columns + 1u;
        } else if (hash == ray_form_column_hash("text") && text == columns) {
            text = column[8] == d::kText ? c : columns + 1u;
        }
    }
    if (material >= columns || text >= columns) {
        return false;
    }
    forms.reserve(static_cast<std::size_t>(rows));
    for (std::uint64_t r = 0u; r < rows; ++r) {
        const unsigned char* cells = bytes + static_cast<std::size_t>(rows_at + r * columns * 4u);
        const unsigned char* words = bytes + static_cast<std::size_t>(text_at);
        RayForm form;
        if (!d::word(words, text_size, d::le32(cells + static_cast<std::size_t>(material * 4u)), form.material) ||
            !d::word(words, text_size, d::le32(cells + static_cast<std::size_t>(text * 4u)), form.text) || form.material.empty()) {
            forms.clear();
            return false;
        }
        forms.push_back(form);
    }
    return true;
}

// The form of material `material` of the model file `model` (NebulaModelHeader): 1 + its row among `forms`, 0 where it
// has none or its name is not inside the file (header.size bounds every read).
inline std::uint8_t ray_form_of_material(const void* model, std::uint32_t material, const std::vector<RayForm>& forms) {
    if (model == nullptr || forms.empty()) {
        return 0u;
    }
    NebulaModelHeader header;
    std::memcpy(&header, model, sizeof(header));
    const std::uint64_t size = header.size;
    const std::uint64_t record = std::uint64_t{header.materials_at} + std::uint64_t{material} * sizeof(NebulaModelMaterial);
    if (material >= header.material_count || size < sizeof(header) || record + sizeof(NebulaModelMaterial) > size) {
        return 0u;
    }
    const auto* bytes = static_cast<const unsigned char*>(model);
    NebulaModelMaterial m;
    std::memcpy(&m, bytes + static_cast<std::size_t>(record), sizeof(m));
    std::string_view name;
    if (!ray_forms_detail::word(bytes, size, m.name_at, name)) {
        return 0u;
    }
    for (std::size_t k = 0; k < forms.size() && k < kRayFormsMost; ++k) {
        if (forms[k].material == name) {
            return static_cast<std::uint8_t>(k + 1u);
        }
    }
    return 0u;
}

}  // namespace galaxy::render::core
