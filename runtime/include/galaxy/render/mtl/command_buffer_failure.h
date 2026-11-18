// SPDX-License-Identifier: GPL-3.0-only
// A Metal command buffer that ended in an error, in words the render core's recovery reads (crash_report.cpp
// gpu_lost). Apart from the system's own process plain C++, so that the rule is tested on every platform.
#pragma once

#include <string>
#include <string_view>

namespace galaxy::render::mtl {

// Every error a command buffer ends in is a lost device, as Direct3D 12's and Vulkan's GPU faults are: the device is
// made again and the game goes on (twice in a row is borne: render_core.cpp bear_device_loss). Metal keeps a failed
// buffer's work undone (uploads included), so no frame after it can trust the device's contents. Before (P-008) only a
// removed device, revoked access and a timeout were: an internal error, a page fault, memory run out or work in the
// background on iOS (MTLCommandBufferErrorNotPermitted) ended the renderer.
// `in_metal_domain`: the error's domain is MTLCommandBufferErrorDomain; `code` its code; `words` its description.
[[nodiscard]] inline std::string command_buffer_failure_text(bool in_metal_domain, long code, std::string_view words) {
    std::string text = "device lost (Metal command buffer error ";
    text += in_metal_domain ? std::to_string(code) : "of another domain";
    text += "): ";
    text += words.empty() ? std::string_view("no description") : words;
    return text;
}

}  // namespace galaxy::render::mtl
