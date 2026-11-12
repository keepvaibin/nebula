// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/luma_services_host.h"

#include "galaxy/host_extension.h"
#include "galaxy/luma_assets_service.h"
#include "galaxy/luma_audio_service.h"
#include "galaxy/luma_capture_service.h"
#include "galaxy/luma_fs_service.h"
#include "galaxy/luma_net_service.h"
#include "galaxy/luma_input_service.h"
#include "galaxy/luma_mods_service.h"
#include "galaxy/luma_system_platform.h"
#include "galaxy/mod_screens.h"

#include <luma_services_facts.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace galaxy::luma_services {
namespace {

// Written on the game thread by provide_all, then read from any thread by a provider (acquire).
std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
bool g_provided = false;  // game thread

struct Giving {
    LumaProvideService provide;
};

// The system's wall clock for the module's time service (services-time 11). std::chrono::system_clock counts from
// 1970-01-01 UTC on every platform since C++20.
int64_t utc_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// save.game's offered file (services-save 8): found as mods.install finds its file (no link followed), read whole.
int32_t data_file(const char* id, const char* path, void* buf, uint32_t size, uint32_t* length) noexcept {
    if (id == nullptr || path == nullptr || length == nullptr || (buf == nullptr && size != 0u)) {
        return LUMA_E_ARGUMENT;
    }
    try {
        std::filesystem::path file;
        if (const std::int32_t found = galaxy::luma_fs::find_data_file(id, path, &file); found != LUMA_OK) {
            return found;
        }
        std::error_code error;
        const std::uintmax_t bytes = std::filesystem::file_size(file, error);
        if (error || bytes > UINT32_MAX) {
            return LUMA_E_IO;
        }
        *length = static_cast<uint32_t>(bytes);
        const uint32_t want = size < *length ? size : *length;
        if (want == 0u) {
            return LUMA_OK;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in.read(static_cast<char*>(buf), static_cast<std::streamsize>(want))) {
            return LUMA_E_IO;
        }
    } catch (...) {
        return LUMA_E_IO;
    }
    return LUMA_OK;
}

galaxy::mods::OfferFile offer_file(const LumaOfferFile& file) noexcept {
    galaxy::mods::OfferFile out;
    out.exists = file.exists != 0u;
    out.in_play = file.in_play != 0u;
    out.luigi = file.luigi != 0u;
    out.stars = file.stars;
    out.saved = file.saved;
    return out;
}

// J's offer screen (galaxy/mod_screens.h); its answer reaches the module through `done` at a frame's start.
int32_t ask_offer(const LumaOfferFacts* facts, const char* asker, LumaOfferDone done, void* context) noexcept {
    if (facts == nullptr || asker == nullptr || done == nullptr) {
        return LUMA_E_ARGUMENT;
    }
    try {
        galaxy::mods::OfferFacts given;
        given.offered = offer_file(facts->offered);
        for (std::size_t i = 0; i < given.files.size(); ++i) {
            given.files[i] = offer_file(facts->files[i]);
        }
        return galaxy::mods::ask_offer(given, asker, [done, context](std::int32_t status, std::uint32_t file) {
            done(context, status, file);
        });
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

void close_offer(const char* asker) noexcept {
    if (asker == nullptr) {
        return;
    }
    try {
        galaxy::mods::close_asks(asker);
    } catch (...) {
    }
}

const LumaServicesFacts kFacts{sizeof(LumaServicesFacts), utc_ns, data_file, ask_offer, close_offer};

}  // namespace

bool provide_all(void* (*find_symbol)(const char*), void (*log)(const char*),
                 std::span<const LumaServiceProvider* const> own) noexcept {
    if (g_provided) {
        return true;
    }
    const auto provide = reinterpret_cast<LumaProvideService>(find_symbol != nullptr ? find_symbol("luma_provide_service") : nullptr);
    const auto open = reinterpret_cast<LumaServicesOpen>(find_symbol != nullptr ? find_symbol("luma_services_open") : nullptr);
    if (provide == nullptr || open == nullptr) {
        if (log != nullptr) {
            log("LumaCore services: the game module has no luma_services_open or luma_provide_service; mods get no service");
        }
        return false;
    }
    g_provided = true;
    // The module's own services first (the catalog checked), then the host's, then the extensions' test services.
    g_kernel.store(open(), std::memory_order_release);
    if (const auto give = reinterpret_cast<LumaServicesGiveFacts>(find_symbol("luma_services_facts")); give != nullptr) {
        give(&kFacts);
    } else if (log != nullptr) {
        log("LumaCore services: the game module has no luma_services_facts; time's wall clock is the local calendar");
    }
    // A bad provider stops the game in the module (luma_service_provider.h), naming it; 0 is the only other answer.
    for (const LumaServiceProvider* const provider : own) {
        provide(provider);
    }
    // The system services (services-system 10), after the host's own.
    const auto monotonic = reinterpret_cast<LumaServicesMonotonic>(find_symbol("luma_services_monotonic_ns"));
    if (monotonic == nullptr && log != nullptr) {
        log("LumaCore services: the game module has no luma_services_monotonic_ns; a link's spacing uses the system's clock");
    }
    for (const LumaServiceProvider* const provider : galaxy::luma_system::providers(log, monotonic)) {
        provide(provider);
    }
    // The mods services (services-mods 4), and the module's half of the REMOVED MODS line (services-save 5).
    for (const LumaServiceProvider* const provider : galaxy::luma_mods::providers(log)) {
        provide(provider);
    }
    const auto kept = reinterpret_cast<LumaSaveKept>(find_symbol("luma_save_kept"));
    const auto forget = reinterpret_cast<LumaSaveForget>(find_symbol("luma_save_forget"));
    if ((kept == nullptr || forget == nullptr) && log != nullptr) {
        log("LumaCore services: the game module has no luma_save_kept or luma_save_forget; REMOVED MODS counts no file values");
    }
    galaxy::luma_mods::set_chunk_half(kept, forget);
    // The file services (services-fs 10).
    for (const LumaServiceProvider* const provider : galaxy::luma_fs::providers(log)) {
        provide(provider);
    }
    // The net services (services-net; layer 1: net.local).
    for (const LumaServiceProvider* const provider : galaxy::luma_net::providers(log)) {
        provide(provider);
    }
    // The assets service (services-assets; layer 1: the game's files by path).
    for (const LumaServiceProvider* const provider : galaxy::luma_assets::providers(log)) {
        provide(provider);
    }
    // The input service (services-input): every host has it; a host that samples no controls leaves every action up.
    for (const LumaServiceProvider* const provider : galaxy::luma_input::providers(log)) {
        provide(provider);
    }
    // The audio service (services-audio): a mod's cooked sounds, its streams, music, taps; the game's own sounds and
    // the camera's place from the module.
    galaxy::luma_audio::set_game(find_symbol);
    for (const LumaServiceProvider* const provider : galaxy::luma_audio::providers(log)) {
        provide(provider);
    }
    // The capture services (services-audio 7; layers 5-6: the microphone and the camera, each its permission).
    for (const LumaServiceProvider* const provider : galaxy::luma_capture::providers(log)) {
        provide(provider);
    }
    Giving giving{provide};
    galaxy::extensions::luma_providers(
        [](const LumaServiceProvider* provider, void* user) { static_cast<Giving*>(user)->provide(provider); }, &giving);
    return true;
}

const LumaServiceKernel* kernel() noexcept {
    return g_kernel.load(std::memory_order_acquire);
}

}  // namespace galaxy::luma_services
