// SPDX-License-Identifier: GPL-3.0-only
// Compiles every stage of a Metal programs' file (programs_msl) with each of this machine's GPUs, as the renderer will:
// the build's check that Metal's compiler takes what SPIRV-Cross wrote.
//
//   msl_check <programs_msl.bin> [<first failures to print>]
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 2) {
            std::fprintf(stderr, "usage: msl_check <programs_msl.bin> [<failures to print>]\n");
            return 2;
        }
        const unsigned shown = argc > 2 ? static_cast<unsigned>(std::atoi(argv[2])) : 5u;
        std::ifstream input(argv[1], std::ios::binary);
        const std::vector<char> in((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (in.size() < 64u || std::memcmp(in.data(), "NBPROG02", 8u) != 0) {
            std::fprintf(stderr, "%s is not a programs file\n", argv[1]);
            return 1;
        }
        std::uint32_t vertex_rows = 0u, pixel_rows = 0u;
        std::memcpy(&vertex_rows, in.data() + 8, 4u);
        std::memcpy(&pixel_rows, in.data() + 12, 4u);
        struct Stage {
            std::uint32_t offset;
            bool pixel, arrays;
        };
        std::vector<Stage> stages;
        std::set<std::uint32_t> seen;
        for (std::size_t row = 0; row < std::size_t{vertex_rows} + pixel_rows; ++row) {
            for (unsigned variant = 0; variant < 10u; ++variant) {
                std::uint32_t offset = 0u, size = 0u;
                std::memcpy(&offset, in.data() + 64u + row * 96u + 16u + variant * 8u, 4u);
                std::memcpy(&size, in.data() + 64u + row * 96u + 20u + variant * 8u, 4u);
                if (size != 0u && seen.insert(offset).second) {
                    stages.push_back({offset, row >= vertex_rows, row >= vertex_rows && variant < 5u});
                }
            }
        }
        int result = 0;
        for (id<MTLDevice> device in MTLCopyAllDevices()) {
            const bool tier2 = device.argumentBuffersSupport == MTLArgumentBuffersTier2;
            std::atomic<std::size_t> next{0u};
            std::atomic<unsigned> failed{0u}, compiled{0u}, skipped{0u};
            std::mutex print;
            std::vector<std::thread> threads;
            const auto started = [NSDate date];
            for (unsigned worker = 0; worker < std::max(1u, std::thread::hardware_concurrency()); ++worker) {
                threads.emplace_back([&] {
                    for (std::size_t at = next.fetch_add(1u); at < stages.size(); at = next.fetch_add(1u)) {
                        @autoreleasepool {
                            const Stage& stage = stages[at];
                            if (stage.arrays && !tier2) {
                                skipped.fetch_add(1u);
                                continue;
                            }
                            NSString* const source = [NSString stringWithUTF8String:in.data() + stage.offset];
                            MTLCompileOptions* const options = [MTLCompileOptions new];
                            options.languageVersion = stage.arrays ? MTLLanguageVersion3_0 : MTLLanguageVersion2_4;
                            NSError* error = nil;
                            id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
                            id<MTLFunction> function = library != nil ? [library newFunctionWithName:@"main0"] : nil;
                            if (function == nil) {
                                const unsigned count = failed.fetch_add(1u);
                                if (count < shown) {
                                    const std::lock_guard<std::mutex> lock(print);
                                    std::fprintf(stderr, "FAILED %s stage at %u (%s): %s\n", stage.pixel ? "pixel" : "vertex",
                                                 stage.offset, stage.arrays ? "arrays" : "plain",
                                                 error.localizedDescription.UTF8String ?: "no function main0");
                                }
                            } else {
                                compiled.fetch_add(1u);
                            }
                        }
                    }
                });
            }
            for (std::thread& thread : threads) {
                thread.join();
            }
            std::printf("%s: %u compiled, %u failed, %u skipped (arrays need argument buffers tier 2), %.1f s\n",
                        device.name.UTF8String, compiled.load(), failed.load(), skipped.load(), -[started timeIntervalSinceNow]);
            if (failed.load() != 0u) {
                result = 1;
            }
        }
        return result;
    }
}
