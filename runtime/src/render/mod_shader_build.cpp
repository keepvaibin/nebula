// SPDX-License-Identifier: GPL-3.0-only
// Programs a mod needs and didn't bring, or brought for an earlier Nebula: built here from the texts the cooker wrote, in the file
// format of mod_shaders.h. Each graphics interface compiles a stage its own way (compile_mod_shader_stage).
#include "galaxy/cpu_count.h"
#include "galaxy/render/mod_shaders.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace galaxy::render {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kHeaderBytes = 56u;
constexpr std::size_t kEntryBytes = 32u;

struct Variant {
    std::uint8_t number;
    bool vertex;
    bool only_with_discard;
    std::vector<const char*> defines;
    bool tracer = false;  // ray tracing's: never in the bound-table form
};

// compile_programs.py's forms of a generated program.
const std::vector<Variant>& variants() {
    static const std::vector<Variant> list = {
        {kProgramBase, true, false, {}},
        {kProgramReactive, false, false, {"NATIVE_REACTIVE"}},
        {kProgramLayer, false, false, {"NATIVE_REACTIVE", "NATIVE_LAYER"}},
        {kProgramMotion, true, false, {"NATIVE_MOTION"}},
        {kProgramSample, false, true, {"NATIVE_SAMPLE"}},
        {kProgramPrepass, false, false, {"NATIVE_PREPASS"}, true},
        {kProgramTraced, false, false, {"NATIVE_TRACED"}, true},
        {kProgramTracedMotion, false, false, {"NATIVE_TRACED", "NATIVE_MOTION"}, true},
    };
    return list;
}

std::string read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

using Program = std::pair<std::uint64_t, bool>;  // id, pixel

// The programs a file of this build's interface holds.
void programs_in(const fs::path& file, std::set<Program>& found) {
    const std::string bytes = read_all(file);
    if (bytes.size() < kHeaderBytes || bytes.compare(0, 8, "NBSHADER") != 0 || bytes.compare(16, 40, program_interface(), 40) != 0) {
        return;
    }
    std::uint32_t count = 0u;
    std::memcpy(&count, bytes.data() + 12, 4);
    if (kHeaderBytes + std::uint64_t{count} * kEntryBytes > bytes.size()) {
        return;
    }
    for (std::uint32_t i = 0u; i < count; ++i) {
        const char* entry = bytes.data() + kHeaderBytes + i * kEntryBytes;
        std::uint64_t id = 0u;
        std::memcpy(&id, entry, 8);
        found.emplace(id, entry[9] != 0);
    }
}

struct Stage {
    std::uint64_t id;
    std::uint8_t variant;
    bool pixel;
    std::vector<std::uint8_t> bytes;
};

std::string file_of(const std::vector<Stage>& stages) {
    std::string out(kHeaderBytes + stages.size() * kEntryBytes, '\0');
    std::memcpy(out.data(), "NBSHADER", 8);
    const std::uint32_t version = 3u;
    const auto count = static_cast<std::uint32_t>(stages.size());
    std::memcpy(out.data() + 8, &version, 4);
    std::memcpy(out.data() + 12, &count, 4);
    std::memcpy(out.data() + 16, program_interface(), 40);
    for (std::size_t i = 0u; i < stages.size(); ++i) {
        const Stage& stage = stages[i];
        out.resize((out.size() + 7u) & ~std::size_t{7});
        const std::uint64_t offset = out.size();
        const std::uint64_t size = stage.bytes.size();
        out.append(reinterpret_cast<const char*>(stage.bytes.data()), stage.bytes.size());
        char* entry = out.data() + kHeaderBytes + i * kEntryBytes;
        std::memcpy(entry, &stage.id, 8);
        entry[8] = static_cast<char>(stage.variant);
        entry[9] = stage.pixel ? 1 : 0;
        std::memcpy(entry + 16, &offset, 8);
        std::memcpy(entry + 24, &size, 8);
    }
    return out;
}

}  // namespace

