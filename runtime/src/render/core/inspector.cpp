// SPDX-License-Identifier: GPL-3.0-only
// The developer inspector: what a stop's frame list drew, value by value, and which draws touch one pixel. A replay's
// one-pixel walk leaves the upscaler's history wrong for some frames: an inspecting run is for reading, not for pictures.
#if defined(NEBULA_DEV)

#include "render/core/inspector.h"

#include "galaxy/render/mod_shaders.h"
#include "galaxy/render/render_core.h"
#include "galaxy/run_options.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace galaxy::render::inspector {

static float half_to_float(std::uint16_t h) {
    const unsigned sign = (h >> 15u) & 1u;
    const unsigned exponent = (h >> 10u) & 31u;
    const unsigned fraction = h & 1023u;
    const float value = exponent == 31u ? (fraction != 0u ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity())
                        : exponent == 0u ? std::ldexp(static_cast<float>(fraction), -24)
                                         : std::ldexp(static_cast<float>(fraction + 1024u), static_cast<int>(exponent) - 25);
    return sign != 0u ? -value : value;
}

namespace {

std::FILE* open_for_writing(const char* path) {
#if defined(_WIN32)
    std::FILE* file = nullptr;
    return fopen_s(&file, path, "wb") == 0 ? file : nullptr;
#else
    return std::fopen(path, "wb");
#endif
}

}  // namespace
namespace {

constexpr std::uint64_t kShowPipelines = std::uint64_t{1} << 32u;
constexpr std::uint64_t kShowProgram = NEBULA_PROGRAM_SITE | (std::uint64_t{42u} << 1u);
constexpr std::uint64_t kStageProgram = 1ull << 62u;

std::string text(const char* value) { return value != nullptr ? std::string(value) : std::string{}; }

struct Settings {
    std::string dir, programs, includes;
    bool dump = false, solo_on = false, pixel_on = false, motion_on = false, names = false, views = false;
    std::uint32_t solo = 0u;
    int x = 0, y = 0;
    std::uint32_t show = 0u;  // 1 normal, 2 depth, 3 + n coordinate n
    int stage = -1;
    std::uint64_t every = 0u;
};

bool read_point(const std::string& text, int& x, int& y) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    x = std::atoi(text.c_str());
    y = std::atoi(text.c_str() + comma + 1u);
    return true;
}

const Settings& settings() {
    static const Settings kept = [] {
        Settings s;
        const ::galaxy::RunOptions& o = ::galaxy::run_options();
        s.dir = text(o.inspect_dir);
        if (s.dir.empty()) {
            s.dir = text(o.present_snapshot_dir);
        }
        s.dump = text(o.inspect_dump) == "stops" && !s.dir.empty();
        const std::string solo = text(o.inspect_solo);
        if (!solo.empty()) {
            s.solo_on = true;
            s.solo = static_cast<std::uint32_t>(std::strtoul(solo.c_str(), nullptr, 16));
        }
        s.pixel_on = read_point(text(o.inspect_pixel), s.x, s.y) && !s.dir.empty();
        s.views = !text(o.inspect_views).empty();
        s.motion_on = read_point(text(o.inspect_motion), s.x, s.y) && !s.dir.empty();
        const std::string show = text(o.inspect_show);
        if (show == "normal") {
            s.show = 1u;
        } else if (show == "depth") {
            s.show = 2u;
        } else if (show.rfind("coord-", 0u) == 0u) {
            s.show = 3u + static_cast<std::uint32_t>(std::atoi(show.c_str() + 6u));
        } else if (show.rfind("stage-", 0u) == 0u) {
            s.stage = std::atoi(show.c_str() + 6u);
        }
        s.programs = text(o.inspect_programs);
        s.includes = text(o.inspect_includes);
        s.names = !text(o.inspect_names).empty();
        s.every = ::galaxy::run_option_u64(o.present_snapshot_every, 0u);
        return s;
    }();
    return kept;
}

struct Run {
    std::uint32_t first = 0u, count = 0u;
    bool frame = false;
};

struct State {
    std::FILE* dump = nullptr;
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint8_t>, std::uint32_t> occurrences;
    std::uint32_t last_dumped = 0u;
    bool replaying = false;
    std::vector<Run> runs;
    bool query_open = false;
    std::map<std::uint64_t, std::string> stage_sources;
    std::unordered_map<std::uint64_t, bool> stage_built;
};

