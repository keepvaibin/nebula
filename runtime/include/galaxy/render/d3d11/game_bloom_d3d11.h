// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Direct3D 11: game_bloom.h's passes (the D3D12 model, game_bloom.cpp) from the same shaders, which
// fxc already compiles for shader model 5.0 with plain registers (bloom_*.h: b0, t0-t2, u0-u2, s0).
//
// Every call is on the render thread with the immediate context; no scene pass is open (RendererD3D11 flushed and
// unbound its draws). A call leaves its shaders, constants, sampler and viewport bound (the renderer binds its own again:
// Encoder::reset) and unbinds every view it bound, outputs and inputs.
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"

#include <array>
#include <cstdint>
#include <memory>

namespace galaxy::render::d3d11 {

class GameBloom {
public:
    GameBloom() = default;
    GameBloom(const GameBloom&) = delete;
    GameBloom& operator=(const GameBloom&) = delete;
    ~GameBloom() { shutdown(); }

    // The compute and composite shaders and their states; false leaves the bloom to the game's own chain.
    bool initialize(ID3D11Device1* device);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // game_bloom.hlsl's BloomConstants (b0).
    struct Constants {
        std::uint32_t size[4];
        float sigma[4];
        float params[4];
        float uv[4];
    };

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
    // BloomEffect::preDraw, and BloomEffectSimple::draw with the game's own chain: the scene before the bloom's work over
    // it (a copy).
    bool keep(ID3D11DeviceContext1* context, const Inputs& in);
    // Keeping without a copy: the scene target continues on the texture this returns (null: the renderer makes one; a
    // texture returned is the scene target's size and format, sampled and drawn into), and its picture is the kept
    // scene (keep_picture). The composite writes the kept scene's alpha back with its colour.
    [[nodiscard]] std::shared_ptr<Texture> take_kept_image(ID3D11DeviceContext1* context, const Inputs& in);
    void keep_picture(std::shared_ptr<Texture> picture, const Inputs& in);
    // A normal bloom that adds nothing (bloom::adds_nothing) over a scene kept by a trade: the kept picture becomes the
    // scene target's colour again (take_kept_back, kept_by_trade first: same size and format as the scene target, a
    // render target, one sample), and the texture the glowing models were drawn on waits for the next trade (set_aside).
    // The renderer then draws nothing (renderer_d3d12.cpp game_bloom).
    [[nodiscard]] bool kept_by_trade(const Inputs& in) const;
    [[nodiscard]] std::shared_ptr<Texture> take_kept_back();
    void set_aside(std::shared_ptr<Texture> picture, const Inputs& in);
    // The simple bloom's source without a copy: the scene target continues on the texture this returns (null: the
    // renderer makes one), and give_source takes the scene's picture as the source; apply() copies nothing then, and with
    // nothing else to draw draws the frame back from it. can_take_source first (the composite is there to draw).
    [[nodiscard]] bool can_take_source(const Inputs& in);
    [[nodiscard]] std::shared_ptr<Texture> take_source_image(ID3D11DeviceContext1* context, const Inputs& in);
    void give_source(std::shared_ptr<Texture> picture, const Inputs& in);
    // The game's own chain (the NATIVE setting) where it draws its kept copy back: the kept scene over the frame, as the
    // scene target held it (RGB; the game's alpha stays).
    bool restore(ID3D11DeviceContext1* context, const Inputs& in);
    // BloomEffect::postDraw (the kept scene and the glowing models now in the scene target), BloomEffectSimple::draw (the
    // scene): the bloom over the frame. A bloom whose parameters add nothing makes no level: the normal one draws its kept
    // scene back, the simple one draws nothing (false) unless its source was given or the target is multisampled.
    bool apply(ID3D11DeviceContext1* context, const Inputs& in, const Params& params);
    // Once a frame: the textures no pass has asked for in 600 frames (the bloom switched off or a scene without one) are
    // dropped (Direct3D 11 keeps them alive while the GPU still reads them); the next bloom makes them again.
    void trim_idle();

private:
    struct Target {
        std::shared_ptr<Texture> texture;
        std::uint64_t used = 0;  // the frame clock at the last pass that asked for it
    };
    bool ensure_target(Target& target, DXGI_FORMAT format, unsigned width, unsigned height, std::uint32_t bind,
                       const char* name);
    bool copy_frame(ID3D11DeviceContext1* context, const Inputs& in, Target& target, const char* name);
    [[nodiscard]] std::shared_ptr<Texture> take_image(Target& target, const Inputs& in);
    void set_constants(ID3D11DeviceContext1* context, const Constants& c);
    void dispatch(ID3D11DeviceContext1* context, ID3D11ComputeShader* shader, const Constants& c,
                  const std::array<Texture*, 3>& srvs, const std::array<Texture*, 3>& uavs, unsigned width,
                  unsigned height);
    void draw_composite(ID3D11DeviceContext1* context, const Inputs& in, const Texture& scene, const Texture& first,
                        const Texture& second, const Constants& c);

    bool ready_ = false;
    bool kept_ = false;          // keep() or keep_picture() ran since the last normal apply
    bool source_given_ = false;  // give_source ran since the last apply
    std::uint64_t clock_ = 0;    // trim_idle() calls
    ComPtr<ID3D11ComputeShader> down_;
    ComPtr<ID3D11ComputeShader> box_;
    ComPtr<ID3D11ComputeShader> blur_;
    ComPtr<ID3D11VertexShader> composite_vs_;
    ComPtr<ID3D11PixelShader> composite_ps_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11SamplerState> linear_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> no_depth_;
    Target keep_;     // the scene (the scene target's format, the frame's size)
    Target source_;   // the simple bloom's scene, its source (as keep_)
    Target work_;     // level 1's resolution: the frame in boxes
    Target box_tmp_;  // the footprint's horizontal box
    Target mask_;     // the selection
    std::array<Target, 3> blur_h_;  // level 1's Gaussians across
    Target level1_;
    Target level2_in_;  // level 1 at level 2's resolution
    std::array<Target, 3> blur2_h_;
    Target level2_;
};

}  // namespace galaxy::render::d3d11
