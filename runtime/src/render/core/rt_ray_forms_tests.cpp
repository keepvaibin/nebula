// SPDX-License-Identifier: GPL-3.0-only
// A mod's `.rayhit` as the tracer reads it (core/rt_ray_forms.h): the cook's table (cook/table.rs to_ntbl) gives each
// material's form; a table cut short anywhere before its text ends, an offset or a count past the bytes, a count whose
// product wraps 32 bits, a word with no end, a column of the wrong kind or none gives no form at all; a material is
// found by its name in the model file only inside that file.
#include "render/core/rt_ray_forms.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using galaxy::render::core::RayForm;
using galaxy::render::core::read_ray_forms;
using galaxy::render::core::ray_form_column_hash;
using galaxy::render::core::ray_form_of_material;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

void put32(std::vector<unsigned char>& out, std::size_t at, std::uint32_t v) {
    for (unsigned k = 0; k < 4u; ++k) {
        out[at + k] = static_cast<unsigned char>(v >> (8u * k));
    }
}

struct Column {
    const char* name;
    std::uint8_t kind;  // 0 int, 3 text
};

// The table as table.rs to_ntbl writes it: header, columns, cells, text ("" at 0), padded to 32 bytes. An int cell
// holds 7. `text_end` takes where the text ends.
std::vector<unsigned char> ntbl(const std::vector<Column>& columns, const std::vector<std::vector<std::string>>& rows,
                                std::size_t* text_end = nullptr) {
    const std::size_t rows_at = 32u + 12u * columns.size();
    const std::size_t text_at = rows_at + 4u * columns.size() * rows.size();
    std::vector<unsigned char> out(text_at, 0u);
    std::memcpy(out.data(), "NTBL", 4u);
    put32(out, 4u, static_cast<std::uint32_t>(rows.size()));
    put32(out, 8u, static_cast<std::uint32_t>(columns.size()));
    put32(out, 12u, static_cast<std::uint32_t>(rows_at));
    put32(out, 16u, static_cast<std::uint32_t>(text_at));
    for (std::size_t c = 0; c < columns.size(); ++c) {
        put32(out, 32u + 12u * c, ray_form_column_hash(columns[c].name));
        put32(out, 32u + 12u * c + 4u, 0xFFFFFFFFu);
        out[32u + 12u * c + 8u] = columns[c].kind;
        out[32u + 12u * c + 9u] = 32u;
    }
    std::string text(1u, '\0');
    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (std::size_t c = 0; c < columns.size(); ++c) {
            std::uint32_t cell = 7u;
            if (columns[c].kind == 3u) {
                cell = static_cast<std::uint32_t>(text.size());
                text += rows[r][c];
                text.push_back('\0');
            }
            put32(out, rows_at + 4u * (r * columns.size() + c), cell);
        }
    }
    put32(out, 20u, static_cast<std::uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    if (text_end != nullptr) {
        *text_end = out.size();
    }
    while (out.size() % 32u != 0u) {
        out.push_back(0u);
    }
    return out;
}

const std::vector<Column> kColumns = {{"material", 3u}, {"text", 3u}};

bool well_formed() {
    std::vector<RayForm> forms;
    const auto table = ntbl({{"material", 3u}, {"count", 0u}, {"text", 3u}},
                            {{"Body", "", "namespace rt_mod { void surface_hit() {} }"}, {"Eye", "", "x"}});
    bool ok = expect(read_ray_forms(table.data(), table.size(), forms) && forms.size() == 2u, "the cook's table: two forms");
    ok &= expect(forms.size() == 2u && forms[0].material == "Body" && forms[0].text.substr(0, 9) == "namespace" &&
                     forms[1].material == "Eye" && forms[1].text == "x",
                 "each row's material and text, columns found by name past one of another kind");
    const auto empty = ntbl(kColumns, {});
    ok &= expect(read_ray_forms(empty.data(), empty.size(), forms) && forms.empty(), "no rows: read, no forms");
    const auto twice = ntbl(kColumns, {{"A", "first"}, {"A", "second"}});
    ok &= expect(read_ray_forms(twice.data(), twice.size(), forms) && forms.size() == 2u, "a name twice is read");
    return ok;
}

bool truncated() {
    std::size_t text_end = 0u;
    const auto table = ntbl(kColumns, {{"Body", "form one"}, {"Arm", "form two"}}, &text_end);
    std::vector<RayForm> forms;
    bool ok = true;
    for (std::size_t n = 0; n < text_end; ++n) {
        if (read_ray_forms(table.data(), n, forms) || !forms.empty()) {
            ok &= expect(false, "a table cut before its text ends is not read");
            break;
        }
    }
    ok &= expect(read_ray_forms(table.data(), text_end, forms) && forms.size() == 2u, "cut after its text (the padding): read");
    ok &= expect(!read_ray_forms(nullptr, 64u, forms) && !read_ray_forms(table.data(), 0u, forms), "no bytes: not read");
    return ok;
}

