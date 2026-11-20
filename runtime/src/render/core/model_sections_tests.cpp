// SPDX-License-Identifier: GPL-3.0-only
// Tests of model 13's section table as the game and the host read it (nebula_model.h: nebula_model_section,
// nebula_model_view; FRAME-MODEL 2.8): every section found by name, a later field or an unknown optional section read
// past, and each fault named: another version, a missing section, an unknown required one, a section past the block's
// end or with entries shorter than the reader's, envelopes without one inverse bind a joint.
#include <nebula_model.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

constexpr std::uint32_t name(const char (&text)[5]) {
    return NEBULA_MODEL_SECTION(text[0], text[1], text[2], text[3]);
}

// A block as the cooker lays it out: the header's 112 bytes, each array, the scalars, the table at the end. Each
// section here holds `count` entries of its least stride.
struct Block {
    std::vector<std::uint8_t> bytes;
    std::vector<NebulaModelSection> table;

    void add(std::uint32_t section, std::uint32_t count, std::uint32_t stride, std::uint32_t flags = NEBULA_MODEL_SECTION_REQUIRED) {
        bytes.resize((bytes.size() + 7u) & ~std::size_t{7u});  // every place 8-aligned, as the least alignment asks
        const auto at = static_cast<std::uint32_t>(bytes.size());
        bytes.resize(bytes.size() + static_cast<std::size_t>(count) * stride);
        table.push_back({section, flags, at, count * stride, count, stride});
    }
    std::vector<std::uint8_t> finish() const {
        std::vector<std::uint8_t> out = bytes;
        const auto sections_at = static_cast<std::uint32_t>(out.size());
        out.resize(out.size() + table.size() * sizeof(NebulaModelSection));
        std::memcpy(out.data() + sections_at, table.data(), table.size() * sizeof(NebulaModelSection));
        NebulaModelHeader header{};
        header.magic = NEBULA_MODEL_MAGIC;
        header.version = NEBULA_MODEL_VERSION;
        header.size = static_cast<std::uint32_t>(out.size());
        header.section_count = static_cast<std::uint32_t>(table.size());
        header.sections_at = sections_at;
        std::memcpy(out.data(), &header, sizeof(header));
        return out;
    }
};

Block model(std::uint32_t joints, std::uint32_t envelopes, std::uint32_t binds) {
    Block b;
    b.bytes.resize(sizeof(NebulaModelHeader));
    b.add(name("JNTS"), joints, 96u);
    b.add(name("STEP"), 2u * joints, 8u);
    b.add(name("JMSH"), 1u, 4u);
    b.add(name("DMTX"), 1u, 8u);
    b.add(name("ENVL"), envelopes, 8u);
    b.add(name("MIXS"), 2u * envelopes, 8u);
    b.add(name("IBND"), binds, 48u);
    b.add(name("IMPT"), 1u, 4u);
    b.add(name("MESH"), 1u, 100u);
    b.add(name("MATL"), 1u, 64u);
    b.add(name("GXMT"), 1u, 120u);
    b.add(name("FORM"), 1u, 56u);
    b.add(name("SLOT"), 2u, 8u);
    b.add(name("GXTG"), 1u, 8u);
    b.add(name("GXTM"), 0u, 104u);
    b.add(name("GXWP"), 0u, 24u);
    b.add(name("FROW"), 3u, 4u);
    b.add(name("FTEX"), 2u, 1u);
    b.add(name("FINP"), 4u, 1u);
    b.add(name("TEXR"), 1u, 20u);
    b.add(name("NAME"), 16u, 1u);
    b.add(name("GEOM"), 64u, 1u);
    b.add(name("TIMG"), 0u, 1u);
    b.add(name("SAMP"), 1u, 20u);
    b.add(name("MODL"), 1u, 8u);
    const std::uint32_t scalars = b.table.back().at;
    const std::uint32_t values[2] = {5u, 77u};
    std::memcpy(b.bytes.data() + scalars, values, sizeof(values));
    return b;
}