State& state() {
    static State kept;
    return kept;
}

// The stop's pictures and dumps come from the same frames.
bool chosen(std::uint32_t number) noexcept {
    if (number == 0u || number % 1000u == 0u) {
        return false;
    }
    const std::uint64_t every = settings().every;
    return every != 0u ? number % every == 0u : number % 1000u == 1u;
}

void put_floats(std::FILE* f, const char* name, const float* values, std::size_t count) {
    std::fprintf(f, "\t%s=", name);
    for (std::size_t i = 0u; i < count; ++i) {
        std::fprintf(f, i != 0u ? ",%08x" : "%08x", std::bit_cast<std::uint32_t>(values[i]));
    }
}

std::uint64_t hash_bytes(const void* data, std::size_t size) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0u; i < size; ++i) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

}  // namespace

struct Access {
    static void write_item(RenderCore& core, std::FILE* f, std::uint32_t index, const NebulaFramePass& pass, std::uint8_t aux,
                           const char* extra) {
        const NebulaFrameList& list = *core.walk_.now->list;
        const NebulaFrameItem& item = list.items[index];
        State& s = state();
        const auto key = std::make_tuple(item.source_hash, item.part, pass.layer);
        const std::uint32_t occurrence = s.occurrences[key]++;
        const NebulaPipelineDesc& desc = core.registries_.pipeline(item.pipeline).desc;
        std::fprintf(f, "I\t%08x\t%u\t%u\t%u", item.source_hash, item.part, pass.layer, occurrence);
        if (extra != nullptr) {
            std::fprintf(f, "\t%s", extra);
        }
        std::fprintf(f, "\tindex=%u\tvs=%016" PRIx64 "\tps=%016" PRIx64 "\tstate=%08x\tflags=%04x\taux=%u\tmatrix=%u\tslot=%u\tgeneration=%u",
                     index, desc.vertex, desc.pixel, desc.state, item.flags, aux, item.matrix, item.slot, item.generation);
        if ((item.flags & NEBULA_ITEM_RING) != 0u) {
            const auto* vertices = static_cast<const std::byte*>(list.ring_vertices) + item.ring_at;
            std::fprintf(f, "\tring=%u,%u,%u,%u,%016" PRIx64, item.mesh, item.ring_count, item.first, item.count,
                         hash_bytes(vertices, std::size_t{item.ring_count} * item.mesh));
            if ((item.flags & NEBULA_ITEM_INDEXED) != 0u) {
                std::fprintf(f, ",%016" PRIx64, hash_bytes(list.ring_indices + item.first, std::size_t{item.count} * 4u));
            }
            if (std::size_t{item.ring_count} * item.mesh <= 256u) {
                std::fprintf(f, "\tvertices=");
                for (std::size_t w = 0; w < std::size_t{item.ring_count} * item.mesh / 4u; ++w) {
                    std::uint32_t word = 0u;
                    std::memcpy(&word, vertices + w * 4u, 4u);
                    std::fprintf(f, w == 0u ? "%08x" : ",%08x", word);
                }
            }
        } else if ((item.flags & NEBULA_ITEM_INSTANCES) != 0u) {
            // As instances: the first is the instance, not the row (particles read as before the rows were shared).
            const std::uint32_t rows = item.mesh != 0u ? item.mesh : 1u;
            std::fprintf(f, "\t%s=%u,%u,%u,%016" PRIx64, rows == core::kParticleRows ? "particles" : "glyphs", item.ring_at / rows,
                         item.ring_count, item.count, hash_bytes(list.instance_rows + item.ring_at, std::size_t{item.ring_count} * rows * 16u));
        } else {
            const core::GpuMesh& mesh = core.registries_.mesh(item.mesh);
            std::fprintf(f, "\tmesh=%u,%u,%u,%u", mesh.vertex_count, mesh.index_count, mesh.refined_vertex_count, mesh.refined_index_count);
            put_floats(f, "min", mesh.min, 3u);
            put_floats(f, "max", mesh.max, 3u);
        }
        const std::uint32_t rows_at = core.walk_block_rows_[item.block] + item.matrix * core::kRowsPerMatrix;
        put_floats(f, "eye", &core.walk_rows_cpu_[rows_at][0], 24u);
        const std::uint32_t world_at = core.walk_block_world_[item.block];
        if (world_at != core.walk_block_rows_[item.block]) {
            put_floats(f, "world", &core.walk_rows_cpu_[world_at + item.matrix * core::kRowsPerMatrix][0], 24u);
        }
        if (item.light_set != NEBULA_NONE32) {
            put_floats(f, "lights", &list.light_sets[item.light_set].lights[0].color[0], 16u * NEBULA_LIGHTS_PER_SET);
        }
        const NebulaFrameConstants& constants = list.constants[item.constants];
        const float* rows = constants.slot != NEBULA_NONE32 ? core.registries_.slot_rows(constants.slot) : &constants.rows[0][0];
        std::fprintf(f, "\tconstant_rows=%u,%u", constants.vertex_rows, constants.pixel_rows);
        if (rows != nullptr) {
            put_floats(f, "constants", rows, (std::size_t{constants.vertex_rows} + constants.pixel_rows) * 4u);
        }
        const core::TextureTable& table = core.registries_.table(item.textures);
        for (std::uint32_t t = 0u; t < table.count; ++t) {
            const NebulaFrameTexture& texture = table.list[t];
            if (texture.texture == 0u) {
                continue;
            }
            const TextureHandle handle = TextureStore::is_target(texture.texture) ? TextureHandle{} : core.texture_store_.handle(texture.texture);
            std::fprintf(f, "\ttexture%u=%016" PRIx64 ",%u,%u,%u,%u%u%u%u%u%u", t,
                         TextureStore::is_target(texture.texture) ? 1ull : core.texture_store_.name_hash(texture.texture),
                         handle.width, handle.height, handle.mip_levels, texture.wrap_s, texture.wrap_t, texture.min_linear,
                         texture.mag_linear, texture.mip_filter, texture.anisotropy);
            put_floats(f, "lod", &texture.min_lod, 3u);
            if (settings().views) {
                std::fprintf(f, "	view%u=%u,%u,%p,%" PRIx64, t, texture.texture, handle.srv_index, static_cast<const void*>(handle.resource), table.textures.ptr);
            }
        }
        const NebulaFrameView& view = list.views[item.view];
        put_floats(f, "terms", core.walk_view_terms_[item.view].data(), 6u);
        put_floats(f, "viewport", view.viewport, 6u);
        put_floats(f, "scissor", view.scissor, 4u);
        std::fprintf(f, "\n");
    }

