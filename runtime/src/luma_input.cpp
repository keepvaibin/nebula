// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's input services (contract services-input, luma_input.h): `input` (a mod's own actions, its luma.json `key`
// options, read each game frame as the controls sampled them (input/mod_actions.h); the game's controller state; the
// device kind; text entry; rumble), `input.inject` (a mod supplies a player's controller) and `input.dev` (raw HID).
//
// The host feeds it (luma_input_service.h): game_read at each of the game's controller reads, the window's text keys,
// and whether a motor is driven. With no mod nothing here runs but one atomic load per read.

#include "galaxy/luma_input_service.h"

#include "galaxy/input/mod_actions.h"
#include "galaxy/input_bindings.h"
#include "galaxy/pointer_control.h"
#include "galaxy/mods.h"

#include <luma_input.h>
#include <nebula_game_api.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <hidsdi.h>
#include <hidpi.h>
#include <SetupAPI.h>
#include <thread>
// input.dev's HID calls, in every host this file is in (the headless one links neither library itself).
#pragma comment(lib, "hid")
#pragma comment(lib, "setupapi")
#endif

namespace galaxy::luma_input {
namespace {

constexpr std::uint32_t kStateLeast = static_cast<std::uint32_t>(sizeof(LumaInputState));  // 1.0's whole struct
static_assert(sizeof(LumaInputState) == 24u, "LumaInputState 1.0 is 24 bytes (luma_input.h)");
constexpr std::uint32_t kGameLeast = static_cast<std::uint32_t>(sizeof(LumaInputGame));
static_assert(sizeof(LumaInputGame) == 28u, "LumaInputGame 1.1 is 28 bytes (luma_input.h)");
constexpr std::uint32_t kTextRequestLeast = static_cast<std::uint32_t>(sizeof(LumaInputTextRequest));
static_assert(sizeof(LumaInputTextRequest) == 24u, "LumaInputTextRequest 1.1 is 24 bytes (luma_input.h)");
constexpr std::uint32_t kDeviceInfoLeast = static_cast<std::uint32_t>(sizeof(LumaInputDeviceInfo));
static_assert(sizeof(LumaInputDeviceInfo) == 400u, "LumaInputDeviceInfo 1.0 is 400 bytes (luma_input.h)");
constexpr std::uint32_t bit(std::uint32_t value) noexcept {
    return value;
}
static_assert(bit(LUMA_INPUT_BUTTON_A) == bit(NEBULA_BUTTON_A) && bit(LUMA_INPUT_BUTTON_Z) == bit(NEBULA_BUTTON_Z) &&
                  bit(LUMA_INPUT_BUTTON_HOME) == bit(NEBULA_BUTTON_HOME) && bit(LUMA_INPUT_BUTTON_C) == bit(NEBULA_BUTTON_C),
              "the input service's buttons are the game's (nebula_game_api.h)");

constexpr float kReadsPerSecond = 60.0f;  // the game reads its controllers once a frame
constexpr float kRumbleMostSeconds = 10.0f;
constexpr std::size_t kReportsKept = 64u;

std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
void (*g_log)(const char*) = nullptr;  // set by providers() before the first mod
bool g_declared = false;              // game thread: the session's actions are declared (at the first mod's begin)
// Game thread, from declare: (mod, a mod it needs) for each started mod, so a library reads the actions of the mods that
// need it (a kit's move started by its user's action).
std::vector<std::pair<std::string, std::string>> g_needs;

// Anything a mod asked for that game_read must look at (an injection, a rumble, a watch, an open text request): with
// none, game_read only records and returns.
std::atomic<bool> g_active{false};
// A mod with code began this session: before it, game_read is one load and a return.
std::atomic<bool> g_mods{false};

// ---- Calling rules (services-registry 4) ---------------------------------------------------------------------------

const LumaServiceKernel* kernel() noexcept {
    return g_kernel.load(std::memory_order_acquire);
}

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr, const char* b = nullptr,
            const char* c = nullptr) noexcept {
    const char* args[3] = {a, b, c};
    std::uint32_t count = 0;
    while (count < 3u && args[count] != nullptr) {
        count++;
    }
    if (const LumaServiceKernel* k = kernel()) {
        k->report(mod, where, code, args, count);
    }
}

// The mod known and live, on the game thread. LUMA_OK or the status to return.
std::int32_t enter(const LumaMod* mod, const char* where) noexcept {
    const LumaServiceKernel* k = kernel();
    if (k == nullptr) {
        return LUMA_E_CANCELLED;
    }
    if (mod == nullptr || k->mod_id(mod) == nullptr) {
        k->unknown_mod(where);
        return LUMA_E_ARGUMENT;
    }
    if (k->mod_live(mod) == 0) {
        return LUMA_E_CANCELLED;
    }
    if (k->game_thread() == 0) {
        report(mod, where, 100u);  // Service.Thread
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

std::int32_t argument(const LumaMod* mod, const char* where, const char* name) noexcept {
    report(mod, where, 101u, name);  // Service.Argument
    return LUMA_E_ARGUMENT;
}

std::int32_t bad_handle(const LumaMod* mod, const char* where) noexcept {
    report(mod, where, 102u);  // Service.Handle
    return LUMA_E_HANDLE;
}

std::int32_t struct_size(const LumaMod* mod, const char* where, const char* name, std::uint32_t found, std::uint32_t least) noexcept {
    char found_text[16];
    char least_text[16];
    std::snprintf(found_text, sizeof(found_text), "%u", found);
    std::snprintf(least_text, sizeof(least_text), "%u", least);
    report(mod, where, 103u, name, found_text, least_text);  // Service.StructSize
    return LUMA_E_ARGUMENT;
}

// Handles never given twice in a session (luma_services.h): one counter for every kind here.
std::atomic<LumaHandle> g_next_handle{1u};
LumaHandle new_handle() noexcept {
    return g_next_handle.fetch_add(1u, std::memory_order_relaxed);
}

// ---- UTF-8 ----------------------------------------------------------------------------------------------------------

// The code points of `text`, or nothing when it is not UTF-8 (overlong forms, surrogates and past U+10FFFF refused).
std::optional<std::u32string> decode_utf8(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        char32_t point = 0;
        if (byte < 0x80u) {
            point = byte;
        } else if ((byte & 0xE0u) == 0xC0u) {
            extra = 1;
            point = byte & 0x1Fu;
        } else if ((byte & 0xF0u) == 0xE0u) {
            extra = 2;
            point = byte & 0x0Fu;
        } else if ((byte & 0xF8u) == 0xF0u) {
            extra = 3;
            point = byte & 0x07u;
        } else {
            return std::nullopt;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            if (i + k >= text.size()) {
                return std::nullopt;
            }
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u) {
                return std::nullopt;
            }
            point = (point << 6) | (next & 0x3Fu);
        }
        static constexpr char32_t kLeast[] = {0, 0x80, 0x800, 0x10000};
        if (point < kLeast[extra] || point > 0x10FFFFu || (point >= 0xD800u && point <= 0xDFFFu)) {
            return std::nullopt;
        }
        out.push_back(point);
        i += extra + 1;
    }
    return out;
}

void append_utf8(std::string& out, char32_t point) {
    if (point < 0x80u) {
        out.push_back(static_cast<char>(point));
    } else if (point < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (point >> 6)));
        out.push_back(static_cast<char>(0x80u | (point & 0x3Fu)));
    } else if (point < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (point >> 12)));
        out.push_back(static_cast<char>(0x80u | ((point >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (point & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (point >> 18)));
        out.push_back(static_cast<char>(0x80u | ((point >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((point >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (point & 0x3Fu)));
    }
}

std::string encode_utf8(std::u32string_view points) {
    std::string out;
    out.reserve(points.size());
    for (const char32_t point : points) {
        append_utf8(out, point);
    }
    return out;
}

// ---- The game's reads (input 1.1 game, game_raw, device; input.inject) -------------------------------------------

struct PlayerRead {
    bool read = false;          // the game has read this player at least once
    LumaInputGame game{};       // what the game got
    LumaInputGame raw{};        // what the player gave
    std::uint32_t device = LUMA_INPUT_DEVICE_NONE;
    bool motor = false;         // the host drives a motor for that device
    std::uint64_t reads = 0;    // reads of this player, the clock of rumble
};
PlayerRead g_players[LUMA_INPUT_PLAYERS];  // game thread

struct Injection {
    const LumaMod* mod = nullptr;  // null: none
    LumaHandle handle = 0;
    bool set = false;              // set since the last read
    LumaInputGame state{};
};
Injection g_injections[LUMA_INPUT_PLAYERS];  // game thread

LumaInputGame game_of(const NebulaInputSample& sample, std::uint32_t device, bool injected) {
    LumaInputGame game{};
    game.size = kGameLeast;
    game.buttons = sample.buttons & LUMA_INPUT_BUTTONS_ALL;
    game.stick_x = sample.stick[0];
    game.stick_y = sample.stick[1];
    game.pointer_on = sample.pointer_on != 0u ? 1u : 0u;
    game.pointer_x = sample.pointer[0];
    game.pointer_y = sample.pointer[1];
    game.shake = sample.spin_presses != 0u || sample.sub_spin_presses != 0u ? 1u : 0u;
    game.device = static_cast<std::uint8_t>(device);
    game.injected = injected ? 1u : 0u;
    return game;
}

// A controller at rest: no button, stick centred, not pointing, held level.
void rest(NebulaInputSample& sample) {
    const float distance = sample.distance;
    sample = NebulaInputSample{};
    sample.roll[0] = 1.0f;
    sample.tilt[2] = 1.0f;
    sample.sub_tilt[2] = 1.0f;
    sample.distance = distance;
}

// ---- Mods' records ---------------------------------------------------------------------------------------------------

struct ModRecord {
    LumaEventNode node{};  // the device watch's deliveries
    const LumaMod* mod = nullptr;
    LumaEventFn watch = nullptr;
    void* watch_user = nullptr;
    std::uint32_t changed = 0;  // players whose device changed since the last delivery (bits)
    float rumble[LUMA_INPUT_PLAYERS] = {};
    std::uint64_t rumble_until[LUMA_INPUT_PLAYERS] = {};  // the player's read count it stops at
    bool orphan = false;  // ended while its node was queued: freed by drop
};
std::vector<ModRecord*> g_records;  // game thread

void deliver_device(LumaEventNode* node);
void drop_device(LumaEventNode* node);

ModRecord* record_of(const LumaMod* mod, bool make) {
    for (ModRecord* record : g_records) {
        if (record->mod == mod) {
            return record;
        }
    }
    if (!make) {
        return nullptr;
    }
    auto* record = new ModRecord();
    record->mod = mod;
    record->node.mod = mod;
    record->node.deliver = deliver_device;
    record->node.drop = drop_device;
    g_records.push_back(record);
    return record;
}

void deliver_device(LumaEventNode* node) {
    auto* record = reinterpret_cast<ModRecord*>(node);
    const std::uint32_t changed = record->changed;
    record->changed = 0;
    if (record->watch == nullptr || record->orphan) {
        return;
    }
    for (std::uint32_t player = 0; player < LUMA_INPUT_PLAYERS; ++player) {
        if ((changed & (1u << player)) == 0u) {
            continue;
        }
        LumaEvent event{};
        event.size = sizeof(LumaEvent);
        event.kind = LUMA_INPUT_EVENT_DEVICE;
        event.status = LUMA_OK;
        event.flags = player;
        event.value = g_players[player].device;
        if (kernel()->call(record->mod, LUMA_INPUT_NAME, record->watch, record->watch_user, &event) != LUMA_OK) {
            return;  // the mod is gone
        }
    }
}

void drop_device(LumaEventNode* node) {
    auto* record = reinterpret_cast<ModRecord*>(node);
    record->changed = 0;
    if (record->orphan) {
        delete record;
    }
}

bool any_asked() {
    for (const Injection& injection : g_injections) {
        if (injection.mod != nullptr) {
            return true;
        }
    }
    for (const ModRecord* record : g_records) {
        if (record->watch != nullptr) {
            return true;
        }
        for (std::uint32_t player = 0; player < LUMA_INPUT_PLAYERS; ++player) {
            if (record->rumble[player] > 0.0f) {
                return true;
            }
        }
    }
    return false;
}

// ---- Text entry (input 1.1 text_request) ----------------------------------------------------------------------------

struct TextRequest {
    LumaEventNode node{};  // its one delivery
    const LumaMod* mod = nullptr;
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::uint32_t kind = LUMA_INPUT_TEXT_ANY;
    std::uint32_t most = 0;
    std::u32string text;
    std::size_t caret = 0;
    std::int32_t result = LUMA_PENDING;  // LUMA_OK or LUMA_E_CANCELLED once ended
    std::string answer;                  // the UTF-8 text, made when it ended LUMA_OK
};

std::mutex g_text_mutex;                     // the open request and the list (window thread and game thread)
TextRequest* g_text_open = nullptr;          // under g_text_mutex
std::vector<TextRequest*> g_text_requests;   // ended or open, until delivered or dropped; under g_text_mutex
std::atomic<bool> g_text_available{false};   // the host has text entry (set_text_entry_available)
std::atomic<bool> g_text_is_open{false};

void free_text(TextRequest* request) {
    const std::lock_guard<std::mutex> lock(g_text_mutex);
    g_text_requests.erase(std::remove(g_text_requests.begin(), g_text_requests.end(), request), g_text_requests.end());
    delete request;
}

void deliver_text(LumaEventNode* node) {
    auto* request = reinterpret_cast<TextRequest*>(node);
    LumaEvent event{};
    event.size = sizeof(LumaEvent);
    event.kind = LUMA_INPUT_EVENT_TEXT;
    event.status = request->result;
    event.handle = request->handle;
    if (request->result == LUMA_OK) {
        event.data = request->answer.data();
        event.data_size = request->answer.size();
        event.value = request->text.size();
    }
    (void)kernel()->call(request->mod, LUMA_INPUT_NAME, request->fn, request->user, &event);
    free_text(request);
}

void drop_text(LumaEventNode* node) {
    free_text(reinterpret_cast<TextRequest*>(node));
}

// Under g_text_mutex: the open request ends with `result` and its event is queued. Any thread.
void end_open_text_locked(std::int32_t result) {
    TextRequest* request = g_text_open;
    if (request == nullptr) {
        return;
    }
    g_text_open = nullptr;
    g_text_is_open.store(false, std::memory_order_release);
    request->result = result;
    if (result == LUMA_OK) {
        request->answer = encode_utf8(request->text);
    }
    const LumaServiceKernel* k = kernel();
    if (k == nullptr || k->post(&request->node) != LUMA_OK) {
        // The mod is gone: nothing to tell.
        g_text_requests.erase(std::remove(g_text_requests.begin(), g_text_requests.end(), request), g_text_requests.end());
        delete request;
    }
}

// Whether `point` may go into a request of `kind` at the caret, as the text stands.
bool text_takes(const TextRequest& request, char32_t point) {
    if (point < 0x20u || point == 0x7Fu || (point >= 0x80u && point < 0xA0u)) {
        return false;  // control characters: Enter, Escape and Backspace are keys, not text
    }
    if (request.kind == LUMA_INPUT_TEXT_NUMBER) {
        if (point >= U'0' && point <= U'9') {
            return true;
        }
        if (point == U'-') {
            return request.caret == 0u && request.text.find(U'-') == std::u32string::npos;
        }
        if (point == U'.') {
            return request.text.find(U'.') == std::u32string::npos;
        }
        return false;
    }
    if (request.kind == LUMA_INPUT_TEXT_NAME) {
        return point != 0x2028u && point != 0x2029u;
    }
    return true;
}

// ---- input.dev (raw HID) ----------------------------------------------------------------------------------------------

struct OpenDevice {
    LumaEventNode node{};  // report deliveries
    const LumaMod* mod = nullptr;
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::mutex mutex;                       // the reports (the reader thread and the game thread)
    std::deque<std::vector<std::uint8_t>> reports;
    std::deque<std::uint8_t> cut;           // 1 for a report cut to LUMA_INPUT_REPORT_MOST, beside reports
    std::uint64_t dropped = 0;              // reports dropped since the last delivered
    bool gone = false;                      // the device went away: one LUMA_E_CLOSED event
    bool gone_told = false;
    bool closing = false;                   // closed while queued: freed by deliver or drop (game thread)
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE stop = nullptr;
    std::thread reader;
#endif
};
std::vector<OpenDevice*> g_devices;  // game thread

void stop_device(OpenDevice* device) {
#if defined(_WIN32)
    if (device->stop != nullptr) {
        SetEvent(device->stop);
    }
    if (device->file != INVALID_HANDLE_VALUE) {
        CancelIoEx(device->file, nullptr);
    }
    if (device->reader.joinable()) {
        device->reader.join();
    }
    if (device->file != INVALID_HANDLE_VALUE) {
        CloseHandle(device->file);
        device->file = INVALID_HANDLE_VALUE;
    }
    if (device->stop != nullptr) {
        CloseHandle(device->stop);
        device->stop = nullptr;
    }
#else
    (void)device;
#endif
}

// Game thread: closed by the mod or with it. Its node may be queued: then deliver or drop frees it.
void close_device(OpenDevice* device) {
    stop_device(device);
    g_devices.erase(std::remove(g_devices.begin(), g_devices.end(), device), g_devices.end());
    if (device->node.state != 0u) {
        device->closing = true;
        return;
    }
    delete device;
}

void deliver_reports(LumaEventNode* node) {
    auto* device = reinterpret_cast<OpenDevice*>(node);
    if (device->closing) {
        delete device;
        return;
    }
    // At most kReportsKept a frame: what came after waits for the next (the reader posts again).
    for (std::size_t n = 0; n < kReportsKept; ++n) {
        std::vector<std::uint8_t> report;
        std::uint8_t was_cut = 0;
        std::uint64_t dropped = 0;
        bool closed = false;
        {
            const std::lock_guard<std::mutex> lock(device->mutex);
            if (!device->reports.empty()) {
                report = std::move(device->reports.front());
                device->reports.pop_front();
                was_cut = device->cut.front();
                device->cut.pop_front();
                dropped = device->dropped;
                device->dropped = 0;
            } else if (device->gone && !device->gone_told) {
                device->gone_told = true;
                closed = true;
            } else {
                break;
            }
        }
        LumaEvent event{};
        event.size = sizeof(LumaEvent);
        event.kind = LUMA_INPUT_EVENT_REPORT;
        event.handle = device->handle;
        if (closed) {
            event.status = LUMA_E_CLOSED;
        } else {
            event.status = LUMA_OK;
            event.flags = was_cut;
            event.data = report.data();
            event.data_size = report.size();
            event.value = dropped;
        }
        if (kernel()->call(device->mod, LUMA_INPUT_DEV_NAME, device->fn, device->user, &event) != LUMA_OK) {
            return;
        }
    }
    const std::lock_guard<std::mutex> lock(device->mutex);
    if (!device->reports.empty() || (device->gone && !device->gone_told)) {
        (void)kernel()->post(&device->node);
    }
}

void drop_reports(LumaEventNode* node) {
    auto* device = reinterpret_cast<OpenDevice*>(node);
    if (device->closing) {
        delete device;
    }
}

#if defined(_WIN32)
std::string utf8_of(const wchar_t* wide, int length = -1) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, length, out.data(), bytes, nullptr, nullptr);
    if (length < 0 && !out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

std::wstring wide_of(std::string_view text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

struct HidFacts {
    LumaInputDeviceInfo info{};
    bool keyboard_or_mouse = false;
};

// A HID interface's facts, opened without access (no read: nothing taken from another program).
std::optional<HidFacts> hid_facts(const std::wstring& path) {
    const HANDLE probe = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    HidFacts facts{};
    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(probe, &attributes)) {
        CloseHandle(probe);
        return std::nullopt;
    }
    facts.info.vendor = attributes.VendorID;
    facts.info.product = attributes.ProductID;
    facts.info.version = attributes.VersionNumber;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(probe, &preparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            facts.info.usage_page = caps.UsagePage;
            facts.info.usage = caps.Usage;
            facts.info.input_bytes = caps.InputReportByteLength;
            facts.keyboard_or_mouse = caps.UsagePage == 0x01u && (caps.Usage == 0x02u || caps.Usage == 0x06u);
        }
        HidD_FreePreparsedData(preparsed);
    }
    wchar_t name[127] = {};
    if (HidD_GetProductString(probe, name, sizeof(name) - sizeof(wchar_t))) {
        const std::string text = utf8_of(name);
        std::size_t take = std::min(text.size(), sizeof(facts.info.name) - 1u);
        while (take > 0 && take < text.size() && (static_cast<unsigned char>(text[take]) & 0xC0u) == 0x80u) {
            take--;  // never half a code point
        }
        std::memcpy(facts.info.name, text.data(), take);
    }
    CloseHandle(probe);
    const std::string path_text = utf8_of(path.c_str());
    if (path_text.empty() || path_text.size() >= sizeof(facts.info.path)) {
        return std::nullopt;  // a path too long to hand over is not listed
    }
    std::memcpy(facts.info.path, path_text.data(), path_text.size());
    return facts;
}

// Every HID interface present, in the system's order: visit(path).
template <class Visit>
void each_hid(Visit visit) {
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);
    const HDEVINFO set = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return;
    }
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid_guid, index, &iface); ++index) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
        if (needed == 0) {
            continue;
        }
        std::vector<std::uint8_t> storage(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, nullptr)) {
            continue;
        }
        if (!visit(std::wstring(detail->DevicePath))) {
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
}

