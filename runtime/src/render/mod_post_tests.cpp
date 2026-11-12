// SPDX-License-Identifier: GPL-3.0-only
// draw.post's registry (mod_post.cpp) without a device: post_register's results, the prelude around the mod's text, ids
// never reused, freed programs gone, the no-motion refusal, the error cut to its capacity.
#include "galaxy/render/mod_post.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
        ++g_failures;
    }
}

int g_compiles = 0;
std::string g_last_source;
bool g_last_pixel = false;

// A compiler that "compiles" any text without "bad" in it: its bytes are the stage's name.
std::vector<std::uint8_t> fake_compiler(const std::string& source, const std::filesystem::path& work, bool pixel,
                                        std::string& problem) {
    ++g_compiles;
    g_last_source = source;
    g_last_pixel = pixel;
    if (work.empty() || !std::filesystem::is_directory(work)) {
        problem = "no work folder";
        return {};
    }
    if (source.find("bad") != std::string::npos) {
        problem = "note: before\nprogram.hlsl(3,1): error: undeclared identifier 'bad'\nmore";
        return {};
    }
    const char* name = pixel ? "pixel" : "vertex";
    return std::vector<std::uint8_t>(name, name + std::strlen(name));
}

}  // namespace

int main() {
    using namespace galaxy::render;
    const std::filesystem::path work = std::filesystem::temp_directory_path() / "nebula_mod_post_tests";
    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
    set_mod_post_work_folder(work);
    const std::string text = "float4 post(PostIn i) { return scene(i.uv); }";
    char error[256];
    std::uint32_t id = 77u;

    // No compiler: refused, id 0.
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), 0u, &id, error, sizeof(error)) ==
               kModPostCompile,
           "no compiler: refused");
    expect(id == 0u, "no compiler: id 0");
    expect(std::string(error).find("no compiler") != std::string::npos, "no compiler: said");

    set_mod_post_compiler(&fake_compiler, false);
    // A good program: its pixel stage, then the host's vertex stage once; the work folder made.
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), kModPostReadsDepth, &id, error,
                             sizeof(error)) == kModPostOk,
           "good program: ok");
    expect(id != 0u, "good program: an id");
    expect(std::filesystem::is_directory(work), "the work folder is made");
    expect(g_compiles == 2, "pixel and vertex compiled");
    expect(g_last_source.find(text) != std::string::npos && g_last_source.find("float4 program_ps") != std::string::npos &&
               g_last_source.find("struct PostIn") < g_last_source.find(text),
           "prelude, text, entry in order");
    expect(g_last_source.find("#define NEBULA_POST_SPIRV") == std::string::npos, "D3D12 source does not define SPIR-V");
    const auto program = mod_post_program(id);
    expect(program != nullptr && program->reads == kModPostReadsDepth && program->id == id, "the program is kept");
    expect(program != nullptr && program->pixel.size() == 5u && program->vertex.size() == 6u, "its stages");

    // A second program: the vertex stage is not compiled again; a new id.
    std::uint32_t second = 0u;
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), 0u, &second, error, sizeof(error)) ==
               kModPostOk,
           "second program: ok");
    expect(g_compiles == 3, "second program: pixel stage only");
    expect(second != 0u && second != id, "second program: its own id");

    // Freed: gone, the epoch moves, the id is never given again.
    const std::uint64_t epoch = mod_post_epoch();
    mod_post_free(id);
    expect(mod_post_program(id) == nullptr, "freed: gone");
    expect(mod_post_epoch() != epoch, "freed: the epoch moved");
    std::uint32_t third = 0u;
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), 0u, &third, error, sizeof(error)) ==
               kModPostOk,
           "third program: ok");
    expect(third != id && third != second, "ids are not reused");
    const std::uint64_t unchanged = mod_post_epoch();
    mod_post_free(12345u);
    expect(mod_post_epoch() == unchanged, "freeing an unknown id changes nothing");

    // The compiler's refusal: its first line naming an error, cut to the capacity and ended.
    const std::string bad = "float4 post(PostIn i) { return bad; }";
    expect(mod_post_register(bad.data(), static_cast<std::uint32_t>(bad.size()), 0u, &id, error, sizeof(error)) ==
               kModPostCompile,
           "bad program: refused");
    expect(id == 0u, "bad program: id 0");
    expect(std::string(error) == "program.hlsl(3,1): error: undeclared identifier 'bad'", "bad program: the error line");
    char small[8];
    std::memset(small, 'x', sizeof(small));
    (void)mod_post_register(bad.data(), static_cast<std::uint32_t>(bad.size()), 0u, &id, small, sizeof(small));
    expect(small[7] == '\0' && std::strlen(small) == 7u, "the error is cut and ended");

    // Motion: refused without it (NO_MOTION), taken with it.
    set_mod_post_motion(false);
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), kModPostReadsMotion, &id, error,
                             sizeof(error)) == kModPostNoMotion,
           "motion without it: NO_MOTION");
    expect(std::strncmp(error, "no motion", 9) == 0, "motion without it: said");
    set_mod_post_motion(true);
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), kModPostReadsMotion, &id, error,
                             sizeof(error)) == kModPostOk,
           "motion with it: ok");

    // Arguments: an unknown reads bit, empty text, a NUL inside, no place for the id.
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), 4u, &id, error, sizeof(error)) ==
               kModPostCompile,
           "unknown reads bit: refused");
    expect(mod_post_register(text.data(), 0u, 0u, &id, error, sizeof(error)) == kModPostCompile, "empty text: refused");
    const char nul[] = {'a', '\0', 'b'};
    expect(mod_post_register(nul, 3u, 0u, &id, error, sizeof(error)) == kModPostCompile, "a NUL inside: refused");
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), 0u, nullptr, error, sizeof(error)) ==
               kModPostCompile,
           "no id place: refused");
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), 0u, &id, nullptr, 0u) == kModPostOk,
           "no error buffer: fine");

    // SPIR-V: the define leads; a new compiler compiles the vertex stage again.
    set_mod_post_compiler(&fake_compiler, true);
    const int before = g_compiles;
    expect(mod_post_register(text.data(), static_cast<std::uint32_t>(text.size()), 0u, &id, error, sizeof(error)) ==
               kModPostOk,
           "spirv program: ok");
    expect(g_compiles == before + 2, "a new compiler: the vertex stage again");
    expect(g_last_source.rfind("#define NEBULA_POST_SPIRV 1\n", 0) == 0, "spirv source starts with its define");

    set_mod_post_compiler(nullptr, false);
    std::filesystem::remove_all(work, ignored);
    if (g_failures == 0) {
        std::puts("mod_post tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
