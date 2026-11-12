// SPDX-License-Identifier: GPL-3.0-only
// The Mods screens a mod's request opens (galaxy/mod_screens.h): the calls the services make, passed to the Mods page's
// hooks, and the answers' queue, run on the game thread at a frame's start.

#include "galaxy/mod_screens.h"

#include <luma_services.h>

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace galaxy::mods {
namespace {

std::atomic<const ScreenHooks*> g_hooks{nullptr};
std::atomic<void (*)()> g_pause{nullptr};
std::mutex g_answers_mutex;
std::vector<std::function<void()>> g_answers;  // under g_answers_mutex

}  // namespace

void set_screen_hooks(const ScreenHooks* hooks) {
    g_hooks.store(hooks, std::memory_order_release);
}

bool player_screen_up() {
    const ScreenHooks* hooks = g_hooks.load(std::memory_order_acquire);
    return hooks != nullptr && hooks->screen_up();
}

std::int32_t ask_install(const std::filesystem::path& file, std::string_view asker,
                         std::function<void(std::int32_t status)> done) {
    const ScreenHooks* hooks = g_hooks.load(std::memory_order_acquire);
    return hooks != nullptr ? hooks->ask_install(file, asker, std::move(done)) : LUMA_E_ABSENT;
}

bool install_ask_allowed(std::string_view id) {
    const ScreenHooks* hooks = g_hooks.load(std::memory_order_acquire);
    return hooks == nullptr || hooks->install_ask_allowed(id);
}

std::int32_t ask_offer(const OfferFacts& facts, std::string_view asker,
                       std::function<void(std::int32_t status, std::uint32_t file)> done) {
    const ScreenHooks* hooks = g_hooks.load(std::memory_order_acquire);
    return hooks != nullptr ? hooks->ask_offer(facts, asker, std::move(done)) : LUMA_E_ABSENT;
}

void close_asks(std::string_view asker) {
    if (const ScreenHooks* hooks = g_hooks.load(std::memory_order_acquire)) hooks->close_asks(asker);
}

void post_screen_answer(std::function<void()> answer) {
    const std::lock_guard<std::mutex> lock(g_answers_mutex);
    g_answers.push_back(std::move(answer));
}

void run_screen_answers() {
    std::vector<std::function<void()>> answers;
    {
        const std::lock_guard<std::mutex> lock(g_answers_mutex);
        if (g_answers.empty()) return;
        answers.swap(g_answers);
    }
    for (std::function<void()>& answer : answers) {
        if (answer) answer();
    }
}

void set_screen_pause(void (*pause)()) {
    g_pause.store(pause, std::memory_order_release);
}

void pause_for_screen() {
    if (void (*pause)() = g_pause.load(std::memory_order_acquire)) pause();
}

}  // namespace galaxy::mods
