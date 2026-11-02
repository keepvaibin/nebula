#include "galaxy/ui/settings_system_page.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared System page must not include Windows.h
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
int recorded = 0;
void record() { ++recorded; }
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
galaxy::ui::SettingsRow& row(std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    const auto found = std::find_if(rows.begin(), rows.end(), [label](const auto& r) { return !r.heading && r.label == label; });
    if (found == rows.end()) throw std::runtime_error(label);
    return *found;
}
}

int main() {
    try {
        galaxy::render::RenderFeatureCaps caps;
        std::memcpy(caps.adapter_description.data(), "TEST GPU  ", 11);
        caps.dxr_raytracing_tier = 11;
        caps.dxr.settings_exposed = true;
        auto rows = galaxy::ui::system_page(caps, "POST: SMAA", record);
        check(row(rows, "GRAPHICS CARD").value == "TEST GPU", "adapter description trimmed");
        check(row(rows, "RAY TRACING HARDWARE").value == "TIER 1.1 READY", "feature tier and visibility");
        check(row(rows, "NVIDIA STREAMLINE").value == "HIDDEN", "unexposed feature");
        check(row(rows, "POST-PROCESS").value == "SMAA", "post-process label");
        galaxy::ui::RenderConfig config;
        row(rows, "DIAGNOSTICS").press(config, 0);
        check(recorded == 1, "recording action");
        std::cout << "System page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
