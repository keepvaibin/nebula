// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's system services (docs/modding/services/system.md): `system` (facts and the display), `system.clipboard`
// and `system.links`. The tables, the checks, the requests, the mod log's Notes, the deterministic clipboard and the
// codecs; the machine's clipboard and browser are the platform layer's (luma_system_platform.h).

#include "galaxy/luma_system_platform.h"

#include "galaxy/host_extension.h"
#include "galaxy/mod_log.h"
#include "galaxy/mods.h"

#include <luma_modlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

// The codecs, private to this file (section 5): no symbol meets the render layer's copy of stb.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "../../third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../../third_party/stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace galaxy::luma_system {
namespace {

constexpr std::uint32_t kClipboardMost = 4u;              // open clipboard requests a mod (section 1 item 10)
constexpr std::uint64_t kTextMost = 16ull << 20u;          // bytes of text either way (item 5)
constexpr std::uint32_t kSideMost = 8192u;                 // pixels a side (item 4)
constexpr std::uint64_t kPixelsMost = 64ull << 20u;        // bytes of RGBA
constexpr std::size_t kUrlMost = 2048u;                    // bytes of a link (item 7)
constexpr std::uint64_t kLinkSpacingNs = 10'000'000'000ull; // one link a mod in 10 seconds
constexpr std::uint32_t kFactsLeast = static_cast<std::uint32_t>(sizeof(LumaSystemFacts));    // 1.0's whole struct
constexpr std::uint32_t kDisplayLeast = static_cast<std::uint32_t>(sizeof(LumaSystemDisplay)); // 1.0's whole struct
static_assert(sizeof(LumaSystemFacts) == 320u, "LumaSystemFacts 1.0 is 320 bytes (system.md section 2)");
static_assert(sizeof(LumaSystemDisplay) == 56u, "LumaSystemDisplay 1.0 is 56 bytes (system.md section 2)");

const char* const kClipboardName = LUMA_SYSTEM_CLIPBOARD_NAME;
const char* const kLinksName = LUMA_SYSTEM_LINKS_NAME;

// ---- What the host gives -------------------------------------------------------------------------------------------

std::atomic<const Platform*> g_platform{nullptr};
std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
// Set by providers() on the game thread before the first mod loads, so before any request reaches a platform thread.
void (*g_log)(const char*) = nullptr;
std::uint64_t (*g_monotonic)() = nullptr;

std::mutex g_renderer_lock;
bool g_renderer_known = false;
std::uint32_t g_renderer = 0;
std::array<char, sizeof(LumaSystemFacts::gpu)> g_gpu{};

bool deterministic() noexcept {
    return (galaxy::extensions::flags() & NEBULA_EXT_FIXED_STEPS) != 0u;
}

std::uint64_t monotonic_ns() noexcept {
    if (g_monotonic != nullptr) {
        return g_monotonic();
    }
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// `text` into `out` (`size` bytes with its NUL), cut at a character's end.
void copy_text(char* out, std::size_t size, std::string_view text) noexcept {
    std::size_t length = std::min(text.size(), size - 1u);
    if (length < text.size()) {
        while (length > 0u && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
            length--;
        }
    }
    std::memcpy(out, text.data(), length);
    std::memset(out + length, 0, size - length);
}

// UTF-8 as RFC 3629 writes it (no overlong form, no surrogate, nothing above U+10FFFF) and no NUL.
bool valid_text(const char* text, std::uint64_t bytes) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    std::uint64_t i = 0;
    while (i < bytes) {
        const unsigned char c = p[i];
        if (c == 0u) {
            return false;
        }
        if (c < 0x80u) {
            i++;
            continue;
        }
        std::uint32_t more = 0;
        std::uint32_t least = 0;
        std::uint32_t code = 0;
        if (c >= 0xC2u && c <= 0xDFu) {
            more = 1;
            least = 0x80u;
            code = c & 0x1Fu;
        } else if (c >= 0xE0u && c <= 0xEFu) {
            more = 2;
            least = 0x800u;
            code = c & 0x0Fu;
        } else if (c >= 0xF0u && c <= 0xF4u) {
            more = 3;
            least = 0x10000u;
            code = c & 0x07u;
        } else {
            return false;
        }
        if (bytes - i <= more) {
            return false;
        }
        for (std::uint32_t k = 1; k <= more; k++) {
            const unsigned char next = p[i + k];
            if ((next & 0xC0u) != 0x80u) {
                return false;
            }
            code = (code << 6u) | (next & 0x3Fu);
        }
        if (code < least || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu)) {
            return false;
        }
        i += more + 1u;
    }
    return true;
}

