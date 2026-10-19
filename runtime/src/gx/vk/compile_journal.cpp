// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/vk/compile_journal.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace galaxy::gx::vk {

namespace {

constexpr std::uint64_t kMagic = 0x314c4e524a4b5658ull;  // "XVKJRNL1"

// Word 0 the magic, then each slot's kind (0: free) and key.
std::atomic<std::uint64_t>* words(void* view) {
    static_assert(sizeof(std::atomic<std::uint64_t>) == sizeof(std::uint64_t));
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    return static_cast<std::atomic<std::uint64_t>*>(view);
}

}  // namespace

std::vector<CompileJournal::Left> CompileJournal::open(const std::filesystem::path& path) {
    close();
#if defined(_WIN32)
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(kBytes), nullptr);
    CloseHandle(file);
    if (mapping == nullptr) {
        return {};
    }
    view_ = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kBytes);
    CloseHandle(mapping);  // the view keeps it
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        return {};
    }
    if (::ftruncate(fd, static_cast<off_t>(kBytes)) != 0) {
        ::close(fd);
        return {};
    }
    void* view = ::mmap(nullptr, kBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    view_ = view == MAP_FAILED ? nullptr : view;
#endif
    if (view_ == nullptr) {
        return {};
    }
    std::atomic<std::uint64_t>* w = words(view_);
    std::vector<Left> left;
    if (w[0].load(std::memory_order_relaxed) == kMagic) {
        for (std::size_t i = 0; i < kSlots; ++i) {
            const std::uint64_t kind = w[1 + 2 * i].load(std::memory_order_relaxed);
            if (kind != 0u) {
                left.push_back({kind, w[2 + 2 * i].load(std::memory_order_relaxed)});
            }
        }
    }
    for (std::size_t i = 0; i < 1 + 2 * kSlots; ++i) {
        w[i].store(0u, std::memory_order_relaxed);
    }
    w[0].store(kMagic, std::memory_order_release);
    return left;
}

void CompileJournal::close() noexcept {
    if (view_ == nullptr) {
        return;
    }
#if defined(_WIN32)
    UnmapViewOfFile(view_);
#else
    ::munmap(view_, kBytes);
#endif
    view_ = nullptr;
}

std::atomic<std::uint64_t>* CompileJournal::slot_for_this_thread() noexcept {
    if (view_ == nullptr) {
        return nullptr;
    }
    thread_local std::uint32_t slot = UINT32_MAX;
    if (slot == UINT32_MAX) {
        slot = next_slot_.fetch_add(1u, std::memory_order_relaxed) % kSlots;
    }
    return words(view_) + 1 + 2 * slot;
}

CompileJournal::Guard::Guard(CompileJournal& journal, std::uint64_t kind, std::uint64_t key) noexcept
    : slot_(journal.slot_for_this_thread()) {
    if (slot_ != nullptr) {
        slot_[1].store(key, std::memory_order_relaxed);
        slot_[0].store(kind, std::memory_order_release);
    }
}

CompileJournal::Guard::~Guard() {
    if (slot_ != nullptr) {
        slot_[0].store(0u, std::memory_order_release);
    }
}

}  // namespace galaxy::gx::vk
