#pragma once

// How the real Wii Remote's worker (wiimote_hid.cpp) reaches a remote on each platform: Windows HID for a remote
// paired with Windows or on a DolphinBar (wiimote_link_win32.cpp), Android's USB host for one on a DolphinBar
// (wiimote_link_android.cpp; Android gives apps no way into a Bluetooth remote's data channel). Reports cross with
// their report id first, as Windows HID and a USB HID interface both carry them.

#include <cstdint>
#include <functional>
#include <span>

namespace galaxy::host::wiimote_link {

// One remote, open.
class Link {
public:
    virtual ~Link() = default;
    // One output report, report id first. A write that only times out is not sent again (it may have reached the
    // remote): it fails, and the worker sets the remote up again.
    virtual bool send(std::span<const std::uint8_t> report) = 0;
    // The next input report, waiting up to `timeout_ms` (or, `wakeable`, until wake()): its length, 0 without one, -1
    // when the remote is gone.
    virtual int receive(std::uint32_t timeout_ms, bool wakeable) = 0;
    // The report receive() returned `size` for.
    [[nodiscard]] virtual std::span<const std::uint8_t> data(int size) const = 0;
};

// Device arrivals are watched from the first call on.
void start();
// Each remote there now, opened in turn: `use` gets its link and returns true when it answered as a remote, which
// ends the search; the link is closed after `use` returns. A Wii U Pro Controller (a remote's ids under its own name)
// is a controller, never offered.
void each_remote(const std::function<bool(Link&)>& use);
// Waits up to `timeout_ms` for a device to arrive: true when one did since the last call.
bool wait_arrival(std::uint32_t timeout_ms);
// Wakes a receive(..., true) now waiting, or the next one.
void wake();
// The calling thread serves a remote (higher priority) or looks for one.
void set_serving(bool serving);

}  // namespace galaxy::host::wiimote_link
