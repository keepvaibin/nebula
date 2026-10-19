// SPDX-License-Identifier: GPL-3.0-only
// The game's object names as the native game reports them (gx_d3d12.h note_object_name): NameObj::mName by the id
// its host markers carry. Simulation thread writes, render thread reads.
#include "galaxy/gx_d3d12.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace galaxy::gx {
namespace {

std::mutex g_object_names_mutex;
std::unordered_map<std::uint32_t, std::string> g_object_names;

}  // namespace

void note_object_name(std::uint32_t id, const char* name) noexcept {
    if (id == 0u || name == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_object_names_mutex);
    try {
        g_object_names[id] = name;
    } catch (...) {
    }
}

bool game_object_name(std::uint32_t id, char* out, std::size_t size) noexcept {
    if (size == 0u) {
        return false;
    }
    out[0] = 0;
    const std::lock_guard<std::mutex> lock(g_object_names_mutex);
    const auto it = g_object_names.find(id);
    if (it == g_object_names.end()) {
        return false;
    }
    const std::size_t n = std::min(it->second.size(), size - 1u);
    std::memcpy(out, it->second.data(), n);
    out[n] = 0;
    return true;
}

}  // namespace galaxy::gx