// The reader thread: each input report into the device's queue, the node posted when the queue was empty.
void read_reports(OpenDevice* device, std::size_t report_bytes) {
    std::vector<std::uint8_t> buffer(std::max<std::size_t>(report_bytes, 64u));
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        const std::lock_guard<std::mutex> lock(device->mutex);
        device->gone = true;
        (void)kernel()->post(&device->node);
        return;
    }
    for (;;) {
        ResetEvent(overlapped.hEvent);
        DWORD got = 0;
        BOOL done = ReadFile(device->file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, &overlapped);
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            const HANDLE waits[2] = {overlapped.hEvent, device->stop};
            const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (which != WAIT_OBJECT_0) {
                CancelIoEx(device->file, &overlapped);
                GetOverlappedResult(device->file, &overlapped, &got, TRUE);
                break;  // closing
            }
            done = GetOverlappedResult(device->file, &overlapped, &got, FALSE);
        }
        if (!done) {
            if (WaitForSingleObject(device->stop, 0) == WAIT_OBJECT_0) {
                break;
            }
            // Unplugged (ERROR_DEVICE_NOT_CONNECTED) or failed: one CLOSED event.
            const std::lock_guard<std::mutex> lock(device->mutex);
            device->gone = true;
            (void)kernel()->post(&device->node);
            break;
        }
        if (got == 0) {
            continue;
        }
        const std::size_t keep = std::min<std::size_t>(got, LUMA_INPUT_REPORT_MOST);
        const std::lock_guard<std::mutex> lock(device->mutex);
        if (device->reports.size() == kReportsKept) {
            device->reports.pop_front();
            device->cut.pop_front();
            device->dropped++;
        }
        device->reports.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(keep));
        device->cut.push_back(keep < got ? 1u : 0u);
        (void)kernel()->post(&device->node);  // already queued: LUMA_OK, nothing more
    }
    CloseHandle(overlapped.hEvent);
}
#endif

