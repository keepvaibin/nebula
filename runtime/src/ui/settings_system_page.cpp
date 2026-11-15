// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_system_page.h"

#include "galaxy/ui/gpu_pass_label.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace galaxy::ui {
namespace {
const char* exposed_text(const render::RenderFeatureStatus& status) {
    return status.settings_exposed ? "READY" : "HIDDEN";
}

const char* renderer_text(render::RendererKind kind) {
    switch (kind) {
    case render::RendererKind::kD3D12: return "DIRECT3D 12";
    case render::RendererKind::kD3D11: return "DIRECT3D 11";
    case render::RendererKind::kVulkan: return "VULKAN";
    case render::RendererKind::kMetal: return "METAL";
    default: return "AUTO";
    }
}

// Why the card does not run a renderer; nothing where it does or it is not known.
const char* refusal_text(render::RendererRefusal refusal) {
    using render::RendererRefusal;
    switch (refusal) {
    case RendererRefusal::kNotInstalled: return "NOT INSTALLED";
    case RendererRefusal::kNoDriver: return "NO DRIVER IS INSTALLED";
    case RendererRefusal::kNoDevice: return "NOT SUPPORTED BY THE CARD'S DRIVER";
    case RendererRefusal::kFeatureLevel: return "NEEDS FEATURE LEVEL 11.0";
    case RendererRefusal::kBindingTier: return "NEEDS RESOURCE BINDING TIER 2";
    case RendererRefusal::kShaderModel: return "NEEDS SHADER MODEL 6.0";
    case RendererRefusal::kNoD3D11_1: return "NEEDS THE DIRECT3D 11.1 RUNTIME";
    case RendererRefusal::kConstantBufferOffsets: return "NEEDS CONSTANT BUFFER OFFSETS";
    case RendererRefusal::kVulkanVersion: return "NEEDS VULKAN 1.1";
    case RendererRefusal::kNoSwapchain:
    case RendererRefusal::kNoPresentQueue: return "CANNOT SHOW PICTURES IN A WINDOW";
    default: return nullptr;
    }
}

// The renderers the row offers: those the card runs, or that are not known not to.
std::vector<render::RendererKind> offered_renderers(const RendererPickerView& view) {
    std::vector<render::RendererKind> offered;
    for (const render::RendererKind kind : render::kRendererPreference) {
        if (refusal_text(view.card.refusal(kind)) == nullptr) offered.push_back(kind);
    }
    return offered;
}

// The RENDERER row (RendererPickerView): the renderers the card runs to choose from, those it does not named with
// their reason in the help.
void renderer_rows(std::vector<SettingsRow>& rows, const RendererPickerView& view) {
    if (!view.choose) {
        rows.push_back({"RENDERER", renderer_text(view.running), nullptr, {}});
        return;
    }
    if (!view.checked) {
        rows.push_back({"RENDERER", "CHECKING", nullptr, "CHANGES APPLY AT THE NEXT START"});
        return;
    }
    std::string help;
    for (const render::RendererKind kind : render::kRendererPreference) {
        if (const char* const reason = refusal_text(view.card.refusal(kind)); reason != nullptr) {
            help += std::string(renderer_text(kind)) + ": " + reason + ". ";
        }
    }
    help += "CHANGES APPLY AT THE NEXT START";
    const std::vector<render::RendererKind> offered = offered_renderers(view);
    const render::RendererKind shown = shown_renderer(view);
    if (offered.size() < 2u) {
        rows.push_back({"RENDERER", renderer_text(shown), nullptr, help});
        return;
    }
    const auto at = static_cast<unsigned>(std::find(offered.begin(), offered.end(), shown) - offered.begin());
    const std::function<void(render::RendererKind)> choose = view.choose;
    rows.push_back({"RENDERER", renderer_text(shown),
                    [offered, choose, at](RenderConfig&, int delta) {
                        const unsigned next = settings_step(at, static_cast<unsigned>(offered.size()), delta);
                        if (next != at) choose(offered[next]);
                    },
                    help});
}

}  // namespace

