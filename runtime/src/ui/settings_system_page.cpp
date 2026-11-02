// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_system_page.h"
#include <cstdio>

namespace galaxy::ui {
namespace {
const char* exposed_text(const render::RenderFeatureStatus& status) {
    return status.settings_exposed ? "READY" : "HIDDEN";
}

}  // namespace
std::vector<SettingsRow> system_page(const render::RenderFeatureCaps& caps, std::string post, void (*record_diagnostics)()) {
    std::vector<SettingsRow> rows;
    {
        std::string name = mods_ui_text(caps.adapter_description.data(), 32u);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        rows.push_back({"GRAPHICS CARD", name, nullptr, "CHOSEN IN NEBULA SETTINGS. A CHANGE APPLIES AT THE NEXT START"});
    }
    char text[64]{};
    if (caps.dxr_raytracing_tier != 0u) {
        std::snprintf(text, sizeof(text), "TIER %u.%u %s", caps.dxr_raytracing_tier / 10u, caps.dxr_raytracing_tier % 10u,
                      exposed_text(caps.dxr));
    } else {
        std::snprintf(text, sizeof(text), "NONE");
    }
    rows.push_back({"RAY TRACING HARDWARE", text, nullptr, "DIRECTX RAYTRACING ON THIS GPU"});
    rows.push_back({"NVIDIA STREAMLINE", exposed_text(caps.streamline), nullptr, "LOADS DLSS FROM THE NVIDIA DOWNLOAD"});
    rows.push_back({"NVIDIA REFLEX", exposed_text(caps.reflex), nullptr, "LOW LATENCY WITH FRAME GENERATION"});
    rows.push_back({"DLSS", exposed_text(caps.dlss_super_resolution), nullptr, "UPSCALING ON NVIDIA RTX GPUS"});
    rows.push_back({"DLSS FRAME GENERATION", exposed_text(caps.dlss_frame_generation), nullptr,
                    "GENERATED FRAMES ON NVIDIA RTX 40 AND 50 GPUS"});
    if (const std::size_t colon = post.find(": "); colon != std::string::npos) post.erase(0, colon + 2u);
    rows.push_back({"POST-PROCESS", post, nullptr, "ANTI-ALIASING, FSR 1 AND SHARPENING"});
    rows.push_back(button_row("DIAGNOSTICS", {"RECORD"},
                              [record_diagnostics](RenderConfig&, unsigned) { if (record_diagnostics) record_diagnostics(); },
                              {}));
    return rows;
}


}  // namespace galaxy::ui
