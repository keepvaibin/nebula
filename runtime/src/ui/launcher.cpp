// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/launcher.h"

#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
#include "galaxy/ui/launcher_model.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

namespace galaxy::ui {
namespace {

// The runtime's key numbers (Windows virtual keys on every platform, virtual_keys.h).
constexpr std::uintptr_t kKeyBack = 0x08u;
constexpr std::uintptr_t kKeyReturn = 0x0Du;
constexpr std::uintptr_t kKeyEscape = 0x1Bu;
constexpr std::uintptr_t kKeySpace = 0x20u;
constexpr std::uintptr_t kKeyLeft = 0x25u;
constexpr std::uintptr_t kKeyUp = 0x26u;
constexpr std::uintptr_t kKeyRight = 0x27u;
constexpr std::uintptr_t kKeyDown = 0x28u;

// The notice's page fits on the screen with its notes' head: the full notes are on the release's page.
constexpr std::size_t kNotesUnits = 400u;
constexpr std::size_t kNotesLines = 8u;
constexpr std::size_t kVersionUnits = 64u;

LauncherModel s_model;
std::atomic<bool> s_enabled{false};
std::atomic<bool> s_opened{false};
std::mutex s_text_mutex;
std::u16string s_version;
std::u16string s_notes;
std::optional<galaxy::render::RenderConfig> s_draft;  // under s_text_mutex
std::atomic<void (*)(int)> s_upload{nullptr};
constexpr unsigned kGraphicsPage = 1u;                // native_settings.cpp kNativeSettingsTabNames

// UTF-8 as UTF-16, at most `limit` units and `lines` lines, without control characters but line breaks; a malformed
// sequence becomes U+FFFD.
std::u16string utf16_of(std::string_view text, std::size_t limit, std::size_t lines) {
    std::u16string out;
    std::size_t breaks = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80u ? 1u : (lead >> 5) == 0x6u ? 2u : (lead >> 4) == 0xEu ? 3u : (lead >> 3) == 0x1Eu ? 4u : 0u;
        char32_t code = length == 1u ? lead : length == 2u ? (lead & 0x1Fu) : length == 3u ? (lead & 0x0Fu) : (lead & 0x07u);
        bool valid = length != 0u && i + length <= text.size();
        for (std::size_t k = 1; valid && k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            valid = (next & 0xC0u) == 0x80u;
            code = (code << 6) | (next & 0x3Fu);
        }
        constexpr char32_t kLeast[] = {0, 0, 0x80, 0x800, 0x10000};
        valid = valid && code >= kLeast[length] && code <= 0x10FFFFu && (code < 0xD800u || code > 0xDFFFu);
        i += valid ? length : 1u;
        if (!valid) {
            code = 0xFFFDu;
        } else if (code < 0x20u && code != u'\n') {
            continue;  // control characters: the game's text writer reads U+001A as a tag's start
        }
        const std::size_t units = code > 0xFFFFu ? 2u : 1u;
        if (out.size() + units > limit || (code == u'\n' && ++breaks >= lines)) {
            break;
        }
        if (units == 2u) {
            out.push_back(static_cast<char16_t>(0xD800u + ((code - 0x10000u) >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00u + ((code - 0x10000u) & 0x3FFu)));
        } else {
            out.push_back(static_cast<char16_t>(code));
        }
    }
    return out;
}

// The recommendation's draft, once: when the screen opens, or after the upload question when one shows.
void open_draft() {
    std::optional<galaxy::render::RenderConfig> draft;
    {
        const std::lock_guard<std::mutex> lock(s_text_mutex);
        draft.swap(s_draft);
    }
    if (draft) {
        galaxy::render::host_window::open_native_settings_draft(kGraphicsPage, *draft);
    }
}

void apply(LauncherModel::Effect effect) {
    switch (effect) {
    case LauncherModel::Effect::OpenSettings: galaxy::render::host_window::open_native_settings(0u); break;
    case LauncherModel::Effect::ShowReport:
    case LauncherModel::Effect::SendReport:
        if (void (*const act)(int) = s_upload.load(std::memory_order_acquire)) {
            act(effect == LauncherModel::Effect::SendReport ? 1 : 0);
        }
        break;
    case LauncherModel::Effect::UploadLeft: open_draft(); break;
    case LauncherModel::Effect::None: break;
    }
}

}  // namespace

void launcher_enable(bool on) noexcept {
    s_enabled.store(on, std::memory_order_relaxed);
}

void launcher_offer_benchmark(bool on) noexcept {
    const std::lock_guard<std::mutex> lock(s_model.mutex);
    s_model.benchmark = on;
}

void launcher_offer_message() noexcept {
    s_model.offer_message();
}

void launcher_offer_upload(void (*act)(int what)) noexcept {
    s_upload.store(act, std::memory_order_release);
    s_model.offer_upload();
}

void launcher_upload_finished(bool sent) noexcept {
    s_model.upload_finished(sent);
}

void launcher_show_draft(const galaxy::render::RenderConfig& draft) {
    const std::lock_guard<std::mutex> lock(s_text_mutex);
    s_draft = draft;
}

bool launcher_open() noexcept {
    if (!s_enabled.load(std::memory_order_relaxed) || s_opened.exchange(true, std::memory_order_relaxed)) {
        return false;
    }
    s_model.open();
    bool asking = false;
    {
        const std::lock_guard<std::mutex> lock(s_model.mutex);
        asking = s_model.upload_page != 0u || s_model.message_page;
    }
    if (!asking) {
        open_draft();  // else after the pages before it (apply: UploadLeft)
    }
    return true;
}

bool launcher_visible() noexcept {
    return s_model.visible.load(std::memory_order_acquire);
}

int launcher_view() noexcept {
    return s_model.view();
}

int launcher_poll() noexcept {
    return s_model.poll();
}

void launcher_point(unsigned answer) noexcept {
    s_model.point(answer);
}

void launcher_choose(unsigned answer) noexcept {
    apply(s_model.click(answer));
}

bool launcher_key(std::uintptr_t key, bool was_down, bool accept) noexcept {
    if (!s_model.visible.load(std::memory_order_acquire)) {
        return false;
    }
    LauncherAction action = LauncherAction::Ignore;
    if (accept) {
        action = LauncherAction::Accept;
    } else {
        switch (key) {
        case kKeyUp:
        case kKeyLeft: action = LauncherAction::Previous; break;
        case kKeyDown:
        case kKeyRight: action = LauncherAction::Next; break;
        case kKeyReturn:
        case kKeySpace: action = LauncherAction::Accept; break;
        case kKeyEscape:
        case kKeyBack: action = LauncherAction::Back; break;
        default: break;
        }
    }
    apply(s_model.key(action, was_down));
    return true;
}

void launcher_offer_update(std::string_view version, std::string_view notes) {
    {
        const std::lock_guard<std::mutex> lock(s_text_mutex);
        s_version = utf16_of(version, kVersionUnits, 1u);
        s_notes = utf16_of(notes, kNotesUnits, kNotesLines);
    }
    s_model.offer_notice();
}

unsigned launcher_text(unsigned which, unsigned index) noexcept {
    const std::lock_guard<std::mutex> lock(s_text_mutex);
    const std::u16string& text = which == 0u ? s_version : s_notes;
    return index < text.size() ? text[index] : 0u;
}

}  // namespace galaxy::ui