    static void begin_walk(RenderCore& core, const NebulaFrameList& list) {
        State& s = state();
        s.occurrences.clear();
        if (s.replaying || !settings().dump || core.walk_.between || !chosen(list.snapshot_number) ||
            s.last_dumped == list.snapshot_number) {
            return;
        }
        s.last_dumped = list.snapshot_number;
        const std::string path = settings().dir + "/inspect_" + std::to_string(list.snapshot_number) + ".tsv";
        s.dump = open_for_writing(path.c_str());
        if (s.dump == nullptr) {
            return;
        }
        for (std::uint32_t p = 0u; p < list.pass_count; ++p) {
            const NebulaFramePass& pass = list.passes[p];
            std::fprintf(s.dump, "P\t%u\tkind=%u\tlayer=%u\tphase=%u\ttarget=%s\titems=%u\tclear=%08x\tchannels=%u\targs=%08x,%08x\n", p, pass.kind,
                         pass.layer, pass.phase, pass.target != 0u ? "yes" : "no", pass.item_count, pass.clear_rgba, pass.channels, pass.arg0, pass.arg1);
        }
    }

    // The program of stage `n` of `pixel`, compiled once per variant from the text; 0 when it can't be.
    static std::uint64_t stage_program(std::uint64_t pixel) {
        const Settings& c = settings();
        State& s = state();
        const std::uint64_t id = pixel ^ kStageProgram;
        const auto known = s.stage_built.find(id);
        if (known != s.stage_built.end()) {
            return known->second ? id : 0u;
        }
        bool built = false;
        if (!c.programs.empty() && (pixel & NEBULA_PROGRAM_SITE) == 0u) {
            if (s.stage_sources.empty()) {
                std::ifstream table(c.programs + "/table.tsv");
                std::string stage, text, group;
                while (table >> stage >> text >> group) {
                    if (stage == "pixel" || stage == "p") {
                        s.stage_sources[std::strtoull(text.c_str(), nullptr, 16)] = group;
                    }
                }
            }
            const auto named = s.stage_sources.find(pixel);
            const std::string file = c.programs + "/p" + (named != s.stage_sources.end() ? named->second : std::string{}) + ".hlsl";
            std::ifstream in(file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            if (in && named != s.stage_sources.end()) {
                const std::string stage_define = "NATIVE_SHOW_STAGE=" + std::to_string(c.stage);
                const std::filesystem::path includes = c.includes.empty() ? std::filesystem::path(c.programs) / ".." / "materials" : std::filesystem::path(c.includes);
                struct Form {
                    unsigned variant;
                    std::vector<const char*> defines;
                };
                const Form forms[] = {{kProgramBase, {}},
                                      {kProgramReactive, {"NATIVE_REACTIVE"}},
                                      {kProgramLayer, {"NATIVE_REACTIVE", "NATIVE_LAYER"}},
                                      {kProgramMotion, {"NATIVE_MOTION"}},
                                      {kProgramSample, {"NATIVE_SAMPLE"}}};
                built = true;
                for (const Form& form : forms) {
                    std::vector<const char*> defines = form.defines;
                    defines.push_back(stage_define.c_str());
                    std::string problem;
                    const bool bound = program_bound_tables();
                    std::vector<std::uint8_t> bytes = compile_mod_shader_stage(text.str(), includes, true, defines, problem, bound);
                    if (bytes.empty()) {
                        std::fprintf(stderr, "[inspect] stage of %016" PRIx64 ": %s\n", pixel, problem.c_str());
                        built = built && form.variant == kProgramSample;
                    } else {
                        add_mod_program(id, form.variant + (bound ? kProgramBoundOffset : 0u), true, std::move(bytes));
                    }
                }
            }
        }
        s.stage_built[id] = built;
        return built ? id : 0u;
    }

    static bool item(RenderCore& core, std::uint32_t index, std::uint32_t instances, const NebulaFramePass& pass, std::uint8_t aux,
                     gpu::Pipeline*& pipeline, const core::PipelineEntry& entry) {
        const Settings& c = settings();
        State& s = state();
        const NebulaFrameItem& first = core.walk_.now->list->items[index];
        if (c.solo_on && first.source_hash != c.solo && first.slot != c.solo) {
            return false;
        }
        if (s.dump != nullptr) {
            for (std::uint32_t n = 0u; n < instances; ++n) {
                write_item(core, s.dump, index + n, pass, aux, nullptr);
            }
            // One GPU draw: its first item and how many items it draws (instanced meshes, merged ring draws).
            std::fprintf(s.dump, "D\t%u\t%u\t%u\n", pass.layer, index, instances);
        }
        if (c.show != 0u || c.stage >= 0) {
            core::PipelineEntry shown = entry;
            bool replaced = false;
            if (c.show != 0u) {
                shown.desc.vertex = kShowProgram;
                shown.desc.pixel = kShowProgram;
                replaced = true;
            } else if (const std::uint64_t program = stage_program(entry.desc.pixel); program != 0u) {
                shown.desc.pixel = program;
                replaced = true;
            }
            if (replaced) {
                shown.desc.state &= ~std::uint32_t{0x3FFu};  // no blending: the value itself
                gpu::Pipeline* const got = core.pipelines_.get(kShowPipelines + first.pipeline, shown, core.renderer_.scene_variant(), aux);
                if (got != nullptr) {
                    pipeline = got;
                }
            }
        }
        if (c.names) {
            char name[64];
            std::snprintf(name, sizeof(name), "%08x/%u slot %u", first.source_hash, first.part, first.slot);
            core.renderer_.inspect_mark(name);
        }
        return true;
    }

    static void draw_begin(RenderCore& core, std::uint32_t index, std::uint32_t instances, const NebulaFramePass& pass, gpu::Encoder& encoder,
                           const gpu::Rect& scissor) {
        State& s = state();
        if (!s.replaying || pass.target != 0u) {
            return;
        }
        const Settings& c = settings();
        const gpu::Rect pixel = gpu::rect(std::max<std::int32_t>(c.x, scissor.left), std::max<std::int32_t>(c.y, scissor.top),
                                          std::min<std::int32_t>(c.x + 1, scissor.right), std::min<std::int32_t>(c.y + 1, scissor.bottom));
        if (pixel.left >= pixel.right || pixel.top >= pixel.bottom) {
            encoder.set_scissor(gpu::rect(0, 0, 0, 0));
        } else {
            encoder.set_scissor(pixel);
        }
        core.renderer_.inspect_query_begin(static_cast<std::uint32_t>(s.runs.size()));
        s.runs.push_back({index, instances, true});
        s.query_open = true;
    }

    static void draw_end(RenderCore& core) {
        State& s = state();
        if (s.query_open) {
            s.query_open = false;
            core.renderer_.inspect_query_end(static_cast<std::uint32_t>(s.runs.size() - 1u));
        }
    }

    // The top item's vertex nearest the pixel: where it should move from, against what the motion target holds.
    static void write_motion(RenderCore& core, std::FILE* f, std::uint32_t index) {
        const Settings& c = settings();
        const NebulaFrameList& list = *core.walk_.now->list;
        const NebulaFrameItem& item = list.items[index];
        if ((item.flags & NEBULA_ITEM_INSTANCES) != 0u) {
            std::fprintf(f, "M\tindex=%u\tparticles\n", index);  // corners the vertex stage builds: none to read here
            return;
        }
        const bool ring = (item.flags & NEBULA_ITEM_RING) != 0u;
        const core::GpuMesh* mesh = ring ? nullptr : &core.registries_.mesh(item.mesh);
        const std::byte* positions = ring ? static_cast<const std::byte*>(list.ring_vertices) + item.ring_at
                                          : core.registries_.mesh_cpu(*mesh, mesh->positions);
        const std::uint32_t stride = ring ? item.mesh : mesh->site_stride != 0u ? mesh->site_stride : 12u;
        const std::uint32_t count = ring ? item.ring_count : mesh->vertex_count;
        const auto* matrices = !ring && mesh->matrices != 0u && (item.flags & NEBULA_ITEM_ONE_MATRIX) == 0u
                                   ? reinterpret_cast<const std::uint32_t*>(core.registries_.mesh_cpu(*mesh, mesh->matrices))
                                   : nullptr;
        const NebulaFrameView& view = list.views[item.view];
        const ScaledViewport vp = viewport_pixels(view.viewport, core.renderer_.frame_map());
        const auto project = [&](const float (*rows)[4], const std::array<float, 6>& terms, const float* p, float* out) {
            float eye[3];
            for (unsigned r = 0u; r < 3u; ++r) {
                eye[r] = rows[r][0] * p[0] + rows[r][1] * p[1] + rows[r][2] * p[2] + rows[r][3];
            }
            float x, y, w;
            if (view.orthographic != 0u) {
                x = terms[0] * eye[0] + terms[1];
                y = terms[2] * eye[1] + terms[3];
                w = 1.0f;
            } else {
                x = terms[0] * eye[0] + terms[1] * eye[2];
                y = terms[2] * eye[1] + terms[3] * eye[2];
                w = -eye[2];
            }
            out[0] = vp.x + (x / w * 0.5f + 0.5f) * vp.width;
            out[1] = vp.y + (0.5f - y / w * 0.5f) * vp.height;
        };
        const auto* ring_rows = reinterpret_cast<const float (*)[4]>(core.renderer_.matrix_ring().cpu_base());
        float best = 1e30f, computed[2]{};
        std::uint32_t best_vertex = NEBULA_NONE32;
        for (std::uint32_t v = 0u; v < count; ++v) {
            float p[3];
            std::memcpy(p, positions + std::size_t{v} * stride, sizeof(p));
            const std::uint32_t matrix = matrices != nullptr ? matrices[v] : item.matrix;
            const float (*now)[4] = core.walk_rows_cpu_ + core.walk_block_rows_[item.block] + matrix * core::kRowsPerMatrix;
            const float (*before)[4] = ring_rows + core.walk_previous_[item.block] + matrix * core::kRowsPerMatrix;
            float here[2], was[2];
            project(now, core.walk_view_terms_[item.view], p, here);
            const float dx = here[0] - static_cast<float>(c.x), dy = here[1] - static_cast<float>(c.y);
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                best_vertex = v;
                project(before, core.walk_view_previous_[item.view], p, was);
                computed[0] = was[0] - here[0];
                computed[1] = was[1] - here[1];
            }
        }
        std::uint16_t halves[2]{};
        const bool got = core.renderer_.inspect_motion_pixel(static_cast<unsigned>(c.x), static_cast<unsigned>(c.y), halves);
        const float read[2] = {half_to_float(halves[0]), half_to_float(halves[1])};
        std::fprintf(f, "M\tindex=%u\tvertex=%u\tdistance=%g\tcomputed=%g,%g\tread=%s%g,%g\n", index, best_vertex, best < 1e29f ? std::sqrt(best) : -1.0f,
                     computed[0], computed[1], got ? "" : "none ", read[0], read[1]);
    }

