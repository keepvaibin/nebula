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
    b.add(name("MT12"), 1u, 208u);
    b.add(name("TEXR"), 0u, 40u);
    b.add(name("NAME"), 16u, 1u);
    b.add(name("GEOM"), 64u, 1u);
    b.add(name("TIMG"), 0u, 1u);
    const auto scalars = static_cast<std::uint32_t>(b.bytes.size());
    b.add(name("MODL"), 1u, 8u);
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
    check(view.mesh_count == 1u && view.material_count == 1u && view.texture_count == 0u, "meshes, materials, textures");
    check(view.geometry_size == 64u && view.geometry_at == b.table[12].at, "the geometry by GEOM's span");
    check(view.names_size == 16u && view.images_size == 0u, "the names and the images by their spans");
    check(view.full_weight_count == 5u && view.source_vertex_count == 77u, "the scalars from MODL");
    const auto* header = reinterpret_cast<const NebulaModelHeader*>(bytes.data());
    const NebulaModelSection* geometry = nebula_model_section(header, name("GEOM"));
    check(geometry != nullptr && geometry->at == b.table[12].at && geometry->size == 64u, "nebula_model_section finds GEOM");
    check(nebula_model_section(header, name("FORM")) == nullptr, "a section the file does not have is null");
}

void test_read_past() {
    // A later field (a longer stride) and an optional section this reader does not know are read past.
    Block b = model(1u, 0u, 0u);
    b.table[0].stride = 100u;
    b.table[0].size = 100u;
    b.add(name("SAMP"), 2u, 32u, 0u);
    NebulaModelView view{};
    check(view_of(b.finish(), view).fault == NEBULA_MODEL_FINE && view.joint_count == 1u,
          "a longer entry and an unknown optional section are read past");
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
        b.table.erase(b.table.begin() + 9);  // MT12
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_MISSING_SECTION && std::strcmp(text, "MT12") == 0, "a missing section is named");
    }
    {
        Block b = model(1u, 0u, 0u);
        b.add(name("FORM"), 1u, 16u);
        NebulaModelView view{};
        const NebulaModelCheck result = view_of(b.finish(), view);
        nebula_model_section_text(result.what, text);
        check(result.fault == NEBULA_MODEL_UNKNOWN_REQUIRED && std::strcmp(text, "FORM") == 0,
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
        b.table[12].size = 1u << 20u;  // GEOM past the end
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