// The link's site (section 1 item 7, section 6): the host lowercased, or "" when the link is not one the service opens.
std::string link_site(const char* url) {
    const std::size_t length = std::strlen(url);
    if (length > kUrlMost) {
        return {};
    }
    for (std::size_t i = 0; i < length; i++) {
        const unsigned char c = static_cast<unsigned char>(url[i]);
        if (c <= 0x20u || c == 0x7Fu) {
            return {};
        }
    }
    const auto starts = [&](const char* scheme) {
        const std::size_t n = std::strlen(scheme);
        if (length < n) {
            return false;
        }
        for (std::size_t i = 0; i < n; i++) {
            const char c = url[i];
            const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            if (lower != scheme[i]) {
                return false;
            }
        }
        return true;
    };
    std::size_t at = 0;
    if (starts("https://")) {
        at = 8;
    } else if (starts("http://")) {
        at = 7;
    } else {
        return {};
    }
    std::size_t end = at;
    while (end < length && url[end] != '/' && url[end] != '?' && url[end] != '#') {
        end++;
    }
    const std::string_view authority(url + at, end - at);
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return {};  // no host, or a userinfo part (J-SS2)
    }
    std::string_view host = authority;
    std::string_view port;
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos || close < 2u) {
            return {};
        }
        host = authority.substr(0, close + 1u);
        const std::string_view rest = authority.substr(close + 1u);
        if (!rest.empty()) {
            if (rest.front() != ':') {
                return {};
            }
            port = rest.substr(1);
        }
        for (const char c : host.substr(1, host.size() - 2u)) {
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hex && c != ':' && c != '.') {
                return {};
            }
        }
    } else {
        const std::size_t colon = authority.find(':');
        if (colon != std::string_view::npos) {
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1u);
        }
        if (host.empty() || host.front() == '.' || host.front() == '-') {
            return {};
        }
        for (const char c : host) {
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!letter && c != '-' && c != '.') {
                return {};  // not ASCII (an internationalized name is written in its xn-- form), or not a host
            }
        }
    }
    if (port.size() > 5u) {
        return {};
    }
    for (const char c : port) {
        if (c < '0' || c > '9') {
            return {};
        }
    }
    std::string site(host);
    for (char& c : site) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return site;
}

// ---- The display (section 4): one writer at a time, read by a sequence count -------------------------------------

constexpr std::size_t kDisplayWords = sizeof(LumaSystemDisplay) / sizeof(std::uint32_t);
std::mutex g_display_write;
LumaSystemDisplay g_display_last{};  // the writer's (g_display_write)
std::atomic<std::uint32_t> g_display_sequence{0};
std::array<std::atomic<std::uint32_t>, kDisplayWords> g_display_words{};

LumaSystemDisplay read_display() noexcept {
    std::array<std::uint32_t, kDisplayWords> words{};
    for (;;) {
        const std::uint32_t before = g_display_sequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) {
            continue;
        }
        for (std::size_t i = 0; i < kDisplayWords; i++) {
            words[i] = g_display_words[i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_display_sequence.load(std::memory_order_relaxed) == before) {
            break;
        }
    }
    LumaSystemDisplay out{};
    std::memcpy(&out, words.data(), sizeof(out));
    return out;
}

// A window layer that never gave a snapshot (changes 0: iOS until its host window calls apple_window_changed) cannot
// say the game is behind another; the game runs only in front there, so it counts as in front rather than every
// request answering BUSY for good.
bool window_in_front() noexcept {
    if (deterministic()) {
        return true;
    }
    const LumaSystemDisplay display = read_display();
    return display.changes == 0u || display.focused != 0u;
}

// ---- Requests (section 5) ------------------------------------------------------------------------------------------

struct Request;
struct ModState;

// The kernel's node and the request it belongs to (a node pointer converts to this, its first member).
struct Node {
    LumaEventNode event;
    Request* request;
};

struct Request {
    Node node{};
    ModState* owner = nullptr;  // g_lock; null once the mod is gone (then nothing is posted or called for it)
    Request* next = nullptr;    // g_lock: the owner's list
    bool links = false;         // system.links; else system.clipboard
    bool cancelled = false;     // g_lock: the mod cancelled it (no event)
    bool answered = false;      // g_lock: posted
    bool taken = false;         // game thread, in the event's call
    std::uint32_t kind = 0;
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    // The answer: written by the platform's thread before it takes g_lock to post, read on the game thread after.
    std::int32_t status = LUMA_OK;
    std::string text;
    Image image;
    std::string mod_id;  // the Notes' (the mod may be gone when the platform answers)
    std::string site;
};

struct ModState {
    const LumaMod* mod = nullptr;
    std::string id;
    std::uint32_t clipboard_open = 0;  // game thread
    bool link_open = false;            // game thread
    bool linked = false;               // game thread
    std::uint64_t last_link_ns = 0;    // game thread
    Request* first = nullptr;          // g_lock
};

