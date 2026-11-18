// The settings menu's view, as the game thread asks for it each frame while the menu is open: every page shown, its rows'
// text the same on every ask, and the time an ask takes per page (P-088: the view made its page twice and the CONTROLS
// rows about twice per row). Each page's rows are printed as one hash, so two builds' outputs show whether they made the
// same rows.
#include "galaxy/render_api.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {
std::uint64_t hash_rows(const galaxy::render::NativeSettingsView& view) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const std::string& text) {
        for (const unsigned char c : text) {
            h = (h ^ c) * 1099511628211ull;
        }
        h = (h ^ 0xFFu) * 1099511628211ull;
    };
    for (const galaxy::render::NativeSettingsViewRow& row : view.rows) {
        mix(row.label);
        mix(row.value);
        h = (h ^ (row.heading ? 1u : 0u) ^ (row.changes ? 2u : 0u)) * 1099511628211ull;
    }
    return h;
}
}  // namespace

int main() {
    using galaxy::render::NativeSettingsInput;
    galaxy::render::native_settings_input(NativeSettingsInput::Open, 0);
    const galaxy::render::NativeSettingsView first = galaxy::render::native_settings_view();
    if (!first.open || first.pages.empty()) {
        std::fprintf(stderr, "native_settings_view: the menu did not open\n");
        return 1;
    }

    bool ok = true;
    constexpr int kAsks = 300;
    for (unsigned page = 0; page < first.pages.size(); page++) {
        galaxy::render::native_settings_input(NativeSettingsInput::SelectPage, static_cast<int>(page));
        const galaxy::render::NativeSettingsView view = galaxy::render::native_settings_view();
        if (view.page != page || view.rows.empty()) {
            std::fprintf(stderr, "native_settings_view: page %u shows %zu rows as page %u\n", page, view.rows.size(), view.page);
            ok = false;
            continue;
        }
        const std::uint64_t rows = hash_rows(view);
        double best = 1e30;
        for (int round = 0; round < 5; round++) {
            const auto start = std::chrono::steady_clock::now();
            for (int ask = 0; ask < kAsks; ask++) {
                const galaxy::render::NativeSettingsView again = galaxy::render::native_settings_view();
                if (hash_rows(again) != rows) {
                    std::fprintf(stderr, "native_settings_view: page %u's rows changed between asks\n", page);
                    ok = false;
                }
            }
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kAsks;
            best = us < best ? us : best;
        }
        std::printf("page %u %-12s %3zu rows, rows %016llx, %.1f us an ask\n", page, view.pages[page].c_str(), view.rows.size(),
                    static_cast<unsigned long long>(rows), best);
    }
    galaxy::render::native_settings_input(NativeSettingsInput::CloseUnseen, 0);
    std::printf("native_settings_view: %s\n", ok ? "every case holds" : "FAILED");
    return ok ? 0 : 1;
}