    static void end_walk(RenderCore&) {
        State& s = state();
        if (s.dump != nullptr) {
            std::fclose(s.dump);
            s.dump = nullptr;
        }
    }

    static void after_frame(RenderCore& core, void* opaque) {
        const Settings& c = settings();
        State& s = state();
        auto& entry = *static_cast<RenderCore::ListEntry*>(opaque);
        const std::uint32_t number = entry.data.list->snapshot_number;
        if (s.replaying || (!c.pixel_on && !c.motion_on) || !chosen(number)) {
            return;
        }
        s.replaying = true;
        s.runs.clear();
        core.renderer_.inspect_queries_open(entry.data.list->item_count);
        core.walk_frame(entry, core::WalkTime{}, false);
        core.renderer_.wait_for_gpu_idle();
        std::vector<std::uint64_t> hits(s.runs.size());
        core.renderer_.inspect_queries_read(static_cast<std::uint32_t>(hits.size()), hits.data());
        const std::string path = c.dir + "/pixel_" + std::to_string(number) + ".tsv";
        std::FILE* const f = open_for_writing(path.c_str());
        if (f != nullptr) {
            std::fprintf(f, "# pixel %d,%d\n", c.x, c.y);
            const NebulaFrameList& list = *entry.data.list;
            std::vector<std::uint32_t> pass_of(list.item_count, 0u);
            for (std::uint32_t p = 0u; p < list.pass_count; ++p) {
                for (std::uint32_t i = list.passes[p].first_item; list.passes[p].kind == NEBULA_PASS_DRAW && i < list.passes[p].first_item + list.passes[p].item_count; ++i) {
                    pass_of[i] = p;
                }
            }
            s.occurrences.clear();
            std::uint32_t top = NEBULA_NONE32;
            for (std::size_t r = 0u; r < s.runs.size(); ++r) {
                if (hits[r] == 0u) {
                    continue;
                }
                top = s.runs[r].first;
                write_item(core, f, s.runs[r].first, list.passes[pass_of[s.runs[r].first]], 0u, "hit=1");
            }
            if (c.motion_on && top != NEBULA_NONE32) {
                write_motion(core, f, top);
            }
            std::fclose(f);
        }
        s.replaying = false;
    }
};

void begin_walk(RenderCore& core, const NebulaFrameList& list) {
    Access::begin_walk(core, list);
}
bool item(RenderCore& core, std::uint32_t index, std::uint32_t instances, const NebulaFramePass& pass, std::uint8_t aux, gpu::Pipeline*& pipeline,
          const core::PipelineEntry& entry) {
    return Access::item(core, index, instances, pass, aux, pipeline, entry);
}
void draw_begin(RenderCore& core, std::uint32_t index, std::uint32_t instances, const NebulaFramePass& pass, gpu::Encoder& encoder, const gpu::Rect& scissor) {
    Access::draw_begin(core, index, instances, pass, encoder, scissor);
}
void draw_end(RenderCore& core) {
    Access::draw_end(core);
}
void end_walk(RenderCore& core) {
    Access::end_walk(core);
}
void after_frame(RenderCore& core, void* entry) {
    Access::after_frame(core, entry);
}
bool replaying() noexcept {
    return state().replaying;
}
std::uint32_t show_flags() noexcept {
    return settings().show << 8u;
}

}  // namespace galaxy::render::inspector

#endif
