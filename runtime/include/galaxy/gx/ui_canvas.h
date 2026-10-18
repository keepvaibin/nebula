// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What the settings overlay, the HOME Menu and the start hint draw with (host_window_win32.cpp); each renderer
// implements it. Coordinates are backbuffer pixels.

#include <array>
#include <cstdint>

namespace galaxy::gx {

class UiCanvas {
public:
    // Loads the panel's corner texture from the game's files on first use; a no-op afterwards.
    virtual void ensure_native_settings_corner_texture_loaded() = 0;
    // The corner texture over a quad, between the given UVs (mirrored for the other corners).
    virtual void draw_ui_textured_quad(int x, int y, int w, int h, float u0, float v0, float u1, float v1,
                                       const std::array<float, 4>& tint) = 0;
    virtual void draw_ui_solid_quad(int x, int y, int w, int h, const std::array<float, 4>& color) = 0;
    // A 5x7 glyph (bit row * 5 + column), `scale` pixels per cell.
    virtual void draw_ui_glyph(int x, int y, int scale, std::uint64_t bitmap,
                               const std::array<float, 4>& color) = 0;

protected:
    ~UiCanvas() = default;
};

}  // namespace galaxy::gx