NebulaModelCheck view_of(const std::vector<std::uint8_t>& bytes, NebulaModelView& view) {
    return nebula_model_view(bytes.data(), static_cast<std::uint32_t>(bytes.size()), &view);
}

void test_resolved() {
    const Block b = model(3u, 2u, 3u);
    const std::vector<std::uint8_t> bytes = b.finish();
    NebulaModelView view{};
    const NebulaModelCheck result = view_of(bytes, view);
    check(result.fault == NEBULA_MODEL_FINE, "a model of 13 reads");
    check(view.joint_count == 3u && view.joints_at == b.table[0].at, "the joints by JNTS");
    check(view.step_count == 6u && view.envelope_count == 2u && view.mix_count == 4u, "the counts by their sections");
    check(view.inverse_bind_count == 3u && view.inverse_binds_at == b.table[6].at, "the inverse binds by IBND's count");
    check(view.mesh_count == 1u && view.material_count == 1u && view.texture_count == 1u && view.sampler_count == 1u, "meshes, materials, textures");
    check(view.geometry_size == 64u && view.geometry_at == b.table[21].at, "the geometry by GEOM's span");
    check(view.names_size == 16u && view.images_size == 0u, "the names and the images by their spans");
    check(view.full_weight_count == 5u && view.source_vertex_count == 77u, "the scalars from MODL");
    const auto* header = reinterpret_cast<const NebulaModelHeader*>(bytes.data());
    const NebulaModelSection* geometry = nebula_model_section(header, name("GEOM"));
    check(geometry != nullptr && geometry->at == b.table[21].at && geometry->size == 64u, "nebula_model_section finds GEOM");
    check(nebula_model_section(header, name("CBLK")) == nullptr, "a section the file does not have is null");
    check(view.form_count == 1u && view.slot_count == 2u && view.row_count == 3u && view.maps_size == 2u &&
              view.inputs_size == 4u && view.gx_count == 1u && view.coord_count == 1u,
          "the material sections by their counts");
}

void test_read_past() {
    // A later field (a longer stride) and an optional section this reader does not know are read past.
    Block b = model(1u, 0u, 0u);
    b.table[0].stride = 100u;
    b.table[0].size = 100u;
    b.add(name("SAMP"), 2u, 32u, 0u);
    NebulaModelView view{};
    check(view_of(b.finish(), view).fault == NEBULA_MODEL_FINE && view.joint_count == 1u && view.joint_stride == 100u,
          "a longer entry and an unknown optional section are read past");
    // A reader steps by the written stride: the second mesh's first field where a 104-byte stride puts it.
    Block m = model(1u, 0u, 0u);
    m.table.erase(m.table.begin() + 8);
    m.add(name("MESH"), 2u, 104u);
    const float second = 7.5f;
    std::memcpy(m.bytes.data() + m.table.back().at + 104u, &second, 4u);
    const std::vector<std::uint8_t> bytes = m.finish();
    NebulaModelView mesh_view{};
    check(view_of(bytes, mesh_view).fault == NEBULA_MODEL_FINE && mesh_view.mesh_stride == 104u && mesh_view.mesh_count == 2u,
          "a mesh section of a longer stride reads");
    const auto* mesh = static_cast<const NebulaModelMesh*>(nebula_model_entry(bytes.data(), mesh_view.meshes_at, mesh_view.mesh_stride, 1u));
    check(mesh->min[0] == 7.5f, "the second entry by the written stride");
}