std::mutex g_lock;
std::vector<std::unique_ptr<ModState>> g_mods;  // game thread
std::atomic<LumaHandle> g_next_handle{1};
Request* g_in_call = nullptr;  // game thread: the request whose event's call runs

// The deterministic clipboard (section 1 item 8): the process's own, empty at start. Any thread (g_private_lock).
std::mutex g_private_lock;
bool g_private_has_text = false;
std::string g_private_text;
Image g_private_image;

ModState* state_of(const LumaMod* mod) noexcept {
    for (const std::unique_ptr<ModState>& state : g_mods) {
        if (state->mod == mod) {
            return state.get();
        }
    }
    return nullptr;
}

void unlink(Request* request) noexcept {  // g_lock
    if (request->owner == nullptr) {
        return;
    }
    for (Request** at = &request->owner->first; *at != nullptr; at = &(*at)->next) {
        if (*at == request) {
            *at = request->next;
            break;
        }
    }
    request->next = nullptr;
    request->owner = nullptr;
}

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

// The mod known and live, then the thread (services-registry 5). LUMA_OK or the status to return.
std::int32_t enter(const LumaMod* mod, const char* where, bool any_thread) noexcept {
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
    if (!any_thread && kernel->game_thread() == 0) {
        report(mod, where, 100u);  // Service.Thread
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

std::int32_t argument(const LumaMod* mod, const char* where, const char* name) noexcept {
    report(mod, where, 101u, name);  // Service.Argument
    return LUMA_E_ARGUMENT;
}

std::int32_t struct_size(const LumaMod* mod, const char* where, const char* name, std::uint32_t found,
                         std::uint32_t least) noexcept {
    char found_text[16];
    char least_text[16];
    std::snprintf(found_text, sizeof(found_text), "%u", found);
    std::snprintf(least_text, sizeof(least_text), "%u", least);
    report(mod, where, 103u, name, found_text, least_text);  // Service.StructSize
    return LUMA_E_ARGUMENT;
}

void deliver(LumaEventNode* node);
void drop(LumaEventNode* node);

// A new request of the mod's, linked and counted (game thread). Null: no memory.
Request* open_request(ModState& state, const LumaMod* mod, bool links, std::uint32_t kind, LumaEventFn fn, void* user) {
    Request* request = new (std::nothrow) Request();
    if (request == nullptr) {
        return nullptr;
    }
    request->node.event.mod = mod;
    request->node.event.deliver = deliver;
    request->node.event.drop = drop;
    request->node.request = request;
    request->links = links;
    request->kind = kind;
    request->handle = g_next_handle.fetch_add(1u, std::memory_order_relaxed);
    request->fn = fn;
    request->user = user;
    request->mod_id = state.id;
    if (links) {
        state.link_open = true;
    } else {
        state.clipboard_open++;
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    request->owner = &state;
    request->next = state.first;
    state.first = request;
    return request;
}

// The request no longer counts against its mod's limits (game thread).
void close_count(ModState& state, const Request& request) noexcept {
    if (request.links) {
        state.link_open = false;
    } else if (state.clipboard_open > 0u) {
        state.clipboard_open--;
    }
}

// The platform's answer (any thread, once): the Notes, then the event for the frame's start. `status` and the
// request's text or image are written before this is called.
void answer(Request* request) {
    // The Notes are the player's to see whatever the mod did meanwhile (section 6): a link the system took, and a read
    // that gave something to a mod still waiting for it.
    const bool link_opened = request->links && request->status == LUMA_OK;
    const bool read = !request->links && request->status == LUMA_OK &&
                      (request->kind == LUMA_SYSTEM_CLIPBOARD_EVENT_TEXT || request->kind == LUMA_SYSTEM_CLIPBOARD_EVENT_IMAGE);
    const std::string mod_id = request->mod_id;
    const std::string site = request->site;
    bool read_kept = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        if (request->owner == nullptr) {
            // The mod is gone: nothing is posted for it (luma_service_provider.h, mod_gone).
            delete request;
            request = nullptr;
        } else {
            read_kept = read && !request->cancelled;
            request->answered = true;
            const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
            if (kernel == nullptr || kernel->post(&request->node.event) != LUMA_OK) {
                unlink(request);
                delete request;
                request = nullptr;
            }
        }
    }
    if (link_opened) {
        galaxy::modlog::add_row(LUMA_MODLOG_CODE, LUMA_MODLOG_NOTE, mod_id, 140u, site, "System.LinkOpened", {{"site", site}});
    }
    if (read_kept) {
        galaxy::modlog::add_row(LUMA_MODLOG_CODE, LUMA_MODLOG_NOTE, mod_id, 141u, "", "System.ClipboardRead", {});
    }
}

// Game thread, at the frame's start: the event, unless the request was cancelled or its mod is gone; then the request
// is closed (section 1 item 10).
void deliver(LumaEventNode* node) {
    Request* const request = reinterpret_cast<Node*>(node)->request;
    bool call = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        if (request->owner != nullptr) {
            call = !request->cancelled;
            if (call) {
                close_count(*request->owner, *request);
            }
            unlink(request);
        }
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (call && kernel != nullptr) {
        LumaEvent event{};
        event.size = sizeof(event);
        event.kind = request->kind;
        event.status = request->status;
        event.handle = request->handle;
        if (request->status == LUMA_OK) {
            if (request->kind == LUMA_SYSTEM_CLIPBOARD_EVENT_TEXT) {
                event.value = request->text.size();
            } else if (request->kind == LUMA_SYSTEM_CLIPBOARD_EVENT_IMAGE) {
                event.value = (static_cast<std::uint64_t>(request->image.width) << 32u) | request->image.height;
            }
        }
        g_in_call = request;
        kernel->call(node->mod, request->links ? kLinksName : kClipboardName, request->fn, request->user, &event);
        g_in_call = nullptr;
    }
    delete request;
}

// Game thread: the mod is gone and the node was queued.
void drop(LumaEventNode* node) {
    Request* const request = reinterpret_cast<Node*>(node)->request;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        unlink(request);
    }
    delete request;
}

// The request answered at once with `status` (the platform not asked).
void answer_now(Request* request, std::int32_t status) {
    request->status = status;
    answer(request);
}

// ---- system --------------------------------------------------------------------------------------------------------

std::int32_t system_facts(const LumaMod* mod, LumaSystemFacts* out) {
    const char* const where = "system.facts";
    if (const std::int32_t status = enter(mod, where, true); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (out->size < kFactsLeast) {
        return struct_size(mod, where, "LumaSystemFacts", out->size, kFactsLeast);
    }
    LumaSystemFacts facts{};
    if (const Platform* platform = g_platform.load(std::memory_order_acquire)) {
        platform->facts(facts);
    }
    facts.size = out->size;
    facts.lumacore_major = LUMA_API;
    facts.lumacore_minor = 0;
    const std::string game_id = galaxy::mods::mods_game_id();
    copy_text(facts.game_id, sizeof(facts.game_id), game_id);
    // Only the three releases' discs run (RMGE01, RMGP01, RMGJ01).
    facts.region = game_id.size() > 3u && game_id[3] == 'P' ? LUMA_SYSTEM_EU
                   : game_id.size() > 3u && game_id[3] == 'J' ? LUMA_SYSTEM_JP
                                                              : LUMA_SYSTEM_US;
    // The language the game plays, as the host set it before the mods loaded (the one their text is read in).
    copy_text(facts.language, sizeof(facts.language), galaxy::mods::language_folder(game_id, galaxy::mods::played_language()));
    copy_text(facts.game_version, sizeof(facts.game_version), NEBULA_VERSION);
    {
        const std::lock_guard<std::mutex> lock(g_renderer_lock);
        if (g_renderer_known) {
            facts.renderer = static_cast<LumaSystemRenderer>(g_renderer);
        }
        std::memcpy(facts.gpu, g_gpu.data(), sizeof(facts.gpu));
    }
    if (deterministic()) {
        // Runs compare (section 1 item 8).
        std::memset(facts.os, 0, sizeof(facts.os));
        std::memset(facts.gpu, 0, sizeof(facts.gpu));
        copy_text(facts.locale, sizeof(facts.locale), "en-US");
    } else if (facts.locale[0] == 0) {
        copy_text(facts.locale, sizeof(facts.locale), "und");
    }
    std::memcpy(out, &facts, sizeof(facts));
    return LUMA_OK;
}

std::int32_t system_display(const LumaMod* mod, LumaSystemDisplay* out) {
    const char* const where = "system.display";
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (out->size < kDisplayLeast) {
        return struct_size(mod, where, "LumaSystemDisplay", out->size, kDisplayLeast);
    }
    LumaSystemDisplay display = read_display();
    display.size = out->size;
    if (deterministic()) {
        display.focused = 1u;
    }
    std::memcpy(out, &display, sizeof(display));
    return LUMA_OK;
}

bool clipboard_images() noexcept {
    if (deterministic()) {
        return true;
    }
    const Platform* platform = g_platform.load(std::memory_order_acquire);
    if (platform == nullptr) {
        return false;
    }
    LumaSystemFacts facts{};
    platform->facts(facts);
    return facts.clipboard_images != 0u;
}

// ---- system.clipboard ----------------------------------------------------------------------------------------------

// The checks every request shares: the mod, the thread, fn and out, the limit. Null: the status in *status.
ModState* clipboard_enter(const LumaMod* mod, const char* where, LumaEventFn fn, LumaHandle* out, std::int32_t* status) {
    *status = enter(mod, where, false);
    if (*status != LUMA_OK) {
        return nullptr;
    }
    if (fn == nullptr) {
        *status = argument(mod, where, "fn");
        return nullptr;
    }
    if (out == nullptr) {
        *status = argument(mod, where, "out");
        return nullptr;
    }
    ModState* state = state_of(mod);
    if (state == nullptr) {
        *status = LUMA_E_CANCELLED;
        return nullptr;
    }
    if (state->clipboard_open >= kClipboardMost) {
        char count[16];
        std::snprintf(count, sizeof(count), "%u", kClipboardMost);
        report(mod, where, 104u, count);  // Service.Limit
        *status = LUMA_E_LIMIT;
        return nullptr;
    }
    return state;
}

std::int32_t clipboard_get_text(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out) {
    std::int32_t status = LUMA_OK;
    ModState* state = clipboard_enter(mod, "system.clipboard.get_text", fn, out, &status);
    if (state == nullptr) {
        return status;
    }
    Request* request = open_request(*state, mod, false, LUMA_SYSTEM_CLIPBOARD_EVENT_TEXT, fn, user);
    if (request == nullptr) {
        return LUMA_E_MEMORY;
    }
    *out = request->handle;
    if (!window_in_front()) {
        answer_now(request, LUMA_E_BUSY);
    } else if (deterministic()) {
        {
            const std::lock_guard<std::mutex> lock(g_private_lock);
            if (g_private_has_text) {
                request->text = g_private_text;
            }
            request->status = g_private_has_text ? LUMA_OK : LUMA_E_NOT_FOUND;
        }
        answer(request);
    } else if (const Platform* platform = g_platform.load(std::memory_order_acquire)) {
        platform->read_text([request](std::int32_t answered, std::string text) {
            if (answered == LUMA_OK && text.size() > kTextMost) {
                answered = LUMA_E_TOO_LARGE;
            } else if (answered == LUMA_OK && !valid_text(text.data(), text.size())) {
                answered = LUMA_E_FORMAT;  // a system that handed bytes that are not UTF-8: the mod gets UTF-8 only
            } else if (answered == LUMA_OK) {
                request->text = std::move(text);
            }
            request->status = answered;
            answer(request);
        });
    } else {
        answer_now(request, LUMA_E_ABSENT);
    }
    return LUMA_PENDING;
}

int64_t clipboard_take_text(const LumaMod* mod, LumaHandle handle, char* buf, std::uint64_t size) {
    const char* const where = "system.clipboard.take_text";
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    Request* request = g_in_call;
    if (request == nullptr || request->handle != handle || request->node.event.mod != mod || request->links ||
        request->kind != LUMA_SYSTEM_CLIPBOARD_EVENT_TEXT || request->status != LUMA_OK || request->taken) {
        report(mod, where, 102u);  // Service.Handle
        return LUMA_E_HANDLE;
    }
    if (buf == nullptr) {
        return argument(mod, where, "buf");
    }
    if (size == 0u) {
        return argument(mod, where, "size");
    }
    const std::uint64_t length = request->text.size();
    const std::uint64_t copied = std::min<std::uint64_t>(length, size - 1u);
    std::memcpy(buf, request->text.data(), static_cast<std::size_t>(copied));
    buf[copied] = 0;
    request->taken = true;
    return static_cast<int64_t>(length);
}

std::int32_t clipboard_get_image(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out) {
    const char* const where = "system.clipboard.get_image";
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    if (!clipboard_images()) {
        return LUMA_E_ABSENT;  // an answer (section 2)
    }
    std::int32_t status = LUMA_OK;
    ModState* state = clipboard_enter(mod, where, fn, out, &status);
    if (state == nullptr) {
        return status;
    }
    Request* request = open_request(*state, mod, false, LUMA_SYSTEM_CLIPBOARD_EVENT_IMAGE, fn, user);
    if (request == nullptr) {
        return LUMA_E_MEMORY;
    }
    *out = request->handle;
    if (!window_in_front()) {
        answer_now(request, LUMA_E_BUSY);
    } else if (deterministic()) {
        {
            const std::lock_guard<std::mutex> lock(g_private_lock);
            const bool has = !g_private_image.rgba.empty();
            if (has) {
                request->image.width = g_private_image.width;
                request->image.height = g_private_image.height;
                request->image.rgba = g_private_image.rgba;
            }
            request->status = has ? LUMA_OK : LUMA_E_NOT_FOUND;
        }
        answer(request);
    } else if (const Platform* platform = g_platform.load(std::memory_order_acquire)) {
        platform->read_image([request](std::int32_t answered, Image image) {
            if (answered == LUMA_OK && image.rgba.empty()) {
                answered = decode(image);
            }
            if (answered == LUMA_OK) {
                const std::uint64_t bytes = std::uint64_t{image.width} * image.height * 4u;
                if (image.width == 0u || image.height == 0u || image.width > kSideMost || image.height > kSideMost ||
                    bytes > kPixelsMost) {
                    answered = LUMA_E_TOO_LARGE;
                } else if (image.rgba.size() != bytes) {
                    answered = LUMA_E_FORMAT;
                } else {
                    request->image = std::move(image);
                }
            }
            request->status = answered;
            answer(request);
        });
    } else {
        answer_now(request, LUMA_E_ABSENT);
    }
    return LUMA_PENDING;
}

std::int32_t clipboard_take_image(const LumaMod* mod, LumaHandle handle, std::uint8_t* rgba, std::uint64_t size) {
    const char* const where = "system.clipboard.take_image";
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    Request* request = g_in_call;
    if (request == nullptr || request->handle != handle || request->node.event.mod != mod || request->links ||
        request->kind != LUMA_SYSTEM_CLIPBOARD_EVENT_IMAGE || request->status != LUMA_OK || request->taken) {
        report(mod, where, 102u);  // Service.Handle
        return LUMA_E_HANDLE;
    }
    if (rgba == nullptr) {
        return argument(mod, where, "rgba");
    }
    if (size < request->image.rgba.size()) {
        return argument(mod, where, "size");
    }
    std::memcpy(rgba, request->image.rgba.data(), request->image.rgba.size());
    request->taken = true;
    return LUMA_OK;
}

std::int32_t clipboard_set_text(const LumaMod* mod, const char* text, std::uint64_t bytes, LumaEventFn fn, void* user,
                                LumaHandle* out) {
    const char* const where = "system.clipboard.set_text";
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    if (text == nullptr) {
        return argument(mod, where, "text");
    }
    if (bytes > kTextMost) {
        report(mod, where, 101u, "bytes");  // Service.Argument: more than the contract's 16 MiB
        return LUMA_E_TOO_LARGE;
    }
    if (!valid_text(text, bytes)) {
        return argument(mod, where, "text");
    }
    std::int32_t status = LUMA_OK;
    ModState* state = clipboard_enter(mod, where, fn, out, &status);
    if (state == nullptr) {
        return status;
    }
    std::string copy;
    try {
        copy.assign(text, static_cast<std::size_t>(bytes));
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    Request* request = open_request(*state, mod, false, LUMA_SYSTEM_CLIPBOARD_EVENT_SET, fn, user);
    if (request == nullptr) {
        return LUMA_E_MEMORY;
    }
    *out = request->handle;
    if (deterministic()) {
        {
            const std::lock_guard<std::mutex> lock(g_private_lock);
            g_private_text = std::move(copy);
            g_private_has_text = true;
            g_private_image = Image{};  // the clipboard holds one thing, as the system's does
        }
        answer_now(request, LUMA_OK);
    } else if (const Platform* platform = g_platform.load(std::memory_order_acquire)) {
        platform->write_text(std::move(copy), [request](std::int32_t answered) { answer_now(request, answered); });
    } else {
        answer_now(request, LUMA_E_ABSENT);
    }
    return LUMA_PENDING;
}

std::int32_t clipboard_set_image(const LumaMod* mod, const std::uint8_t* rgba, std::uint64_t size, std::uint32_t width,
                                 std::uint32_t height, LumaEventFn fn, void* user, LumaHandle* out) {
    const char* const where = "system.clipboard.set_image";
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    if (!clipboard_images()) {
        return LUMA_E_ABSENT;  // an answer (section 2)
    }
    if (rgba == nullptr) {
        return argument(mod, where, "rgba");
    }
    if (width == 0u || width > kSideMost) {
        return argument(mod, where, "width");
    }
    const std::uint64_t bytes = std::uint64_t{width} * height * 4u;
    if (height == 0u || height > kSideMost || bytes > kPixelsMost) {
        return argument(mod, where, "height");
    }
    // The buffer's own length (registry 3, G R-5d): a script passes bytes, and the pixels are never read past it.
    if (size != bytes) {
        return argument(mod, where, "size");
    }
    std::int32_t status = LUMA_OK;
    ModState* state = clipboard_enter(mod, where, fn, out, &status);
    if (state == nullptr) {
        return status;
    }
    Image image;
    image.width = width;
    image.height = height;
    try {
        image.rgba.assign(rgba, rgba + bytes);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    Request* request = open_request(*state, mod, false, LUMA_SYSTEM_CLIPBOARD_EVENT_SET, fn, user);
    if (request == nullptr) {
        return LUMA_E_MEMORY;
    }
    *out = request->handle;
    if (deterministic()) {
        {
            const std::lock_guard<std::mutex> lock(g_private_lock);
            g_private_image = std::move(image);
            g_private_text.clear();
            g_private_has_text = false;
        }
        answer_now(request, LUMA_OK);
    } else if (const Platform* platform = g_platform.load(std::memory_order_acquire)) {
        platform->write_image(std::move(image), [request](std::int32_t answered) { answer_now(request, answered); });
    } else {
        answer_now(request, LUMA_E_ABSENT);
    }
    return LUMA_PENDING;
}

// Both services' cancel (registry 4.8): no event for the request after this returns.
std::int32_t cancel_request(const LumaMod* mod, LumaHandle handle, bool links, const char* where) {
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return LUMA_E_CANCELLED;
    }
    bool found = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        for (Request* request = state->first; request != nullptr; request = request->next) {
            if (request->handle == handle && request->links == links && !request->cancelled) {
                // It stays linked until the platform answers or the node is delivered: then it is freed, no call.
                request->cancelled = true;
                found = true;
                if (links) {
                    state->link_open = false;
                } else if (state->clipboard_open > 0u) {
                    state->clipboard_open--;
                }
                break;
            }
        }
    }
    if (!found) {
        report(mod, where, 102u);  // Service.Handle
        return LUMA_E_HANDLE;
    }
    return LUMA_OK;
}

std::int32_t clipboard_cancel(const LumaMod* mod, LumaHandle handle) {
    return cancel_request(mod, handle, false, "system.clipboard.cancel");
}

// ---- system.links --------------------------------------------------------------------------------------------------

std::int32_t links_open(const LumaMod* mod, const char* url, LumaEventFn fn, void* user, LumaHandle* out) {
    const char* const where = "system.links.open";
    if (const std::int32_t status = enter(mod, where, false); status != LUMA_OK) {
        return status;
    }
    if (url == nullptr) {
        return argument(mod, where, "url");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    std::string site = link_site(url);
    if (site.empty()) {
        return argument(mod, where, "url");
    }
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return LUMA_E_CANCELLED;
    }
    const std::uint64_t now = monotonic_ns();
    if (state->link_open || (state->linked && now - state->last_link_ns < kLinkSpacingNs) || !window_in_front()) {
        return LUMA_E_BUSY;  // an answer, not reported
    }
    Request* request = open_request(*state, mod, true, LUMA_SYSTEM_LINKS_EVENT_LINK, fn, user);
    if (request == nullptr) {
        return LUMA_E_MEMORY;
    }
    request->site = std::move(site);
    state->linked = true;
    state->last_link_ns = now;
    *out = request->handle;
    if (g_log != nullptr) {
        const std::string line = "LumaCore: " + state->id + " opens " + url;
        g_log(line.c_str());
    }
    if (deterministic()) {
        answer_now(request, LUMA_OK);  // no browser (section 6)
    } else if (const Platform* platform = g_platform.load(std::memory_order_acquire)) {
        platform->open_link(url, [request](std::int32_t answered) { answer_now(request, answered); });
    } else {
        answer_now(request, LUMA_E_ABSENT);
    }
    return LUMA_PENDING;
}

std::int32_t links_cancel(const LumaMod* mod, LumaHandle handle) {
    return cancel_request(mod, handle, true, "system.links.cancel");
}

// ---- The providers -------------------------------------------------------------------------------------------------

void mod_begin(void*, const LumaMod* mod) {
    if (state_of(mod) != nullptr) {
        return;  // the three providers share one record a mod
    }
    auto state = std::make_unique<ModState>();
    state->mod = mod;
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        if (const char* id = kernel->mod_id(mod)) {
            state->id = id;
        }
    }
    g_mods.push_back(std::move(state));
}

// After the mod's stop: its requests are the platform's to finish (answer frees them) or the kernel's to drop; nothing
// is posted or called for the mod from here on.
void mod_gone(void*, const LumaMod* mod) {
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    while (state->first != nullptr) {
        unlink(state->first);
    }
    state->clipboard_open = 0;
    state->link_open = false;
}

void mod_end(void*, const LumaMod* mod) {
    for (auto it = g_mods.begin(); it != g_mods.end(); ++it) {
        if ((*it)->mod == mod) {
            g_mods.erase(it);
            return;
        }
    }
}

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaSystemService kSystem = {
    {sizeof(LumaSystemService), LUMA_SYSTEM_MAJOR, LUMA_SYSTEM_MINOR},
    system_facts,
    system_display,
};

const LumaSystemClipboardService kClipboard = {
    {sizeof(LumaSystemClipboardService), LUMA_SYSTEM_CLIPBOARD_MAJOR, LUMA_SYSTEM_CLIPBOARD_MINOR},
    clipboard_get_text,
    clipboard_take_text,
    clipboard_get_image,
    clipboard_take_image,
    clipboard_set_text,
    clipboard_set_image,
    clipboard_cancel,
};

const LumaSystemLinksService kLinks = {
    {sizeof(LumaSystemLinksService), LUMA_SYSTEM_LINKS_MAJOR, LUMA_SYSTEM_LINKS_MINOR},
    links_open,
    links_cancel,
};

// The Notes (rows 140, 141) go through the mod log with their rows (section 6), not through report: no report rows.
const LumaServiceProvider kSystemProvider = {
    sizeof(LumaServiceProvider), LUMA_SYSTEM_NAME, &kSystem.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider kClipboardProvider = {
    sizeof(LumaServiceProvider), LUMA_SYSTEM_CLIPBOARD_NAME, &kClipboard.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider kLinksProvider = {
    sizeof(LumaServiceProvider), LUMA_SYSTEM_LINKS_NAME, &kLinks.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider* const kProviders[] = {&kSystemProvider, &kClipboardProvider, &kLinksProvider};

}  // namespace

void set_platform(const Platform* platform) noexcept {
    g_platform.store(platform, std::memory_order_release);
}

void display_changed(const LumaSystemDisplay& now) noexcept {
    const std::lock_guard<std::mutex> lock(g_display_write);
    LumaSystemDisplay next = now;
    next.size = sizeof(LumaSystemDisplay);
    next.changes = g_display_last.changes;
    if (std::memcmp(&next, &g_display_last, sizeof(next)) == 0 && g_display_last.changes != 0u) {
        return;
    }
    next.changes = g_display_last.changes + 1u;
    g_display_last = next;
    std::array<std::uint32_t, kDisplayWords> words{};
    std::memcpy(words.data(), &next, sizeof(next));
    const std::uint32_t sequence = g_display_sequence.load(std::memory_order_relaxed);
    g_display_sequence.store(sequence + 1u, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t i = 0; i < kDisplayWords; i++) {
        g_display_words[i].store(words[i], std::memory_order_relaxed);
    }
    g_display_sequence.store(sequence + 2u, std::memory_order_release);
}

void set_renderer(std::uint32_t renderer, std::string_view gpu) noexcept {
    const std::lock_guard<std::mutex> lock(g_renderer_lock);
    g_renderer_known = true;
    g_renderer = renderer;
    copy_text(g_gpu.data(), g_gpu.size(), gpu);
}

bool encode_png(Image& image) noexcept {
    if (image.width == 0u || image.height == 0u || image.rgba.size() != std::uint64_t{image.width} * image.height * 4u) {
        return false;
    }
    try {
        std::vector<std::uint8_t> encoded;
        const auto write = [](void* context, void* data, int size) {
            auto* out = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            out->insert(out->end(), bytes, bytes + size);
        };
        if (stbi_write_png_to_func(write, &encoded, static_cast<int>(image.width), static_cast<int>(image.height), 4,
                                   image.rgba.data(), static_cast<int>(image.width * 4u)) == 0) {
            return false;
        }
        image.encoded = std::move(encoded);
        return true;
    } catch (...) {
        return false;
    }
}

std::int32_t decode(Image& image) noexcept {
    if (image.encoded.empty() || image.encoded.size() > static_cast<std::size_t>(INT32_MAX)) {
        return LUMA_E_FORMAT;
    }
    const auto* bytes = image.encoded.data();
    const int length = static_cast<int>(image.encoded.size());
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_info_from_memory(bytes, length, &width, &height, &channels) == 0 || width <= 0 || height <= 0) {
        return LUMA_E_FORMAT;
    }
    if (static_cast<std::uint32_t>(width) > kSideMost || static_cast<std::uint32_t>(height) > kSideMost ||
        std::uint64_t(width) * std::uint64_t(height) * 4u > kPixelsMost) {
        return LUMA_E_TOO_LARGE;
    }
    stbi_uc* pixels = stbi_load_from_memory(bytes, length, &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return LUMA_E_FORMAT;
    }
    try {
        image.rgba.assign(pixels, pixels + std::size_t(width) * std::size_t(height) * 4u);
    } catch (...) {
        stbi_image_free(pixels);
        return LUMA_E_MEMORY;
    }
    stbi_image_free(pixels);
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.encoded.clear();
    image.encoded.shrink_to_fit();
    return LUMA_OK;
}

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*), std::uint64_t (*monotonic)()) noexcept {
    g_log = log;
    g_monotonic = monotonic;
    return kProviders;
}

}  // namespace galaxy::luma_system
