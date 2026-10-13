#include "galaxy/controller_roster.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace galaxy::input {

namespace {

const char* link_name(ControllerLink link) noexcept {
    switch (link) {
    case ControllerLink::Usb: return "USB";
    case ControllerLink::Bluetooth: return "Bluetooth";
    case ControllerLink::Wireless: return "wireless";
    default: return nullptr;
    }
}

// Appends formatted text while the label has room.
template <typename... Args>
void append(DeviceEntry& entry, int& used, const char* format, Args... args) noexcept {
    if (used < 0 || static_cast<std::size_t>(used) >= entry.label.size()) return;
    const int added = std::snprintf(entry.label.data() + used, entry.label.size() - static_cast<std::size_t>(used),
                                    format, args...);
    used = added < 0 ? added : used + added;
}

void write_label(DeviceEntry& entry, const char* model, unsigned ordinal, const char* link,
                 const char* note) noexcept {
    int used = 0;
    append(entry, used, "%s", model);
    if (ordinal != 0u) append(entry, used, " %u", ordinal);
    if (link != nullptr) append(entry, used, " (%s)", link);
    if (note != nullptr) append(entry, used, " - %s", note);
}

}  // namespace

void build_device_list(std::span<const ControllerInfo> connected, std::optional<DeviceKey> active,
                       DeviceList& out) noexcept {
    out = DeviceList{};
    DeviceEntry& keyboard = out.entries[out.count++];
    keyboard.key = DeviceKey{ControlScheme::Keyboard, kKeyboardSlot};
    write_label(keyboard, "Keyboard and mouse", 0u, nullptr, nullptr);
    keyboard.active = active && *active == keyboard.key;

    std::array<const ControllerInfo*, kMaxControllers * 2> sorted{};
    std::size_t sorted_count = 0;
    for (const ControllerInfo& info : connected) {
        if (info.scheme == ControlScheme::Keyboard || sorted_count == sorted.size()) continue;
        sorted[sorted_count++] = &info;
    }
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted_count),
              [](const ControllerInfo* a, const ControllerInfo* b) {
                  if (a->scheme != b->scheme) return a->scheme < b->scheme;
                  return a->slot < b->slot;
              });
    const std::size_t listed = std::min(sorted_count, kMaxControllers);
    for (std::size_t i = 0; i < listed; ++i) {
        const ControllerInfo& info = *sorted[i];
        unsigned same_model = 0;
        unsigned ordinal = 0;
        for (std::size_t j = 0; j < listed; ++j) {
            if (std::strcmp(sorted[j]->model, info.model) != 0) continue;
            ++same_model;
            if (j <= i) ordinal = same_model;
        }
        DeviceEntry& entry = out.entries[out.count++];
        entry.key = DeviceKey{info.scheme, info.slot};
        write_label(entry, info.model, same_model > 1u ? ordinal : 0u, link_name(info.link), info.note);
        entry.active = active && *active == entry.key;
    }
}

std::size_t resolve_device(const DeviceList& list, DeviceKey previous) noexcept {
    for (std::size_t i = 0; i < list.count; ++i) {
        if (list.entries[i].key == previous) return i;
    }
    for (std::size_t i = 0; i < list.count; ++i) {
        if (list.entries[i].key.scheme == previous.scheme) return i;
    }
    return 0;
}

}  // namespace galaxy::input