bool out_of_range() {
    const auto good = ntbl(kColumns, {{"Body", "form"}});
    std::vector<RayForm> forms;
    const auto broken = [&](std::size_t at, std::uint32_t value) {
        auto t = good;
        put32(t, at, value);
        return !read_ray_forms(t.data(), t.size(), forms) && forms.empty();
    };
    const std::uint32_t size = static_cast<std::uint32_t>(good.size());
    bool ok = expect(broken(12u, size), "cells past the end");
    ok &= expect(broken(12u, 8u), "cells over the header and columns");
    ok &= expect(broken(16u, size + 1u), "text past the end");
    ok &= expect(broken(20u, size), "text longer than what is left");
    const std::size_t rows_at = 32u + 12u * kColumns.size();
    ok &= expect(broken(rows_at, 0x7FFFFFFFu), "a material's word past the text");
    ok &= expect(broken(rows_at + 4u, 0xFFFFFFFFu), "a form's word past the text");
    ok &= expect(broken(rows_at, 0u), "a material with no name (the text's first word)");
    // The text's last NUL gone: the last word runs past the text.
    {
        auto t = good;
        std::uint32_t text_at = 0u, text_size = 0u;
        std::memcpy(&text_at, t.data() + 16u, 4u);
        std::memcpy(&text_size, t.data() + 20u, 4u);
        t[text_at + text_size - 1u] = 'x';
        ok &= expect(!read_ray_forms(t.data(), t.size(), forms) && forms.empty(), "a word with no end");
    }
    {
        auto t = good;
        t[0] = 'B';
        ok &= expect(!read_ray_forms(t.data(), t.size(), forms), "not NTBL");
    }
    return ok;
}

bool counts() {
    const auto good = ntbl(kColumns, {{"Body", "form"}});
    std::vector<RayForm> forms;
    const auto broken = [&](std::size_t at, std::uint32_t value) {
        auto t = good;
        put32(t, at, value);
        return !read_ray_forms(t.data(), t.size(), forms) && forms.empty();
    };
    bool ok = expect(broken(4u, 0xFFFFFFFFu), "rows: all ones");
    ok &= expect(broken(4u, 2u), "two rows where one is written: the second's cells are the text");
    ok &= expect(broken(8u, 0xFFFFFFFFu), "columns: all ones");
    ok &= expect(broken(8u, 0x40000000u), "columns x 4 bytes wraps 32 bits");
    ok &= expect(broken(8u, 0x15555556u), "columns x 12 bytes wraps 32 bits");
    ok &= expect(broken(8u, 0u), "no columns");
    std::vector<std::vector<std::string>> many(256u, std::vector<std::string>{"M", "f"});
    const auto big = ntbl(kColumns, many);
    ok &= expect(!read_ray_forms(big.data(), big.size(), forms) && forms.empty(), "more rows than a byte names");
    return ok;
}

bool kinds() {
    std::vector<RayForm> forms;
    const auto int_material = ntbl({{"material", 0u}, {"text", 3u}}, {{"", "form"}});
    bool ok = expect(!read_ray_forms(int_material.data(), int_material.size(), forms), "material not of text");
    const auto no_text = ntbl({{"material", 3u}, {"param", 3u}}, {{"Body", "speed"}});
    ok &= expect(!read_ray_forms(no_text.data(), no_text.size(), forms), "no text column (a .params table)");
    return ok;
}

// A model file of two materials named "Body" and "Arm" (NebulaModelHeader, two NebulaModelMaterial, the names).
std::vector<unsigned char> model(std::uint32_t size_word = 0u) {
    const std::size_t materials_at = sizeof(NebulaModelHeader);
    const std::size_t names_at = materials_at + 2u * sizeof(NebulaModelMaterial);
    std::vector<unsigned char> out(names_at + 9u, 0u);
    NebulaModelHeader header{};
    header.magic = NEBULA_MODEL_MAGIC;
    header.version = NEBULA_MODEL_VERSION;
    header.size = size_word != 0u ? size_word : static_cast<std::uint32_t>(out.size());
    header.material_count = 2u;
    header.materials_at = static_cast<std::uint32_t>(materials_at);
    std::memcpy(out.data(), &header, sizeof(header));
    NebulaModelMaterial m[2]{};
    m[0].name_at = static_cast<std::uint32_t>(names_at);
    m[1].name_at = static_cast<std::uint32_t>(names_at + 5u);
    std::memcpy(out.data() + materials_at, m, sizeof(m));
    std::memcpy(out.data() + names_at, "Body\0Arm\0", 9u);
    return out;
}

bool materials() {
    std::vector<RayForm> forms;
    const auto table = ntbl(kColumns, {{"Eye", "a"}, {"Body", "b"}});
    bool ok = expect(read_ray_forms(table.data(), table.size(), forms), "the table");
    const auto file = model();
    ok &= expect(ray_form_of_material(file.data(), 0u, forms) == 2u, "Body: 1 + its row");
    ok &= expect(ray_form_of_material(file.data(), 1u, forms) == 0u, "Arm: none");
    ok &= expect(ray_form_of_material(file.data(), 2u, forms) == 0u && ray_form_of_material(file.data(), 0xFFFFFFFFu, forms) == 0u,
                 "a material past the count");
    ok &= expect(ray_form_of_material(file.data(), 0u, {}) == 0u && ray_form_of_material(nullptr, 0u, forms) == 0u, "no forms, no file");
    // The names past what the header says the file holds: "Body" is not found (its NUL lies past size).
    const auto short_file = model(static_cast<std::uint32_t>(sizeof(NebulaModelHeader) + 2u * sizeof(NebulaModelMaterial) + 3u));
    ok &= expect(ray_form_of_material(short_file.data(), 0u, forms) == 0u, "a name past the file's size");
    // Materials past the size: none.
    auto far = model();
    NebulaModelHeader header{};
    std::memcpy(&header, far.data(), sizeof(header));
    header.materials_at = 0xFFFFFF00u;
    std::memcpy(far.data(), &header, sizeof(header));
    ok &= expect(ray_form_of_material(far.data(), 1u, forms) == 0u, "materials past the file");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= well_formed();
    ok &= truncated();
    ok &= out_of_range();
    ok &= counts();
    ok &= kinds();
    ok &= materials();
    if (!ok) {
        return 1;
    }
    std::cout << "rt ray forms tests passed\n";
    return 0;
}
