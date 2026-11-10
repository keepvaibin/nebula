// SPDX-License-Identifier: GPL-3.0-only
// The programs' file for the Metal renderer: tools/dev/compile_programs.py's SPIR-V file with every stage turned to
// MSL text (runtime/src/render/mtl/shader_msl.cpp). The same header and rows (core/program_table.cpp reads either);
// a stage's bytes are its MSL and a terminating zero.
//
//   programs_msl <programs_spirv.bin> <programs_msl.bin> macos|ios
#include "galaxy/render/mtl/shader_msl.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kHeader = 64u;
constexpr unsigned kStored = 10u;       // kProgramStored
constexpr unsigned kBoundOffset = 5u;   // kProgramBoundOffset
constexpr std::size_t kRow = 16u + 8u * kStored;

std::uint32_t word(const std::vector<unsigned char>& bytes, std::size_t at) {
    std::uint32_t value = 0u;
    std::memcpy(&value, bytes.data() + at, 4u);
    return value;
}

struct Stage {
    std::uint32_t offset = 0u, size = 0u;
    bool pixel = false, bound = false;
    std::string msl, error;
    std::uint32_t placed = 0u;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: programs_msl <programs_spirv.bin> <programs_msl.bin> macos|ios\n");
        return 2;
    }
    const bool ios = std::strcmp(argv[3], "ios") == 0;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> in((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (in.size() < kHeader || std::memcmp(in.data(), "NBPROG02", 8u) != 0) {
        std::fprintf(stderr, "%s is not a programs file\n", argv[1]);
        return 1;
    }
    const std::uint32_t vertex_rows = word(in, 8u), pixel_rows = word(in, 12u);
    const std::size_t rows = std::size_t{vertex_rows} + pixel_rows;
    if (in.size() < kHeader + rows * kRow) {
        std::fprintf(stderr, "%s is cut short\n", argv[1]);
        return 1;
    }
    // Each distinct stage once: a pixel stage in its bound-table form is another module than the same text's array form.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> index;
    std::vector<Stage> stages;
    for (std::size_t row = 0; row < rows; ++row) {
        const bool pixel = row >= vertex_rows;
        for (unsigned variant = 0; variant < kStored; ++variant) {
            const std::size_t at = kHeader + row * kRow + 16u + variant * 8u;
            const std::uint32_t offset = word(in, at), size = word(in, at + 4u);
            if (size == 0u) {
                continue;
            }
            if (std::size_t{offset} + size > in.size() || (size & 3u) != 0u) {
                std::fprintf(stderr, "row %zu variant %u lies outside the file\n", row, variant);
                return 1;
            }
            if (index.emplace(std::make_pair(offset, size), stages.size()).second) {
                Stage stage;
                stage.offset = offset;
                stage.size = size;
                stage.pixel = pixel;
                stage.bound = pixel && variant >= kBoundOffset;
                stages.push_back(std::move(stage));
            }
        }
    }

    std::atomic<std::size_t> next{0u};
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&] {
            for (std::size_t at = next.fetch_add(1u); at < stages.size(); at = next.fetch_add(1u)) {
                Stage& stage = stages[at];
                std::vector<std::uint32_t> spirv(stage.size / 4u);
                std::memcpy(spirv.data(), in.data() + stage.offset, stage.size);
                galaxy::render::mtl::MslOptions options;
                options.ios = ios;
                options.bound_tables = stage.bound;
                stage.msl = galaxy::render::mtl::spirv_to_msl(
                    spirv, stage.pixel ? galaxy::render::mtl::MslStage::Fragment : galaxy::render::mtl::MslStage::Vertex,
                    options, stage.error);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    std::size_t failed = 0u;
    for (const Stage& stage : stages) {
        if (stage.msl.empty()) {
            if (failed++ < 20u) {
                std::fprintf(stderr, "FAILED %s stage at %u (%s): %s\n", stage.pixel ? "pixel" : "vertex", stage.offset,
                             stage.bound ? "bound tables" : "arrays", stage.error.c_str());
            }
        }
    }
    if (failed != 0u) {
        std::fprintf(stderr, "%zu of %zu stages failed\n", failed, stages.size());
        return 1;
    }

    std::vector<unsigned char> out(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(kHeader + rows * kRow));
    for (Stage& stage : stages) {
        out.resize((out.size() + 15u) & ~std::size_t{15u}, 0u);
        stage.placed = static_cast<std::uint32_t>(out.size());
        out.insert(out.end(), stage.msl.begin(), stage.msl.end());
        out.push_back(0u);
    }
    for (std::size_t row = 0; row < rows; ++row) {
        for (unsigned variant = 0; variant < kStored; ++variant) {
            const std::size_t at = kHeader + row * kRow + 16u + variant * 8u;
            const std::uint32_t offset = word(in, at), size = word(in, at + 4u);
            if (size == 0u) {
                continue;
            }
            const Stage& stage = stages[index.at(std::make_pair(offset, size))];
            const std::uint32_t length = static_cast<std::uint32_t>(stage.msl.size());
            std::memcpy(out.data() + at, &stage.placed, 4u);
            std::memcpy(out.data() + at + 4u, &length, 4u);
        }
    }
    const std::string temporary = std::string(argv[2]) + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!output) {
            std::fprintf(stderr, "%s cannot be written\n", temporary.c_str());
            return 1;
        }
    }
    if (std::rename(temporary.c_str(), argv[2]) != 0) {
        std::fprintf(stderr, "%s cannot be written\n", argv[2]);
        return 1;
    }
    std::printf("programs msl (%s): %zu stages, %zu bytes\n", ios ? "ios" : "macos", stages.size(), out.size());
    return 0;
}
