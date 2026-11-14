// SPDX-License-Identifier: GPL-3.0-only
// The proof harness's host half (contract `harness`, revision 1): reads the plan the runner (tools/lumacore/luma_test.py)
// wrote, keeps the test clock, drives the controls and menu keys, keeps the pictures asked for, and writes facts.jsonl.
// It offers the game half (tools/test_driver/luma_test, a LumaCore mod) the service "harness" (luma_harness.h).
#pragma once

#include <nebula_host_extension.h>

#include <cstdint>
#include <string>

namespace harness {

// `plan`: the plan file (LUMA_HARNESS_PLAN); without start this is not a harness run and every call below does nothing.
// `log` writes a line to the host's log; `quit` ends the run (the sound file closed first). False and a logged reason
// when the plan cannot be read: the run then ends at once.
bool start(const NebulaHostForExtension* host, const std::string& plan, void (*log)(const std::string&), void (*quit)());
[[nodiscard]] bool on();

// Game thread, once a game frame, before its controls.
void frame(std::uint64_t game_frames);
// Game thread: the input the plan holds now.
void controls(std::uint64_t game_frames, NebulaExtensionControls* out);
// Render thread: the file for a picture the plan asks for at this present, or null.
const char* picture_path(std::uint64_t game_frame, std::uint32_t kind);
// Any thread: a line of the host's log ("<tag> <text>").
void log_line(const char* tag, const char* text);
// Game thread: the harness's service for mods, or null.
const void* service(const char* name, std::uint32_t major, std::uint32_t minor);
// After start: the plan's controls are a controller's (`device pad`).
[[nodiscard]] bool as_pad();
// Game thread: the controller's motor strength at a change (the fact input.motor).
void motor(std::uint8_t strength);
// Game thread: the setup is not done yet (frame 0 not fixed), so a menu the boot hid stays hidden: the setup's save and
// stage sequences run only while the scene goes, which an open menu holds.
[[nodiscard]] bool holds_menus();

}  // namespace harness
