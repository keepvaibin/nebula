// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Direct3D 11: game_bloom.h's passes (the D3D12 model, game_bloom.cpp) from the same shaders
// compiled for shader model 5.0. Owned by the effects lane: until its implementation lands, ready() is false and the
// renderer leaves the bloom to the game's own chain.
//
// Every call is on the render thread with the immediate context; no scene pass is open (RendererD3D11 flushed and
// unbound its draws). A call leaves its own state bound: the renderer binds its own again (Encoder::reset).
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"

#include <cstdint>
#include <memory>

namespace galaxy::render::d3d11 {

class GameBloom {
public:
    GameBloom() = default;
    GameBloom(const GameBloom&) = delete;
    GameBloom& operator=(const GameBloom&) = delete;
    ~GameBloom() { shutdown(); }

    // The compute and composite shaders; false leaves the bloom to the game's own chain.
    bool initialize(ID3D11Device1* device) {
        (void)device;
        return false;
    }
    void shutdown() noexcept {}
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    enum class Kind : std::uint8_t { Normal = 1, Simple = 2 };
    struct Params {
        Kind kind = Kind::Normal;
        unsigned threshold = 128;  // the selection's 8-bit threshold
        unsigned channel = 0;      // what passes it: 0 intensity, 1 red, 2 green, 3 blue
        unsigned tap1 = 42;        // normal: level 1's tap weight (/255); simple: its ring's
        unsigned tap2 = 21;        // normal: level 2's
        unsigned intensity = 255;  // the bloom's weight (/255; the effect's fade included)
        float ring_x = 1.0f;       // a ring's horizontal and vertical radius per unit (frame width, height)
        float ring_y = 1.0f;
        float strength = 1.0f;     // the player's setting (1 the game's)
    };
    struct Inputs {
        Texture* scene_target = nullptr;        // the scene target's single-sample colour (MSAA resolved)
        ID3D11RenderTargetView* rtv = nullptr;  // the scene target's render target (multisampled with MSAA)
        unsigned samples = 1;
        bool hdr = false;                       // the colour is the wide scene target (kSceneVariantHdr)
        unsigned frame_width = 0;               // the frame in scene target pixels
        unsigned frame_height = 0;
        float scale = 1.0f;                     // scene target pixels per game pixel
    };
    // As game_bloom.h: the scene kept before the bloom's work over it; the trades that keep it or the simple bloom's
    // source without a copy (the scene target continues on the texture returned; null: the renderer makes one); the
    // game's own chain drawing its kept copy back; the bloom over the frame; once a frame, the idle images handed back.
    bool keep(ID3D11DeviceContext1* context, const Inputs& in) {
        (void)context, (void)in;
        return false;
    }
    [[nodiscard]] std::shared_ptr<Texture> take_kept_image(ID3D11DeviceContext1* context, const Inputs& in) {
        (void)context, (void)in;
        return {};
    }
    void keep_picture(std::shared_ptr<Texture> picture, const Inputs& in) { (void)picture, (void)in; }
    [[nodiscard]] bool can_take_source(const Inputs& in) {
        (void)in;
        return false;
    }
    [[nodiscard]] std::shared_ptr<Texture> take_source_image(ID3D11DeviceContext1* context, const Inputs& in) {
        (void)context, (void)in;
        return {};
    }
    void give_source(std::shared_ptr<Texture> picture, const Inputs& in) { (void)picture, (void)in; }
    bool restore(ID3D11DeviceContext1* context, const Inputs& in) {
        (void)context, (void)in;
        return false;
    }
    bool apply(ID3D11DeviceContext1* context, const Inputs& in, const Params& params) {
        (void)context, (void)in, (void)params;
        return false;
    }
    void trim_idle() {}

private:
    bool ready_ = false;
};

}  // namespace galaxy::render::d3d11