ModShaderBuild build_missing_mod_shaders(const std::filesystem::path& sources, const std::vector<std::filesystem::path>& brought,
                                         const std::filesystem::path& output, const std::function<void(unsigned, unsigned)>& progress) {
    ModShaderBuild result;
    std::error_code error;
    fs::remove(output, error);
    std::set<Program> present;
    for (const fs::path& file : brought) {
        programs_in(file, present);
    }
    struct Job {
        std::uint64_t id;
        bool pixel;
        std::string text;
    };
    std::vector<Job> jobs;
    // increment(ec) ends the list when the folder goes away mid-walk; the range-for form throws there.
    for (auto it = fs::directory_iterator(sources, error); !error && it != fs::directory_iterator(); it.increment(error)) {
        const fs::directory_entry& entry = *it;
        const std::string name = entry.path().filename().string();
        if (name.size() != 22u || (name[0] != 'v' && name[0] != 'p') || !name.ends_with(".hlsl")) {
            continue;
        }
        const std::uint64_t id = std::strtoull(name.substr(1, 16).c_str(), nullptr, 16);
        const bool pixel = name[0] == 'p';
        if (present.count({id, pixel}) == 0u && program_bytes(id, kProgramBase, pixel).size == 0u) {
            jobs.push_back({id, pixel, read_all(entry.path())});
        }
    }
    if (jobs.empty()) {
        return result;
    }
    result.wanted = static_cast<unsigned>(jobs.size());
    std::mutex lock;
    std::vector<Stage> built;
    std::atomic<std::size_t> next{0u};
    std::atomic<unsigned> done{0u};
    // One worker per logical processor, and no more than there are programs to build.
    const auto threads = static_cast<unsigned>(std::min<std::size_t>(logical_cores(), jobs.size()));
    std::vector<std::thread> pool;
    for (unsigned t = 0u; t < threads; ++t) {
        pool.emplace_back([&]() {
            for (std::size_t at = next.fetch_add(1u); at < jobs.size(); at = next.fetch_add(1u)) {
                const Job& job = jobs[at];
                std::vector<Stage> stages;
                bool good = true;
                for (const Variant& variant : variants()) {
                    if ((!job.pixel && !variant.vertex) ||
                        (variant.only_with_discard && job.text.find("discard") == std::string::npos)) {
                        continue;
                    }
                    // A pixel program's second form, where this build has one.
                    for (const bool bound : {false, true}) {
                        if (bound && (!job.pixel || variant.tracer || !mod_shader_has_bound_form())) {
                            continue;
                        }
                        std::string problem;
                        std::vector<std::uint8_t> bytes =
                            compile_mod_shader_stage(job.text, sources, job.pixel, variant.defines, problem, bound);
                        if (bytes.empty()) {
                            good = false;
                            const std::lock_guard<std::mutex> guard(lock);
                            if (result.problem.empty()) {
                                result.problem = problem;
                            }
                            break;
                        }
                        const unsigned stored = variant.number + (bound ? kProgramBoundOffset : 0u);
                        stages.push_back({job.id, static_cast<std::uint8_t>(stored), job.pixel, std::move(bytes)});
                    }
                    if (!good) {
                        break;
                    }
                }
                if (good) {
                    const std::lock_guard<std::mutex> guard(lock);
                    built.insert(built.end(), std::make_move_iterator(stages.begin()), std::make_move_iterator(stages.end()));
                    ++result.built;
                }
                const unsigned count = done.fetch_add(1u) + 1u;
                if (progress) {
                    progress(count, static_cast<unsigned>(jobs.size()));
                }
            }
        });
    }
    for (std::thread& thread : pool) {
        thread.join();
    }
    if (!built.empty()) {
        // Written once, whole: to a temporary file beside it, then renamed over, so a crash or a full disk never leaves a
        // half-written file for the next start to read.
        const std::string bytes = file_of(built);
        fs::path temporary = output;
        temporary += ".tmp";
        bool written = false;
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            written = static_cast<bool>(out);
        }
        std::error_code rename_error;
        if (written) {
            fs::rename(temporary, output, rename_error);
        }
        if (!written || rename_error) {
            fs::remove(temporary, rename_error);
            if (result.problem.empty()) {
                result.problem = "the built shaders could not be written";
            }
        }
    }
    return result;
}

}  // namespace galaxy::render
