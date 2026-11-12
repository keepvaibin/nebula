// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's input service (contract services-input, luma_input.h): a mod's own actions, its luma.json `key` options,
// read each game frame as the controls sampled them (input/mod_actions.h).

#include "galaxy/luma_input_service.h"

#include "galaxy/input/mod_actions.h"
#include "galaxy/input_bindings.h"
#include "galaxy/mods.h"

#include <luma_input.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::luma_input {
namespace {

constexpr std::uint32_t kStateLeast = static_cast<std::uint32_t>(sizeof(LumaInputState));  // 1.0's whole struct
static_assert(sizeof(LumaInputState) == 24u, "LumaInputState 1.0 is 24 bytes (luma_input.h)");

std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
void (*g_log)(const char*) = nullptr;  // set by providers() before the first mod
bool g_declared = false;              // game thread: the session's actions are declared (at the first mod's begin)

// ---- Calling rules (services-registry 4) ---------------------------------------------------------------------------

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr, const char* b = nullptr,
            const char* c = nullptr) noexcept {
    const char* args[3] = {a, b, c};
    std::uint32_t count = 0;
    while (count < 3u && args[count] != nullptr) {
        count++;
    }
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, count);
    }
}

// The mod known and live, on the game thread. LUMA_OK or the status to return.
std::int32_t enter(const LumaMod* mod, const char* where) noexcept {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr) {
        return LUMA_E_CANCELLED;
    }
    if (mod == nullptr || kernel->mod_id(mod) == nullptr) {
        kernel->unknown_mod(where);
        return LUMA_E_ARGUMENT;
    }
    if (kernel->mod_live(mod) == 0) {
        return LUMA_E_CANCELLED;
    }
    if (kernel->game_thread() == 0) {
        report(mod, where, 100u);  // Service.Thread
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

std::int32_t argument(const LumaMod* mod, const char* where, const char* name) noexcept {
    report(mod, where, 101u, name);  // Service.Argument
    return LUMA_E_ARGUMENT;
}

// ---- The actions ----------------------------------------------------------------------------------------------------

// Game thread, once: every action of the mods this session started with (their key options), in the Mods list's order.
// A mod that is off has none: its bindings stay in the settings, unread.
void declare() {
    if (g_declared) {
        return;
    }
    g_declared = true;
    std::vector<galaxy::input::mod_actions::Declared> actions;
    for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
        if (!mod.enabled) {
            continue;
        }
        for (const galaxy::mods::ModInfo::Action& action : mod.actions) {
            const std::optional<galaxy::input::GameInput> follows = galaxy::input::parse_game_input(action.follows);
            if (!follows) {
                // The validator refuses a key option whose default is no game control; a mod past it is said here.
                if (g_log != nullptr) {
                    const std::string line = "input: " + mod.id + "'s action " + action.id + " is left out (its default " +
                                             action.follows + " is no game control)";
                    g_log(line.c_str());
                }
                continue;
            }
            actions.push_back({mod.id, action.id, action.name, *follows});
        }
    }
    galaxy::input::mod_actions::declare(std::move(actions), g_log);
}

// The action `handle` names when it is one of `mod`'s: its index, or -1.
std::int32_t index_of(const LumaMod* mod, LumaInputAction handle) {
    if (handle == 0u || handle > 0x7FFFFFFFu) {
        return -1;
    }
    const std::int32_t index = static_cast<std::int32_t>(handle - 1u);
    const char* const id = g_kernel.load(std::memory_order_acquire)->mod_id(mod);
    return id != nullptr && galaxy::input::mod_actions::owner(index) == std::string_view(id) ? index : -1;
}

// ---- input (items 2, 3) ---------------------------------------------------------------------------------------------

std::int32_t input_action(const LumaMod* mod, const char* id, LumaInputAction* out) {
    const char* const where = "input.action";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (id == nullptr) {
        return argument(mod, where, "id");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    *out = 0u;
    const char* const mod_id = g_kernel.load(std::memory_order_acquire)->mod_id(mod);
    const std::int32_t index = galaxy::input::mod_actions::find(mod_id, id);
    if (index < 0) {
        return LUMA_E_NOT_FOUND;  // an answer: the mod's luma.json has no key option `id`
    }
    *out = static_cast<LumaInputAction>(index) + 1u;
    return LUMA_OK;
}

std::int32_t input_read(const LumaMod* mod, LumaInputAction action, LumaInputState* out) {
    const char* const where = "input.read";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (out->size < kStateLeast) {
        char found_text[16];
        char least_text[16];
        std::snprintf(found_text, sizeof(found_text), "%u", out->size);
        std::snprintf(least_text, sizeof(least_text), "%u", kStateLeast);
        report(mod, where, 103u, "LumaInputState", found_text, least_text);  // Service.StructSize
        return LUMA_E_ARGUMENT;
    }
    const std::int32_t index = index_of(mod, action);
    if (index < 0) {
        report(mod, where, 102u);  // Service.Handle
        return LUMA_E_HANDLE;
    }
    const galaxy::input::mod_actions::State state = galaxy::input::mod_actions::read(index);
    const std::uint32_t size = out->size;
    std::memset(out, 0, kStateLeast);
    out->size = size;
    out->down = state.down ? 1u : 0u;
    out->pressed = state.pressed ? 1u : 0u;
    out->released = state.released ? 1u : 0u;
    out->bound = state.bound ? 1u : 0u;
    out->value = state.value;
    out->frames = state.frames;
    return LUMA_OK;
}

// ---- The provider ---------------------------------------------------------------------------------------------------

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

// Game thread, before each mod's begin: the first declares the session's actions (the start has recorded its mods).
void mod_begin(void*, const LumaMod*) {
    declare();
}

const LumaInputService kInput = {
    {sizeof(LumaInputService), LUMA_INPUT_MAJOR, LUMA_INPUT_MINOR},
    input_action,
    input_read,
};

// No per-mod state: an action is the session's, read by index; a handle of another mod's is refused by its owner.
const LumaServiceProvider kInputProvider = {
    sizeof(LumaServiceProvider), LUMA_INPUT_NAME, &kInput.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, nullptr, nullptr, registered,
};
const LumaServiceProvider* const kProviders[] = {&kInputProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept {
    g_log = log;
    return kProviders;
}

}  // namespace galaxy::luma_input
