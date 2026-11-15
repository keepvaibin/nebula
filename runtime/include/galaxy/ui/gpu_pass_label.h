// SPDX-License-Identifier: GPL-3.0-only
// What the settings menu calls a renderer's GPU pass (the names gpu_pass_times and the benchmark's gpu_passes give):
// the feature a player turns on or off, so one feature's passes share its name.
#pragma once

#include <string_view>

namespace galaxy::ui {

// The pass's name in the menu's text, or nullptr for a name no renderer gives.
[[nodiscard]] constexpr const char* gpu_pass_label(std::string_view pass) noexcept {
    if (pass == "scene") return "SCENE";
    if (pass == "resolve") return "MSAA";
    if (pass == "frame read" || pass == "display copy") return "FRAME COPIES";
    if (pass == "bloom") return "BLOOM";
    if (pass == "scene aa") return "ANTI-ALIASING";
    if (pass == "scene ao" || pass.substr(0u, 3u) == "ao ") return "AMBIENT OCCLUSION";
    if (pass == "upscale") return "UPSCALING";
    if (pass == "ray tracing") return "RAY TRACING";
    if (pass == "present") return "SCREEN OUTPUT";
    if (pass == "camera") return "CAMERA EFFECTS";
    if (pass == "uploads") return "DATA UPLOADS";
    return nullptr;
}

}  // namespace galaxy::ui