render::RendererKind shown_renderer(const RendererPickerView& view) {
    if (!view.choose || !view.checked) return view.running;
    const std::vector<render::RendererKind> offered = offered_renderers(view);
    if (offered.empty()) return view.running;
    return std::find(offered.begin(), offered.end(), view.next) != offered.end() ? view.next : offered.front();
}
// The BENCHMARK section (BenchmarkView).
void benchmark_rows(std::vector<SettingsRow>& rows, const BenchmarkView& view) {
    if (!view.can_run && !view.has_result) return;  // a host that cannot run it (no renderer of its own) has none
    rows.push_back({"BENCHMARK", {}, nullptr, {}, true});
    if (view.can_run) {
        const std::function<void()> run = view.run;
        rows.push_back(button_row("RUN BENCHMARK", {"RUN"}, [run](RenderConfig&, unsigned) { if (run) run(); },
                                  "MEASURES THIS DEVICE ON 5 STAGES, FROM THE TITLE SCREEN"));
    }
    if (!view.has_result) {
        rows.push_back({"LAST RESULT", "NONE", nullptr, "RUN THE BENCHMARK TO SEE HOW THIS DEVICE RUNS THE GAME"});
        return;
    }
    const std::function<void()> show = view.view;
    std::string seen = view.last_outcome;
    if (!view.last_when.empty()) seen = view.last_when + ": " + seen;
    rows.push_back(button_row("LAST RESULT", {"VIEW"}, [show](RenderConfig&, unsigned) { if (show) show(); }, seen));

    const std::function<void()> recommend = view.recommend;
    const bool offered = view.apply == BenchmarkApply::Offer;
    const char* const apply_help = view.apply == BenchmarkApply::Offer    ? "THE SETTINGS THE BENCHMARK RECOMMENDS, AS CHANGES TO APPLY"
                                   : view.apply == BenchmarkApply::InUse  ? "THE RECOMMENDED SETTINGS ARE IN USE"
                                   : view.apply == BenchmarkApply::OtherGpu ? "THE LAST RESULT IS FROM ANOTHER GRAPHICS CARD"
                                                                          : "THE LAST RUN DID NOT FINISH";
    rows.push_back(button_row("APPLY RECOMMENDED", {offered ? "APPLY" : "-APPLY"},
                              [recommend, offered](RenderConfig&, unsigned) { if (offered && recommend) recommend(); },
                              apply_help));

    const std::function<void()> send = view.send_report;
    switch (view.send) {
    case BenchmarkSend::Hidden: break;
    case BenchmarkSend::Offer:
        rows.push_back(button_row("SEND TO DEVELOPER", {"SEND"}, [send](RenderConfig&, unsigned) { if (send) send(); },
                                  "SENDS THE LAST RESULT ONCE, TO HELP NEBULA RUN WELL ON DEVICES LIKE THIS ONE"));
        break;
    case BenchmarkSend::Sending: rows.push_back({"SEND TO DEVELOPER", "SENDING", nullptr, {}}); break;
    case BenchmarkSend::Sent: rows.push_back({"SEND TO DEVELOPER", "SENT", nullptr, "THANK YOU"}); break;
    case BenchmarkSend::Failed:
        rows.push_back(button_row("SEND TO DEVELOPER", {"TRY AGAIN"}, [send](RenderConfig&, unsigned) { if (send) send(); },
                                  "THE REPORT COULD NOT BE SENT. IT IS KEPT ON THIS DEVICE"));
        break;
    }
}

