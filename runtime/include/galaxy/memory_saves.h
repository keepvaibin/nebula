// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Save files held in memory, for a run that plays the game on blank saves: the benchmark (benchmark.h) and the HDR
// calibration's scene (calibration_scene.h) answer the game's save requests (svc_save_request) from here, and the
// player's files are never opened.

#include "nebula_game_api.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace galaxy {

class MemorySaves {
public:
    // `op` is NEBULA_SAVE_READ, _WRITE or _DELETE; the result a NEBULA_SAVE_* value (galaxy::saves::Result). A read
    // gives the file's length in `length` even when `buffer` is too small for it.
    [[nodiscard]] std::int32_t request(int op, std::string_view name, void* buffer, std::uint32_t size,
                                       std::uint32_t* length) noexcept {
        try {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto at = files_.find(name);
            switch (op) {
            case NEBULA_SAVE_READ:
                if (at == files_.end()) return NEBULA_SAVE_NO_FILE;
                if (length != nullptr) *length = static_cast<std::uint32_t>(at->second.size());
                if (at->second.size() > size) return NEBULA_SAVE_TOO_LARGE;
                std::memcpy(buffer, at->second.data(), at->second.size());
                return NEBULA_SAVE_DONE;
            case NEBULA_SAVE_WRITE:
                files_[std::string(name)].assign(static_cast<const char*>(buffer), size);
                return NEBULA_SAVE_DONE;
            case NEBULA_SAVE_DELETE:
                if (at == files_.end()) return NEBULA_SAVE_NO_FILE;
                files_.erase(at);
                return NEBULA_SAVE_DONE;
            default:
                return NEBULA_SAVE_FAILED;
            }
        } catch (...) {
            return NEBULA_SAVE_FAILED;
        }
    }

    // Every file gone: the next run starts blank.
    void clear() noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        files_.clear();
    }

private:
    std::mutex mutex_;
    std::map<std::string, std::string, std::less<>> files_;
};

}  // namespace galaxy
