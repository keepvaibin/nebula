// SPDX-License-Identifier: GPL-3.0-only
// Shaders a mod needs and didn't bring, or brought for an earlier Nebula: built here from the sources the cooker wrote, in the
// file format of mod_shaders.h. Each graphics interface compiles a stage its own way (compile_mod_shader_stage).
#include "galaxy/render/mod_shaders.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>

namespace galaxy::render {
namespace {

namespace fs = std::filesystem;

struct Variant {
    std::uint8_t number;
    std::uint8_t vertex_of;  // a per-sample variant's vertex stage is another variant's
    std::vector<const char*> defines;
};

// As native_shaders.h numbers them; what LumaLink's own conversion builds.
const std::vector<Variant>& variants() {
    static const std::vector<Variant> list = {
        {0, 0, {}},
        {4, 4, {"NATIVE_DUAL"}},
        {5, 0, {"NATIVE_SAMPLE"}},
        {6, 4, {"NATIVE_SAMPLE", "NATIVE_DUAL"}},
        {7, 7, {"NATIVE_PIXEL_LIT"}},
        {8, 8, {"NATIVE_PIXEL_LIT", "NATIVE_DUAL"}},
        {9, 7, {"NATIVE_PIXEL_LIT", "NATIVE_SAMPLE"}},
        {10, 8, {"NATIVE_PIXEL_LIT", "NATIVE_SAMPLE", "NATIVE_DUAL"}},
    };
    return list;
}

bool has_define(const Variant& variant, const char* name) {
    return std::any_of(variant.defines.begin(), variant.defines.end(), [name](const char* d) { return std::strcmp(d, name) == 0; });
}

bool material_has(const std::string& source, const Variant& variant) {
    return (!has_define(variant, "NATIVE_PIXEL_LIT") || source.find("NATIVE_PIXEL_LIT") != std::string::npos) &&
           (!has_define(variant, "NATIVE_SAMPLE") || source.find("discard") != std::string::npos);
}

std::string read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The materials a shader file of this build's interface holds.
void materials_in(const fs::path& file, std::set<std::uint32_t>& found) {
    const std::string bytes = read_all(file);
    if (bytes.size() < 56u || bytes.compare(0, 8, "NBSHADER") != 0 || bytes.compare(16, 40, native_shader_interface(), 40) != 0) {
        return;
    }
    std::uint32_t count = 0u;
    std::memcpy(&count, bytes.data() + 12, 4);
    if (56u + std::uint64_t{count} * 24u > bytes.size()) {
        return;
    }
    for (std::uint32_t i = 0u; i < count; ++i) {
        std::uint32_t id = 0u;
        std::memcpy(&id, bytes.data() + 56 + i * 24u, 4);
        found.insert(id);
    }
}

struct Stage {
    std::uint8_t variant;
    bool pixel;
    std::vector<std::uint8_t> bytes;
};

void put32(std::string& out, std::size_t at, std::uint32_t value) {
    std::memcpy(out.data() + at, &value, 4);
}

void put64(std::string& out, std::size_t at, std::uint64_t value) {
    std::memcpy(out.data() + at, &value, 8);
}

std::string file_of(const std::map<std::uint32_t, std::vector<Stage>>& materials) {
    struct Row {
        std::uint32_t id;
        std::uint8_t variant;
        bool pixel;
        std::uint8_t holder;
    };
    std::vector<Row> rows;
    for (const auto& [id, stages] : materials) {
        for (const Stage& stage : stages) {
            rows.push_back({id, stage.variant, stage.pixel, stage.variant});
            if (stage.pixel && std::none_of(stages.begin(), stages.end(), [&](const Stage& s) { return s.variant == stage.variant && !s.pixel; })) {
                const auto it = std::find_if(variants().begin(), variants().end(), [&](const Variant& v) { return v.number == stage.variant; });
                rows.push_back({id, stage.variant, false, it->vertex_of});
            }
        }
    }
    std::string out(56u + rows.size() * 24u, '\0');
    std::memcpy(out.data(), "NBSHADER", 8);
    put32(out, 8, 1u);
    put32(out, 12, static_cast<std::uint32_t>(rows.size()));
    std::memcpy(out.data() + 16, native_shader_interface(), 40);
    std::map<std::tuple<std::uint32_t, std::uint8_t, bool>, std::pair<std::uint64_t, std::uint64_t>> places;
    for (const auto& [id, stages] : materials) {
        for (const Stage& stage : stages) {
            out.resize((out.size() + 7u) & ~std::size_t{7});
            places[{id, stage.variant, stage.pixel}] = {out.size(), stage.bytes.size()};
            out.append(reinterpret_cast<const char*>(stage.bytes.data()), stage.bytes.size());
        }
    }
    for (std::size_t i = 0u; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const auto [offset, size] = places[{row.id, row.holder, row.pixel}];
        const std::size_t at = 56u + i * 24u;
        put32(out, at, row.id);
        out[at + 4u] = static_cast<char>(row.variant);
        out[at + 5u] = row.pixel ? 1 : 0;
        put64(out, at + 8u, offset);
        put64(out, at + 16u, size);
    }
    return out;
}

}  // namespace

ModShaderBuild build_missing_mod_shaders(const std::filesystem::path& sources, const std::vector<std::filesystem::path>& brought,
                                         const std::filesystem::path& output, const std::function<void(unsigned, unsigned)>& progress) {
    ModShaderBuild result;
    std::error_code error;
    fs::remove(output, error);
    std::set<std::uint32_t> present;
    for (const fs::path& file : brought) {
        materials_in(file, present);
    }
    struct Job {
        std::uint32_t id;
        std::string source;
    };
    std::vector<Job> jobs;
    for (const auto& entry : fs::directory_iterator(sources, error)) {
        const std::string name = entry.path().filename().string();
        if (name.size() != 14u || name[0] != 'm' || !name.ends_with(".hlsl")) {
            continue;
        }
        const std::uint32_t id = static_cast<std::uint32_t>(std::strtoul(name.substr(1, 8).c_str(), nullptr, 16));
        if (present.count(id) == 0u) {
            jobs.push_back({id, read_all(entry.path())});
        }
    }
    if (jobs.empty()) {
        return result;
    }
    result.wanted = static_cast<unsigned>(jobs.size());
    std::mutex lock;
    std::map<std::uint32_t, std::vector<Stage>> built;
    std::atomic<std::size_t> next{0u};
    std::atomic<unsigned> done{0u};
    const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
    std::vector<std::thread> pool;
    for (unsigned t = 0u; t < threads; ++t) {
        pool.emplace_back([&]() {
            for (std::size_t at = next.fetch_add(1u); at < jobs.size(); at = next.fetch_add(1u)) {
                const Job& job = jobs[at];
                std::vector<Stage> stages;
                bool good = true;
                for (const Variant& variant : variants()) {
                    if (!material_has(job.source, variant)) {
                        continue;
                    }
                    for (const bool pixel : {false, true}) {
                        if (!pixel && variant.vertex_of != variant.number) {
                            continue;
                        }
                        std::string problem;
                        std::vector<std::uint8_t> bytes = compile_mod_shader_stage(job.source, sources, pixel, variant.defines, problem);
                        if (bytes.empty()) {
                            good = false;
                            const std::lock_guard<std::mutex> guard(lock);
                            if (result.problem.empty()) result.problem = problem;
                            break;
                        }
                        stages.push_back({variant.number, pixel, std::move(bytes)});
                    }
                    if (!good) break;
                }
                {
                    const std::lock_guard<std::mutex> guard(lock);
                    if (good) built[job.id] = std::move(stages);
                }
                const unsigned count = done.fetch_add(1u) + 1u;
                if (progress) progress(count, static_cast<unsigned>(jobs.size()));
            }
        });
    }
    for (std::thread& thread : pool) {
        thread.join();
    }
    result.built = static_cast<unsigned>(built.size());
    if (!built.empty()) {
        std::ofstream out(output, std::ios::binary);
        const std::string bytes = file_of(built);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    return result;
}

}  // namespace galaxy::render