std::vector<SettingsRow> system_page(const render::RenderFeatureCaps& caps, std::string post, void (*record_diagnostics)(),
                                     const GpuTimingView* timing, const GpuPickerView* gpus, const UpdatesView* updates,
                                     const BenchmarkView* benchmark, const RendererPickerView* renderers) {
    std::vector<SettingsRow> rows;
    {
        const auto shown_name = [](const std::string& text) {
            std::string name = mods_ui_text(text, 32u);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            return name;
        };
        const std::string in_use = shown_name(caps.adapter_description.data());
        if (gpus != nullptr && gpus->choices.size() >= 2u && gpus->choose) {
            // AUTO, then each GPU; the choice applies at the next start, so the GPU in use has its own row.
            unsigned at = 0u;
            for (std::size_t i = 0u; i < gpus->choices.size(); ++i) {
                if (!gpus->next.empty() && gpus->choices[i].identity == gpus->next) at = static_cast<unsigned>(i + 1u);
            }
            const std::vector<render::GpuChoice> choices = gpus->choices;
            const std::function<void(const std::string&)> choose = gpus->choose;
            SettingsRow row{"GRAPHICS CARD", at == 0u ? std::string("AUTO") : shown_name(choices[at - 1u].name),
                            [choices, choose, at](RenderConfig&, int delta) {
                                const unsigned next = settings_step(at, static_cast<unsigned>(choices.size() + 1u), delta);
                                if (next != at) choose(next == 0u ? std::string() : choices[next - 1u].identity);
                            },
                            "AUTO PREFERS A DEDICATED GPU. CHANGES APPLY AT THE NEXT START"};
            row.names = at == 0u ? 0u : 2u;
            rows.push_back(std::move(row));
            SettingsRow used{"IN USE", in_use, nullptr, {}};
            used.names = 2u;
            rows.push_back(std::move(used));
        } else {
            SettingsRow row{"GRAPHICS CARD", in_use, nullptr, {}};
            row.names = 2u;
            rows.push_back(std::move(row));
        }
    }
    if (renderers != nullptr) renderer_rows(rows, *renderers);
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
    if (record_diagnostics != nullptr) {
        rows.push_back(button_row("DIAGNOSTICS", {"RECORD"},
                                  [record_diagnostics](RenderConfig&, unsigned) { record_diagnostics(); }, {}));
    }
    if (benchmark != nullptr) benchmark_rows(rows, *benchmark);
    if (updates != nullptr) {
        rows.push_back({"UPDATES", {}, nullptr, {}, true});
        const std::function<void()> toggle = updates->toggle;
        rows.push_back({"CHECK AT START", updates->check_at_start ? "ON" : "OFF",
                        [toggle](RenderConfig&, int delta) { if (delta != 0 && toggle) toggle(); },
                        "CHECKS GITHUB FOR A NEW VERSION WHEN NEBULA STARTS"});
        const std::function<void()> check = updates->check;
        const std::function<void()> install = updates->install;
        const std::function<void()> restart = updates->restart;
        const auto check_row = [&rows, check](std::string help) {
            rows.push_back(button_row("CHECK FOR UPDATES", {"CHECK"},
                                      [check](RenderConfig&, unsigned) { if (check) check(); }, std::move(help)));
        };
        const auto version_row = [&rows, updates](std::string help) {
            SettingsRow row{"NEW VERSION", updates->version, nullptr, std::move(help)};
            row.names = 2u;
            rows.push_back(std::move(row));
        };
        switch (updates->step) {
        case UpdateStep::NotChecked: check_row("CHECKS GITHUB FOR A NEW VERSION NOW"); break;
        case UpdateStep::Checking: rows.push_back({"CHECK FOR UPDATES", "CHECKING", nullptr, "CHECKS GITHUB FOR A NEW VERSION NOW"}); break;
        case UpdateStep::UpToDate: check_row("NEBULA IS UP TO DATE"); break;
        case UpdateStep::CheckFailed: check_row("COULD NOT REACH GITHUB"); break;
        case UpdateStep::Available:
            if (!updates->can_install) {
                version_row("INSTALL IT THE SAME WAY YOU INSTALLED NEBULA");
                break;
            }
            version_row({});
            rows.push_back(button_row("UPDATE NEBULA", {"UPDATE"}, [install](RenderConfig&, unsigned) { if (install) install(); },
                                      updates->closes_game ? "CLOSES THE GAME AND INSTALLS THE NEW VERSION"
                                                           : "DOWNLOADS AND INSTALLS THE NEW VERSION"));
            break;
        case UpdateStep::Downloading: {
            version_row({});
            char value[16]{};
            std::snprintf(value, sizeof(value), "%u%%", updates->percent > 100u ? 100u : updates->percent);
            rows.push_back({"UPDATE NEBULA", value, nullptr, "DOWNLOADING"});
            break;
        }
        case UpdateStep::Installing:
            version_row({});
            rows.push_back({"UPDATE NEBULA", "INSTALLING", nullptr, {}});
            break;
        case UpdateStep::Restart:
            version_row({});
            rows.push_back(button_row("UPDATE NEBULA", {"RESTART"}, [restart](RenderConfig&, unsigned) { if (restart) restart(); },
                                      "THE NEW VERSION RUNS AFTER A RESTART"));
            break;
        case UpdateStep::InstallFailed:
            version_row({});
            rows.push_back(button_row("UPDATE NEBULA", {"TRY AGAIN"}, [install](RenderConfig&, unsigned) { if (install) install(); },
                                      "THE UPDATE COULD NOT BE INSTALLED"));
            break;
        }
    }
    if (timing != nullptr) {
        rows.push_back({"PERFORMANCE", {}, nullptr, {}, true});
        void (*const toggle)() = timing->toggle;
        rows.push_back({"GPU TIMING", timing->on ? "ON" : "OFF",
                        [toggle](RenderConfig&, int) { if (toggle) toggle(); },
                        "MILLISECONDS THE GPU SPENDS ON EACH PASS OF A FRAME. OFF AT EVERY START; WHILE OFF NOTHING IS MEASURED"});
        // Each feature's passes under its one name (gpu_pass_label), in the renderer's order.
        std::vector<std::pair<std::string, float>> features;
        float total = 0.0f;
        for (const auto& [name, ms] : timing->passes) {
            const char* const label = gpu_pass_label(name);
            std::string shown = label != nullptr ? std::string(label) : upper_ascii(name);
            const auto at = std::find_if(features.begin(), features.end(), [&](const auto& f) { return f.first == shown; });
            if (at == features.end()) {
                features.emplace_back(std::move(shown), ms);
            } else {
                at->second += ms;
            }
            total += ms;
        }
        for (const auto& [shown, ms] : features) {
            char value[24]{};
            std::snprintf(value, sizeof(value), "%.2f MS", static_cast<double>(ms));
            rows.push_back({shown, value, nullptr, "AVERAGE OVER THE LAST FRAMES THAT RAN THIS PASS"});
        }
        if (timing->on && !timing->passes.empty()) {
            char value[24]{};
            std::snprintf(value, sizeof(value), "%.2f MS", static_cast<double>(total));
            rows.push_back({"ALL PASSES", value, nullptr, "THE SUM OF THE PASSES ABOVE"});
        }
    }
    return rows;
}


}  // namespace galaxy::ui
