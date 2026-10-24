// SPDX-License-Identifier: GPL-3.0-only
// The pipeline compiles in progress, in a small memory-mapped file (vk_compiling.bin). When a driver's compiler
// crashes the process inside vkCreateGraphicsPipelines (reported for Adreno 740), the pipeline it was building stays
// in the file and the next launch never builds it again (PipelineCacheVk). Marking a compile is two stores to memory,
// no system call.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace galaxy::gx::vk {

class CompileJournal {
public:
    struct Left {
        std::uint64_t kind = 0;
        std::uint64_t key = 0;
    };

    CompileJournal() = default;
    CompileJournal(const CompileJournal&) = delete;
    CompileJournal& operator=(const CompileJournal&) = delete;
    ~CompileJournal() { close(); }

    // Maps the file, returning the compiles a crashed process left in it, and clears them.
    std::vector<Left> open(const std::filesystem::path& path);
    void close() noexcept;

    // Marks a compile on this thread for the guard's lifetime (nothing when the journal is not open).
    class Guard {
    public:
        Guard(CompileJournal& journal, std::uint64_t kind, std::uint64_t key) noexcept;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        ~Guard();

    private:
        std::atomic<std::uint64_t>* slot_ = nullptr;
    };

private:
    static constexpr std::size_t kSlots = 64;
    static constexpr std::size_t kBytes = 4096;
    std::atomic<std::uint64_t>* slot_for_this_thread() noexcept;

    void* view_ = nullptr;
    std::atomic<std::uint32_t> next_slot_{0};
};

}  // namespace galaxy::gx::vk