void test_faults() {
    char text[5];
    {
        std::vector<std::uint8_t> bytes = model(1u, 0u, 0u).finish();
        const std::uint32_t twelve = 12u;
        std::memcpy(bytes.data() + 4, &twelve, 4u);
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(bytes, view);
        check(result.fault == NEBULA_MODEL_OTHER_VERSION && result.what == 12u, "a model of 12 names its version");
    }
    {
        Block b = model(1u, 0u, 0u);
        b.table.erase(b.table.begin() + 9);  // MATL
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_MISSING_SECTION && std::strcmp(text, "MATL") == 0, "a model without MATL is refused, naming it");
    }
    {
        Block b = model(1u, 0u, 0u);
        b.add(name("CBLK"), 1u, 16u);
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_UNKNOWN_REQUIRED && std::strcmp(text, "CBLK") == 0,
              "a required section this reader does not know is named");
    }
    {
        Block b = model(1u, 0u, 0u);
        b.table[8].stride = 96u;  // MESH entries shorter than the reader's 100
        b.table[8].size = 96u;
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_BAD_SECTION && std::strcmp(text, "MESH") == 0, "short entries are named");
    }
    {
        Block b = model(1u, 0u, 0u);
        b.table[21].size = 1u << 20u;  // GEOM past the end
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_BAD_SECTION && std::strcmp(text, "GEOM") == 0, "a section past the end is named");
    }
    {
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(model(4u, 2u, 0u).finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_BAD_SECTION && std::strcmp(text, "IBND") == 0,
              "envelopes without one inverse bind a joint are refused");
    }
    {
        std::vector<std::uint8_t> bytes = model(1u, 0u, 0u).finish();
        NebulaModelView view{};
        check(nebula_model_view(bytes.data(), 64u, &view).fault == NEBULA_MODEL_NOT_A_MODEL, "a block shorter than its header");
        bytes[0] ^= 1u;
        check(view_of(bytes, view).fault == NEBULA_MODEL_NOT_A_MODEL, "no magic, no model");
    }
    // A material's, a form's or a GXMT entry's range outside its section (FRAME-MODEL 2.9).
    const auto range_fault = [&text](std::uint32_t section, std::uint32_t offset, std::uint32_t value, const char* named) {
        Block b = model(1u, 0u, 0u);
        std::memcpy(b.bytes.data() + b.table[section].at + offset, &value, 4u);
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        return result.fault == NEBULA_MODEL_BAD_RANGE && std::strcmp(text, named) == 0;
    };
    check(range_fault(9u, 32u, 1u, "GXMT"), "a material naming a GXMT entry past the section");
    {
        Block b = model(1u, 0u, 0u);
        b.table[16].stride = 8u;  // FROW: a list of rows, not a record
        b.table[16].count = 1u;
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_BAD_SECTION && std::strcmp(text, "FROW") == 0, "a list's entries are its stride exactly");
    }
    {
        Block b = model(1u, 0u, 0u);
        b.table[11].stride = 60u;  // FORM holds u64 hashes: 8-aligned entries
        b.table[11].size = 60u;
        b.bytes.resize(b.bytes.size() + 4u);
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_BAD_SECTION && std::strcmp(text, "FORM") == 0, "a misaligned stride is refused");
    }
    check(range_fault(9u, 36u, 1u, "SLOT") == false, "a material's slots inside SLOT");
    check(range_fault(9u, 36u, 5u, "SLOT"), "a material's slots past SLOT");
    check(range_fault(9u, 44u, 1u, "FORM") == false && range_fault(9u, 44u, 2u, "FORM"), "a material's forms past FORM");
    check(range_fault(10u, 96u, 1u, "GXTG") == false && range_fault(10u, 96u, 2u, "GXTG"), "a GXMT entry's coordinates past GXTG");
    check(range_fault(11u, 16u, 1u, "MATL"), "a form of a material the model does not have");
    check(range_fault(11u, 20u, 4u, "FROW"), "a form's rows past FROW");
    check(range_fault(11u, 44u, 5u, "FINP"), "a form's inputs past FINP");
    check(range_fault(19u, 16u, 1u, "SAMP"), "a texture naming a sampler past SAMP");
}

}  // namespace

int main() {
    static_assert(sizeof(NebulaModelHeader) == 112u, "the header keeps 12's room");
    static_assert(sizeof(NebulaModelSection) == 24u, "a section is six words");
    test_resolved();
    test_read_past();
    test_faults();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("model sections: all passed\n");
    return 0;
}