// ---- The actions (input 1.0) ----------------------------------------------------------------------------------------

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
        if (mod.luma) {
            for (const galaxy::luma::Dependency& need : mod.luma->needs) {
                g_needs.emplace_back(mod.id, need.id);
            }
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

// The action `handle` names when `mod` may read it: one of its own, or of a mod that needs it (a library reads its
// users' actions); its index, or -1.
std::int32_t index_of(const LumaMod* mod, LumaInputAction handle) {
    if (handle == 0u || handle > 0x7FFFFFFFu) {
        return -1;
    }
    const std::int32_t index = static_cast<std::int32_t>(handle - 1u);
    const char* const id = kernel()->mod_id(mod);
    if (id == nullptr) {
        return -1;
    }
    const std::string_view owner = galaxy::input::mod_actions::owner(index);
    if (owner.empty()) {
        return -1;
    }
    if (owner == std::string_view(id)) {
        return index;
    }
    for (const std::pair<std::string, std::string>& need : g_needs) {
        if (need.first == owner && need.second == id) {
            return index;
        }
    }
    return -1;
}

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
    const char* const mod_id = kernel()->mod_id(mod);
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
        return struct_size(mod, where, "LumaInputState", out->size, kStateLeast);
    }
    const std::int32_t index = index_of(mod, action);
    if (index < 0) {
        return bad_handle(mod, where);
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

// ---- input 1.1 --------------------------------------------------------------------------------------------------------

std::int32_t game_common(const LumaMod* mod, const char* where, std::uint32_t player, LumaInputGame* out, bool raw) {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (player >= LUMA_INPUT_PLAYERS) {
        return argument(mod, where, "player");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (out->size < kGameLeast) {
        return struct_size(mod, where, "LumaInputGame", out->size, kGameLeast);
    }
    const std::uint32_t size = out->size;
    const PlayerRead& read = g_players[player];
    if (!read.read) {
        std::memset(out, 0, kGameLeast);
        out->size = size;
        return LUMA_E_ABSENT;
    }
    std::memcpy(out, raw ? &read.raw : &read.game, kGameLeast);
    out->size = size;
    return LUMA_OK;
}

std::int32_t input_game(const LumaMod* mod, std::uint32_t player, LumaInputGame* out) {
    return game_common(mod, "input.game", player, out, false);
}

std::int32_t input_game_raw(const LumaMod* mod, std::uint32_t player, LumaInputGame* out) {
    return game_common(mod, "input.game_raw", player, out, true);
}

std::int32_t input_device(const LumaMod* mod, std::uint32_t player, std::uint32_t* out) {
    const char* const where = "input.device";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (player >= LUMA_INPUT_PLAYERS) {
        return argument(mod, where, "player");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    *out = g_players[player].device;
    return LUMA_OK;
}

std::int32_t input_watch_device(const LumaMod* mod, LumaEventFn fn, void* user) {
    const char* const where = "input.watch_device";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    ModRecord* record = record_of(mod, fn != nullptr);
    if (record != nullptr) {
        record->watch = fn;
        record->watch_user = fn != nullptr ? user : nullptr;
        if (fn == nullptr) {
            record->changed = 0;
        }
    }
    g_active.store(any_asked() || g_text_is_open.load(std::memory_order_acquire), std::memory_order_release);
    return LUMA_OK;
}

std::int32_t input_text_request(const LumaMod* mod, const LumaInputTextRequest* request, LumaEventFn fn, void* user,
                                LumaHandle* out) {
    const char* const where = "input.text_request";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (request == nullptr) {
        return argument(mod, where, "request");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    *out = 0u;
    if (request->size < kTextRequestLeast) {
        return struct_size(mod, where, "LumaInputTextRequest", request->size, kTextRequestLeast);
    }
    if (request->kind > LUMA_INPUT_TEXT_NAME) {
        return argument(mod, where, "request.kind");
    }
    if (request->max_length == 0u || request->max_length > LUMA_INPUT_TEXT_MOST) {
        return argument(mod, where, "request.max_length");
    }
    std::u32string initial;
    if (request->initial != nullptr) {
        std::optional<std::u32string> points = decode_utf8(request->initial);
        if (!points) {
            return argument(mod, where, "request.initial");
        }
        initial = std::move(*points);
    }
    if (!g_text_available.load(std::memory_order_acquire)) {
        return LUMA_E_ABSENT;  // an answer: this host has no text entry
    }
    auto* made = new TextRequest();
    made->node.mod = mod;
    made->node.deliver = deliver_text;
    made->node.drop = drop_text;
    made->mod = mod;
    made->handle = new_handle();
    made->fn = fn;
    made->user = user;
    made->kind = request->kind;
    made->most = request->max_length;
    // The initial text as the kind takes it, cut to the most.
    for (const char32_t point : initial) {
        if (made->text.size() == made->most) {
            break;
        }
        if (text_takes(*made, point)) {
            made->text.insert(made->text.begin() + static_cast<std::ptrdiff_t>(made->caret), point);
            made->caret++;
        }
    }
    {
        const std::lock_guard<std::mutex> lock(g_text_mutex);
        end_open_text_locked(LUMA_E_CANCELLED);  // a new request takes the keyboard from the open one
        g_text_requests.push_back(made);
        g_text_open = made;
        g_text_is_open.store(true, std::memory_order_release);
    }
    g_active.store(true, std::memory_order_release);
    *out = made->handle;
    return LUMA_PENDING;
}

std::int32_t input_text_peek(const LumaMod* mod, LumaHandle request, char* buffer, std::uint32_t size, std::uint32_t* length,
                             std::uint32_t* caret) {
    const char* const where = "input.text_peek";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (buffer == nullptr && size > 0u) {
        return argument(mod, where, "buffer");
    }
    std::string text;
    std::size_t caret_at = 0;
    {
        const std::lock_guard<std::mutex> lock(g_text_mutex);
        if (g_text_open == nullptr || g_text_open->mod != mod || g_text_open->handle != request) {
            return bad_handle(mod, where);
        }
        text = encode_utf8(g_text_open->text);
        caret_at = g_text_open->caret;
    }
    if (size > 0u) {
        std::size_t take = std::min<std::size_t>(text.size(), size - 1u);
        while (take > 0 && take < text.size() && (static_cast<unsigned char>(text[take]) & 0xC0u) == 0x80u) {
            take--;
        }
        std::memcpy(buffer, text.data(), take);
        buffer[take] = '\0';
    }
    if (length != nullptr) {
        *length = static_cast<std::uint32_t>(text.size());
    }
    if (caret != nullptr) {
        *caret = static_cast<std::uint32_t>(caret_at);
    }
    return LUMA_OK;
}

std::int32_t input_text_cancel(const LumaMod* mod, LumaHandle request) {
    const char* const where = "input.text_cancel";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    const std::lock_guard<std::mutex> lock(g_text_mutex);
    if (g_text_open == nullptr || g_text_open->mod != mod || g_text_open->handle != request) {
        return bad_handle(mod, where);
    }
    end_open_text_locked(LUMA_E_CANCELLED);
    return LUMA_OK;
}

std::int32_t input_rumble(const LumaMod* mod, std::uint32_t player, float strength, float seconds) {
    const char* const where = "input.rumble";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (player >= LUMA_INPUT_PLAYERS) {
        return argument(mod, where, "player");
    }
    if (!(strength >= 0.0f && strength <= 1.0f)) {
        return argument(mod, where, "strength");
    }
    if (!(seconds >= 0.0f && seconds <= kRumbleMostSeconds)) {
        return argument(mod, where, "seconds");
    }
    if (!g_players[player].motor) {
        return LUMA_E_ABSENT;  // an answer: no motor on that device, or none the host drives
    }
    ModRecord* record = record_of(mod, true);
    record->rumble[player] = strength;
    record->rumble_until[player] = g_players[player].reads + static_cast<std::uint64_t>(std::ceil(seconds * kReadsPerSecond));
    g_active.store(true, std::memory_order_release);
    return LUMA_OK;
}

// ---- input 1.2: what aims the pointer --------------------------------------------------------------------------------

// The mod's choice for the main player (game thread writes; the live controls read at each sample) and what the
// live controls saw could aim it.
std::atomic<std::uint32_t> g_pointer_source{0};
std::atomic<float> g_pointer_gyro_scale{1.0f};
std::atomic<std::uint32_t> g_pointer_sources{0};
const LumaMod* g_pointer_mod = nullptr;  // game thread: the mod whose choice holds

std::int32_t input_pointer_source(const LumaMod* mod, LumaInputPad pad, LumaInputPointerSource kind, float sensitivity) {
    const char* const where = "input.pointer_source";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (static_cast<std::uint32_t>(pad) >= LUMA_INPUT_PLAYERS) {
        return argument(mod, where, "pad");
    }
    using galaxy::input::PointerSource;
    PointerSource source = PointerSource::None;
    switch (kind) {
    case LUMA_INPUT_POINTER_SOURCE_PLAYER: source = PointerSource::None; break;
    case LUMA_INPUT_POINTER_SOURCE_INFRARED: source = PointerSource::None; break;  // the Wii Remote aims itself
    case LUMA_INPUT_POINTER_SOURCE_GYRO: source = PointerSource::Gyro; break;
    case LUMA_INPUT_POINTER_SOURCE_MOUSE: source = PointerSource::Mouse; break;
    case LUMA_INPUT_POINTER_SOURCE_STICK: source = PointerSource::Stick; break;
    default: return argument(mod, where, "kind");
    }
    if (kind == LUMA_INPUT_POINTER_SOURCE_GYRO && !(sensitivity > 0.0f && sensitivity <= 8.0f)) {
        return argument(mod, where, "sensitivity");
    }
    if (pad != LUMA_INPUT_PAD_MAIN && kind != LUMA_INPUT_POINTER_SOURCE_PLAYER) {
        return LUMA_E_ABSENT;  // an answer: the co-star's pointer follows its own controller
    }
    const std::uint32_t seen = g_pointer_sources.load(std::memory_order_acquire);
    const bool has = kind == LUMA_INPUT_POINTER_SOURCE_PLAYER ||
                     (kind == LUMA_INPUT_POINTER_SOURCE_INFRARED ? g_players[0].device == LUMA_INPUT_DEVICE_MOTION
                                                                : (seen & (1u << static_cast<unsigned>(source))) != 0u);
    if (!has) {
        return LUMA_E_ABSENT;  // an answer: the player has no such device now
    }
    g_pointer_gyro_scale.store(kind == LUMA_INPUT_POINTER_SOURCE_GYRO ? sensitivity : 1.0f, std::memory_order_relaxed);
    g_pointer_source.store(static_cast<std::uint32_t>(source), std::memory_order_release);
    g_pointer_mod = source == PointerSource::None ? nullptr : mod;
    return LUMA_OK;
}

// ---- input.inject 1.0 ------------------------------------------------------------------------------------------------

Injection* injection_of(const LumaMod* mod, LumaHandle handle) {
    for (Injection& injection : g_injections) {
        if (injection.mod == mod && injection.handle == handle && handle != 0u) {
            return &injection;
        }
    }
    return nullptr;
}

std::int32_t inject_begin(const LumaMod* mod, std::uint32_t player, LumaHandle* out) {
    const char* const where = "input.inject.begin";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (player >= LUMA_INPUT_PLAYERS) {
        return argument(mod, where, "player");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    *out = 0u;
    Injection& injection = g_injections[player];
    if (injection.mod != nullptr) {
        return LUMA_E_BUSY;  // an answer: a mod injects that player already
    }
    injection.mod = mod;
    injection.handle = new_handle();
    injection.set = false;
    injection.state = LumaInputGame{};
    *out = injection.handle;
    g_active.store(true, std::memory_order_release);
    return LUMA_OK;
}

// A stick or pointer value as the game takes it: a value not a number reads 0, one past 1 reads 1. False: it was changed.
bool held_in(float& value) {
    if (std::isnan(value)) {
        value = 0.0f;
        return false;
    }
    if (value > 1.0f || value < -1.0f) {
        value = value > 0.0f ? 1.0f : -1.0f;
        return false;
    }
    return true;
}

std::int32_t inject_set(const LumaMod* mod, LumaHandle handle, const LumaInputGame* state) {
    const char* const where = "input.inject.set";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    Injection* injection = injection_of(mod, handle);
    if (injection == nullptr) {
        return bad_handle(mod, where);
    }
    if (state == nullptr) {
        return argument(mod, where, "state");
    }
    if (state->size < kGameLeast) {
        return struct_size(mod, where, "LumaInputGame", state->size, kGameLeast);
    }
    LumaInputGame taken = *state;
    taken.size = kGameLeast;
    taken.buttons &= LUMA_INPUT_BUTTONS_ALL;
    taken.pointer_on = taken.pointer_on != 0u ? 1u : 0u;
    taken.shake = taken.shake != 0u ? 1u : 0u;
    bool whole = held_in(taken.stick_x);
    whole = held_in(taken.stick_y) && whole;
    whole = held_in(taken.pointer_x) && whole;
    whole = held_in(taken.pointer_y) && whole;
    if (!whole) {
        report(mod, where, 101u, "state");  // noted; the values as held go in
    }
    injection->state = taken;
    injection->set = true;
    return LUMA_OK;
}

std::int32_t inject_end(const LumaMod* mod, LumaHandle handle) {
    const char* const where = "input.inject.end";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    Injection* injection = injection_of(mod, handle);
    if (injection == nullptr) {
        return bad_handle(mod, where);
    }
    *injection = Injection{};
    g_active.store(any_asked() || g_text_is_open.load(std::memory_order_acquire), std::memory_order_release);
    return LUMA_OK;
}

// ---- input.dev 1.0 ---------------------------------------------------------------------------------------------------

std::int32_t dev_list(const LumaMod* mod, LumaInputDeviceInfo* out, std::uint32_t capacity, std::uint32_t* found) {
    const char* const where = "input.dev.list";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (found == nullptr) {
        return argument(mod, where, "found");
    }
    *found = 0;
    if (out == nullptr && capacity > 0u) {
        return argument(mod, where, "out");
    }
    for (std::uint32_t i = 0; i < capacity; ++i) {
        if (out[i].size < kDeviceInfoLeast) {
            return struct_size(mod, where, "LumaInputDeviceInfo", out[i].size, kDeviceInfoLeast);
        }
    }
#if defined(_WIN32)
    std::uint32_t count = 0;
    each_hid([&](const std::wstring& path) {
        const std::optional<HidFacts> facts = hid_facts(path);
        if (!facts || facts->keyboard_or_mouse) {
            return true;
        }
        if (count < capacity) {
            const std::uint32_t size = out[count].size;
            std::memcpy(&out[count], &facts->info, kDeviceInfoLeast);
            out[count].size = size;
        }
        count++;
        return count < LUMA_INPUT_DEVICES_MOST;
    });
    *found = count;
    return LUMA_OK;
#else
    return LUMA_E_ABSENT;  // an answer: raw devices on this platform are a later layer (the contract)
#endif
}

OpenDevice* device_of(const LumaMod* mod, LumaHandle handle) {
    for (OpenDevice* device : g_devices) {
        if (device->mod == mod && device->handle == handle && handle != 0u) {
            return device;
        }
    }
    return nullptr;
}

std::int32_t dev_open(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out) {
    const char* const where = "input.dev.open";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (path == nullptr || path[0] == '\0') {
        return argument(mod, where, "path");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    *out = 0u;
#if defined(_WIN32)
    std::uint32_t open_count = 0;
    for (const OpenDevice* device : g_devices) {
        open_count += device->mod == mod ? 1u : 0u;
    }
    if (open_count >= LUMA_INPUT_DEV_OPEN_MOST) {
        char most[16];
        std::snprintf(most, sizeof(most), "%u", LUMA_INPUT_DEV_OPEN_MOST);
        report(mod, where, 104u, "devices", most);  // Service.Limit
        return LUMA_E_LIMIT;
    }
    // Only a listed device: a path that names no HID interface present (a file, a pipe) is not one.
    const std::wstring wide = wide_of(path);
    bool listed = false;
    std::size_t report_bytes = 0;
    if (!wide.empty()) {
        each_hid([&](const std::wstring& present) {
            if (_wcsicmp(present.c_str(), wide.c_str()) != 0) {
                return true;
            }
            const std::optional<HidFacts> facts = hid_facts(present);
            listed = facts && !facts->keyboard_or_mouse;
            report_bytes = facts ? facts->info.input_bytes : 0u;
            return false;
        });
    }
    if (!listed) {
        return LUMA_E_NOT_FOUND;
    }
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_DEVICE_NOT_CONNECTED) {
            return LUMA_E_NOT_FOUND;
        }
        return LUMA_E_BUSY;  // held by another program, or not ours to open
    }
    auto* device = new OpenDevice();
    device->node.mod = mod;
    device->node.deliver = deliver_reports;
    device->node.drop = drop_reports;
    device->mod = mod;
    device->handle = new_handle();
    device->fn = fn;
    device->user = user;
    device->file = file;
    device->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (device->stop == nullptr) {
        CloseHandle(file);
        delete device;
        return LUMA_E_MEMORY;
    }
    device->reader = std::thread(read_reports, device, report_bytes);
    g_devices.push_back(device);
    *out = device->handle;
    return LUMA_OK;
#else
    (void)user;
    return LUMA_E_ABSENT;
#endif
}

std::int32_t dev_write(const LumaMod* mod, LumaHandle handle, const void* data, std::uint32_t size) {
    const char* const where = "input.dev.write";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    OpenDevice* device = device_of(mod, handle);
    if (device == nullptr) {
        return bad_handle(mod, where);
    }
    if (data == nullptr || size == 0u || size > LUMA_INPUT_REPORT_MOST) {
        return argument(mod, where, "data");
    }
#if defined(_WIN32)
    // A short write with its own event: the device answers in milliseconds, and a refused one fails at once.
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        return LUMA_E_MEMORY;
    }
    DWORD wrote = 0;
    BOOL done = WriteFile(device->file, data, size, &wrote, &overlapped);
    if (!done && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(overlapped.hEvent, 50) != WAIT_OBJECT_0) {
            CancelIoEx(device->file, &overlapped);
        }
        done = GetOverlappedResult(device->file, &overlapped, &wrote, TRUE);
    }
    CloseHandle(overlapped.hEvent);
    return done && wrote == size ? LUMA_OK : LUMA_E_IO;
#else
    return LUMA_E_IO;
#endif
}

std::int32_t dev_close(const LumaMod* mod, LumaHandle handle) {
    const char* const where = "input.dev.close";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    OpenDevice* device = device_of(mod, handle);
    if (device == nullptr) {
        return bad_handle(mod, where);
    }
    close_device(device);
    return LUMA_OK;
}

// ---- The providers ----------------------------------------------------------------------------------------------------

void registered(void*, const LumaServiceKernel* k) {
    g_kernel.store(k, std::memory_order_release);
}

// Game thread, before each mod's begin: the first declares the session's actions (the start has recorded its mods).
void mod_begin(void*, const LumaMod*) {
    declare();
    g_mods.store(true, std::memory_order_release);
}

// Game thread, after the mod's stop: its watch, rumble, injections, text request and devices end (the inject and dev
// providers have no hooks of their own: this one, which every mod with either also has, ends them all).
void mod_gone(void*, const LumaMod* mod) {
    if (g_pointer_mod == mod) {
        // Its choice of the pointer's source ends with it: the player's controls choose again.
        g_pointer_source.store(0u, std::memory_order_release);
        g_pointer_mod = nullptr;
    }
    for (Injection& injection : g_injections) {
        if (injection.mod == mod) {
            injection = Injection{};
        }
    }
    if (ModRecord* record = record_of(mod, false)) {
        record->watch = nullptr;
        record->watch_user = nullptr;
        record->changed = 0;
        for (std::uint32_t player = 0; player < LUMA_INPUT_PLAYERS; ++player) {
            record->rumble[player] = 0.0f;
        }
    }
    {
        const std::lock_guard<std::mutex> lock(g_text_mutex);
        if (g_text_open != nullptr && g_text_open->mod == mod) {
            // Ended with nobody to tell: the kernel drops a node of a gone mod.
            end_open_text_locked(LUMA_E_CANCELLED);
        }
    }
    std::vector<OpenDevice*> own;
    for (OpenDevice* device : g_devices) {
        if (device->mod == mod) {
            own.push_back(device);
        }
    }
    for (OpenDevice* device : own) {
        close_device(device);
    }
    g_active.store(any_asked() || g_text_is_open.load(std::memory_order_acquire), std::memory_order_release);
}

void mod_end(void*, const LumaMod* mod) {
    ModRecord* record = record_of(mod, false);
    if (record == nullptr) {
        return;
    }
    g_records.erase(std::remove(g_records.begin(), g_records.end(), record), g_records.end());
    if (record->node.state != 0u) {
        record->orphan = true;  // drop frees it
        return;
    }
    delete record;
}

const LumaInputService kInput = {
    {sizeof(LumaInputService), LUMA_INPUT_MAJOR, LUMA_INPUT_MINOR},
    input_action,
    input_read,
    input_game,
    input_game_raw,
    input_device,
    input_watch_device,
    input_text_request,
    input_text_peek,
    input_text_cancel,
    input_rumble,
    input_pointer_source,
};

const LumaInputInjectService kInject = {
    {sizeof(LumaInputInjectService), LUMA_INPUT_INJECT_MAJOR, LUMA_INPUT_INJECT_MINOR},
    inject_begin,
    inject_set,
    inject_end,
};

const LumaInputDevService kDev = {
    {sizeof(LumaInputDevService), LUMA_INPUT_DEV_MAJOR, LUMA_INPUT_DEV_MINOR},
    dev_list,
    dev_open,
    dev_write,
    dev_close,
};

// `input` keeps the records; a mod importing only input.inject or input.dev still has them ended through these.
const LumaServiceProvider kInputProvider = {
    sizeof(LumaServiceProvider), LUMA_INPUT_NAME, &kInput.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider kInjectProvider = {
    sizeof(LumaServiceProvider), LUMA_INPUT_INJECT_NAME, &kInject.base, nullptr, nullptr, 0u, nullptr,
    nullptr, mod_gone, nullptr, registered,
};
const LumaServiceProvider kDevProvider = {
    sizeof(LumaServiceProvider), LUMA_INPUT_DEV_NAME, &kDev.base, nullptr, nullptr, 0u, nullptr,
    nullptr, mod_gone, nullptr, registered,
};
const LumaServiceProvider* const kProviders[] = {&kInputProvider, &kInjectProvider, &kDevProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept {
    g_log = log;
    return kProviders;
}

void game_read(std::uint32_t player, NebulaInputSample* sample, std::uint32_t device, bool motor) noexcept {
    if (!g_mods.load(std::memory_order_acquire) || player >= LUMA_INPUT_PLAYERS || sample == nullptr) {
        return;  // no mod: nothing asks
    }
    PlayerRead& read = g_players[player];
    const std::uint32_t before = read.device;
    read.read = true;
    read.device = device;
    read.motor = motor;
    read.reads++;
    if (!g_active.load(std::memory_order_acquire)) {
        read.raw = read.game = game_of(*sample, device, false);
        return;
    }
    read.raw = game_of(*sample, device, false);
    Injection& injection = g_injections[player];
    if (g_text_is_open.load(std::memory_order_acquire)) {
        rest(*sample);  // the keyboard is the player's: the game holds still
    } else if (injection.mod != nullptr) {
        rest(*sample);
        if (injection.set) {
            const LumaInputGame& state = injection.state;
            sample->buttons = state.buttons;
            sample->stick[0] = state.stick_x;
            sample->stick[1] = state.stick_y;
            sample->pointer_on = state.pointer_on;
            sample->pointer[0] = state.pointer_x;
            sample->pointer[1] = state.pointer_y;
            sample->spin_presses = state.shake;
        }
        injection.set = false;  // the next read is at rest unless the mod sets it again
    }
    read.game = game_of(*sample, device, injection.mod != nullptr);
    // A change of device is one after an earlier read: the session's first read names the device, it changes nothing.
    if (device != before && read.reads > 1u) {
        for (ModRecord* record : g_records) {
            if (record->watch == nullptr) {
                continue;
            }
            record->changed |= 1u << player;
            (void)kernel()->post(&record->node);
        }
    }
}

PointerChoice pointer_choice() noexcept {
    PointerChoice choice;
    choice.source = static_cast<std::uint8_t>(g_pointer_source.load(std::memory_order_acquire));
    choice.gyro_scale = g_pointer_gyro_scale.load(std::memory_order_relaxed);
    return choice;
}

void set_pointer_sources(std::uint32_t bits) noexcept { g_pointer_sources.store(bits, std::memory_order_release); }

std::uint8_t mod_rumble(std::uint32_t player) noexcept {
    if (player >= LUMA_INPUT_PLAYERS || !g_active.load(std::memory_order_acquire)) {
        return 0u;
    }
    float strongest = 0.0f;
    const std::uint64_t now = g_players[player].reads;
    for (ModRecord* record : g_records) {
        if (record->rumble[player] <= 0.0f) {
            continue;
        }
        if (now >= record->rumble_until[player]) {
            record->rumble[player] = 0.0f;
            continue;
        }
        strongest = std::max(strongest, record->rumble[player]);
    }
    return static_cast<std::uint8_t>(std::lround(strongest * 255.0f));
}

void set_text_entry_available(bool available) noexcept {
    g_text_available.store(available, std::memory_order_release);
}

bool text_open() noexcept {
    return g_text_is_open.load(std::memory_order_acquire);
}

void text_char(char32_t point) noexcept {
    const std::lock_guard<std::mutex> lock(g_text_mutex);
    TextRequest* request = g_text_open;
    if (request == nullptr || request->text.size() >= request->most || !text_takes(*request, point)) {
        return;
    }
    request->text.insert(request->text.begin() + static_cast<std::ptrdiff_t>(request->caret), point);
    request->caret++;
}

void text_utf8(const char* text) noexcept {
    if (text == nullptr) {
        return;
    }
    const std::optional<std::u32string> points = decode_utf8(text);
    if (!points) {
        return;
    }
    for (const char32_t point : *points) {
        text_char(point);
    }
}

void text_key(TextKey key) noexcept {
    const std::lock_guard<std::mutex> lock(g_text_mutex);
    TextRequest* request = g_text_open;
    if (request == nullptr) {
        return;
    }
    switch (key) {
    case TextKey::Enter:
        end_open_text_locked(LUMA_OK);
        break;
    case TextKey::Escape:
        end_open_text_locked(LUMA_E_CANCELLED);
        break;
    case TextKey::Backspace:
        if (request->caret > 0u) {
            request->text.erase(request->caret - 1u, 1u);
            request->caret--;
        }
        break;
    case TextKey::Delete:
        if (request->caret < request->text.size()) {
            request->text.erase(request->caret, 1u);
        }
        break;
    case TextKey::Left:
        request->caret = request->caret > 0u ? request->caret - 1u : 0u;
        break;
    case TextKey::Right:
        request->caret = std::min(request->caret + 1u, request->text.size());
        break;
    case TextKey::Home:
        request->caret = 0u;
        break;
    case TextKey::End:
        request->caret = request->text.size();
        break;
    }
}

void text_focus_lost() noexcept {
    const std::lock_guard<std::mutex> lock(g_text_mutex);
    end_open_text_locked(LUMA_E_CANCELLED);
}

}  // namespace galaxy::luma_input
