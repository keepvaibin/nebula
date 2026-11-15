// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The renderer check (Windows): which of Direct3D 12, Vulkan and Direct3D 11 each graphics card runs, by the rules the
// renderers apply (renderer_choice.h). It runs in a process of its own, this program started with
// kRendererCheckSwitch, so a driver that stops or fails never touches the game.

#include "galaxy/render/renderer_choice.h"

#include <filesystem>
#include <vector>

namespace galaxy::render {

inline constexpr const char* kRendererCheckSwitch = "--check-renderers";

// The check process's whole work: every hardware card in Windows' order for performance, written to its standard
// output as format_renderer_check's text. Its exit code.
int run_renderer_check();

// The cards the check found, from a check process started on a worker thread the first time this is asked;
// `done` false until it ended. A check that failed or ran past its time leaves no cards.
struct RendererCheck {
    bool done = false;
    std::vector<CardRenderers> cards;
};
[[nodiscard]] RendererCheck renderer_check();
// A check process waited for, at most `limit_ms`: a start whose renderer found no card. No cards when it failed.
[[nodiscard]] std::vector<CardRenderers> check_renderers_now(unsigned limit_ms);

// The program of a renderer in this program's folder, of this program's CPU level (Nebula.exe, NebulaVk.exe and
// NebulaD11.exe, or the -x86-64-v2 builds beside a -x86-64-v2 program); empty when it is not there.
[[nodiscard]] std::filesystem::path renderer_program(RendererKind kind);

}  // namespace galaxy::render
