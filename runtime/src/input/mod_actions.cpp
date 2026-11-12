// SPDX-License-Identifier: GPL-3.0-only
// Mods' input actions (mod_actions.h, contract services-input).
#include "galaxy/input/mod_actions.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace galaxy::input::mod_actions {
namespace {

constexpr std::string_view kPrefix = "modbind.";

struct Action {
    Declared declared;
    // What the sampler last saw, and what happened since the game thread last took it.
    bool down = false;
    float value = 0.0f;
    std::uint32_t presses = 0;
    std::uint32_t releases = 0;
    std::uint64_t frames = 0;  // samples (game frames) it has been down
    // The game thread's view (take).
    State seen;
};

std::mutex g_lock;  // the actions, the bindings and the samples
std::vector<Action> g_actions;
std::atomic<bool> g_any{false};  // g_actions is not empty
std::uint64_t g_sampled = 0;       // samples taken (0: none)
std::uint64_t g_taken = 0;         // g_sampled when the game thread's view was taken
// Bindings by "<scheme id>|<mod id>/<action id>", kept for actions not declared this session (a mod off or gone).
std::map<std::string, TargetBindings> g_bindings;

std::string binding_key(ControlScheme scheme, std::string_view mod_id, std::string_view id) {
    std::string key = control_scheme_id(scheme);
    key += '|';
    key += mod_id;
    key += '/';
    key += id;
    return key;
}

const TargetBindings* bindings_of(ControlScheme scheme, const Declared& action) {
    const auto it = g_bindings.find(binding_key(scheme, action.mod_id, action.id));
    if (it == g_bindings.end()) return nullptr;
    for (const InputSource& source : it->second) {
        if (!source.empty()) return &it->second;
    }
    return nullptr;
}

// An action id or a mod id as a settings key holds: the id rules (a-z 0-9 . _ -) keep '=' '/' '|' and spaces out.
bool plain(std::string_view text) {
    if (text.empty() || text.size() > 64u) return false;
    for (const char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

void declare(std::vector<Declared> actions, void (*log)(const char*)) {
    const std::lock_guard<std::mutex> lock(g_lock);
    g_actions.clear();
    std::map<std::string, std::size_t> per_mod;
    for (Declared& declared : actions) {
        std::size_t& count = per_mod[declared.mod_id];
        const bool twice = std::any_of(g_actions.begin(), g_actions.end(), [&](const Action& a) {
            return a.declared.mod_id == declared.mod_id && a.declared.id == declared.id;
        });
        if (twice || count >= kPerModMost || g_actions.size() >= kMost || !plain(declared.mod_id) || !plain(declared.id)) {
            if (log != nullptr) {
                const std::string line = "input: " + declared.mod_id + "'s action " + declared.id + " is left out (" +
                                         (twice ? std::string("a second key option of that key")
                                          : count >= kPerModMost ? "more than 64 for one mod"
                                          : g_actions.size() >= kMost ? "more than 1024 in all" : "not an id") + ")";
                log(line.c_str());
            }
            continue;
        }
        count++;
        Action action;
        action.declared = std::move(declared);
        g_actions.push_back(std::move(action));
    }
    g_any.store(!g_actions.empty(), std::memory_order_release);
}

bool any() noexcept {
    return g_any.load(std::memory_order_acquire);
}

std::vector<Listed> list(ControlScheme scheme) {
    const std::lock_guard<std::mutex> lock(g_lock);
    std::vector<Listed> out;
    out.reserve(g_actions.size());
    for (const Action& action : g_actions) {
        Listed listed{action.declared.mod_id, action.declared.id, action.declared.name, action.declared.follows, {}};
        if (const TargetBindings* bound = bindings_of(scheme, action.declared)) listed.bindings = *bound;
        out.push_back(std::move(listed));
    }
    return out;
}

bool bind(ControlScheme scheme, std::string_view mod_id, std::string_view id, const TargetBindings& bindings) {
    const std::lock_guard<std::mutex> lock(g_lock);
    const auto it = std::find_if(g_actions.begin(), g_actions.end(), [&](const Action& a) {
        return a.declared.mod_id == mod_id && a.declared.id == id;
    });
    if (it == g_actions.end()) return false;
    for (const InputSource& source : bindings) {
        if (!source.empty() && !binding_source_allowed(scheme, it->declared.follows, source)) return false;
    }
    g_bindings[binding_key(scheme, mod_id, id)] = bindings;
    return true;
}

bool apply_setting(std::string_view key, std::string_view value) {
    if (!key.starts_with(kPrefix)) return false;
    // modbind.<scheme>.<mod id>/<action id>
    const std::string_view rest = key.substr(kPrefix.size());
    const std::size_t dot = rest.find('.');
    const std::size_t slash = rest.find('/');
    if (dot == std::string_view::npos || slash == std::string_view::npos || slash < dot) return true;
    const std::optional<ControlScheme> scheme = parse_control_scheme(rest.substr(0u, dot));
    const std::string_view mod_id = rest.substr(dot + 1u, slash - dot - 1u);
    const std::string_view id = rest.substr(slash + 1u);
    if (!scheme || !plain(mod_id) || !plain(id)) return true;
    TargetBindings bindings{};
    // The game's own reader of a target's sources: an action's sources are a target's (any target allows the keys and
    // buttons; binding_source_allowed is checked again when the action is declared and bound).
    if (!parse_target_bindings(*scheme, value, bindings)) return true;
    const std::lock_guard<std::mutex> lock(g_lock);
    g_bindings[binding_key(*scheme, mod_id, id)] = bindings;
    return true;
}

std::string format_settings() {
    const std::lock_guard<std::mutex> lock(g_lock);
    std::string text;
    for (const auto& [key, bindings] : g_bindings) {
        const std::size_t bar = key.find('|');
        const std::optional<ControlScheme> scheme = parse_control_scheme(std::string_view(key).substr(0u, bar));
        if (!scheme) continue;
        const std::string sources = format_target_bindings(*scheme, bindings);
        text += std::string(kPrefix) + key.substr(0u, bar) + "." + key.substr(bar + 1u) + "=" + sources + "\n";
    }
    return text;
}

void sample(bool host_menu, bool keyboard, const KeyboardMouseLevels& levels,
            std::span<const PadLevels> pads, std::uint32_t game_held) {
    const std::lock_guard<std::mutex> lock(g_lock);
    for (Action& action : g_actions) {
        float value = 0.0f;
        bool any_bound = false;
        if (keyboard) {
            if (const TargetBindings* bound = bindings_of(ControlScheme::Keyboard, action.declared)) {
                any_bound = true;
                for (const InputSource& source : *bound) value = std::max(value, keyboard_source_value(source, levels));
            }
        }
        for (const PadLevels& pad : pads) {
            if (!pad.sample.connected) continue;
            if (const TargetBindings* bound = bindings_of(pad.scheme, action.declared)) {
                any_bound = true;
                for (const InputSource& source : *bound) value = std::max(value, pad_source_value(source, pad.sample));
            }
        }
        // A scheme with no binding of its own leaves the action to its game control (which every scheme reaches).
        if (!any_bound && (game_held & game_input_bit(action.declared.follows)) != 0u) value = 1.0f;
        if (host_menu) value = 0.0f;
        const bool down = value >= 0.5f;
        if (down && !action.down) action.presses++;
        if (!down && action.down) action.releases++;
        action.frames = down ? action.frames + 1u : 0u;
        action.down = down;
        action.value = value;
        action.seen.bound = any_bound;
    }
    g_sampled++;
}

std::int32_t find(std::string_view mod_id, std::string_view id) {
    const std::lock_guard<std::mutex> lock(g_lock);
    for (std::size_t i = 0; i < g_actions.size(); ++i) {
        if (g_actions[i].declared.mod_id == mod_id && g_actions[i].declared.id == id) return static_cast<std::int32_t>(i);
    }
    return -1;
}

std::string_view owner(std::int32_t index) {
    const std::lock_guard<std::mutex> lock(g_lock);
    // Declared once before the first mod starts and never changed after: the view stays valid.
    return index >= 0 && static_cast<std::size_t>(index) < g_actions.size() ? std::string_view(g_actions[index].declared.mod_id)
                                                                             : std::string_view();
}

namespace {

// g_lock held: the game thread's view from the newest sample.
void take() {
    g_taken = g_sampled;
    for (Action& action : g_actions) {
        State& seen = action.seen;
        seen.pressed = action.presses != 0u;
        seen.released = action.releases != 0u;
        seen.down = action.down;
        seen.value = action.value;
        seen.frames = action.frames;
        action.presses = 0u;
        action.releases = 0u;
    }
}

}  // namespace

State read(std::int32_t index) {
    const std::lock_guard<std::mutex> lock(g_lock);
    if (g_sampled != g_taken) take();
    return index >= 0 && static_cast<std::size_t>(index) < g_actions.size() ? g_actions[index].seen : State{};
}

}  // namespace galaxy::input::mod_actions
