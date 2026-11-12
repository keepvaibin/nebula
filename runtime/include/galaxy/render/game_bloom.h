#pragma once

// The game's bloom on the D3D12 renderer, at the internal resolution.

#include "galaxy/windows_api.h"
#include "galaxy/render/gpu_use.h"
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <vector>

namespace galaxy::render {

class DescriptorRing;

// The game's two blooms (NEBULA_PASS_BLOOM_*) as one operator on the scene target (game_bloom.hlsl). The game
// draws them through scene target copies at fixed sizes: BloomEffect draws its glowing models over black, takes their colour
// where its intensity passes a threshold at a quarter of the frame, blurs it with two rings of 6 taps (level 1), blurs
// that at an eighth with three rings of 12 (level 2) and adds both to the scene it kept; BloomEffectSimple takes the
// scene's own colour past a threshold and blurs it with one ring of 8. At 456 lines each tap's bilinear
// footprint smooths the rings; scaled up they come apart into separate offset images, and copies kept at that
// size are stretched over the screen. Here the same chain runs on the internal resolution: the threshold over the
// game's own 4 x 4 pixel footprint, each ring replaced by the Gaussian of its spread (a ring of radius r spreads r^2 / 2
// along each axis, plus what the game's bilinear taps and resampling add at its resolution, less what this chain's own
// resampling adds), the game's tap weights, clamps and intensity. The scene keep is the scene target's own format, so the
// ray tracer's HDR scene passes through whole.
class GameBloom {
public:
    GameBloom() = default;
    GameBloom(const GameBloom&) = delete;
    GameBloom& operator=(const GameBloom&) = delete;
    ~GameBloom() { shutdown(); }

    // The root signature and compute pipelines (precompiled shaders); the composite's pipelines are made per sample
    // count and format at first use. False leaves the bloom to the game's own chain.
    bool initialize(ID3D12Device* device);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // game_bloom.hlsl's BloomConstants.
    struct Constants {
        std::uint32_t size[4];
        float sigma[4];
        float params[4];
        float uv[4];
    };

    enum class Kind : std::uint8_t { Normal = 1, Simple = 2 };
    struct Params {
        Kind kind = Kind::Normal;
        unsigned threshold = 128;     // the selection's 8-bit threshold
        unsigned channel = 0;         // what passes it: 0 intensity, 1 red, 2 green, 3 blue
        unsigned tap1 = 42;           // normal: level 1's tap weight (/255); simple: its ring's
        unsigned tap2 = 21;           // normal: level 2's
        unsigned intensity = 255;     // the bloom's weight (/255; the effect's fade included)
        float ring_x = 1.0f;          // a ring's horizontal and vertical radius per unit (frame width, height)
        float ring_y = 1.0f;
        float strength = 1.0f;        // the player's setting (1 the game's)
    };
    struct Inputs {
        ID3D12Resource* scene_target = nullptr;           // the scene target's single-sample colour (RENDER_TARGET; MSAA resolved)
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};       // the scene target's render target (multisampled with MSAA)
        unsigned samples = 1;
        bool hdr = false;                        // the colour is the HDR scene scene target
        unsigned frame_width = 0;                // the frame in scene target pixels
        unsigned frame_height = 0;
        float scale = 1.0f;                      // scene target pixels per game pixel
    };
    // BloomEffect::preDraw, and BloomEffectSimple::draw with the game's own chain: the scene before the bloom's work
    // over it.
    bool keep(ID3D12GraphicsCommandList* list, const Inputs& in, std::uint64_t frame_index, unsigned frames_in_flight);
    // Keeping without a copy: the scene target continues on the image this returns (null: it makes one), and its
    // picture is the kept scene (keep_picture). The composite writes the kept scene's alpha back with its colour.
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> take_kept_image(ID3D12GraphicsCommandList* list, const Inputs& in,
                                                                         std::uint64_t frame_index,
                                                                         unsigned frames_in_flight);
    void keep_picture(Microsoft::WRL::ComPtr<ID3D12Resource> picture, const Inputs& in);
    // The simple bloom's source without a copy: the scene target continues on the image this returns (null: it makes one),
    // and give_source takes the scene's picture as the source; apply() copies nothing then, and with nothing else to draw
    // draws the frame back from it. Only where the composite covers the whole target, which it then writes from the
    // source; can_take_source first (the composite is there to draw).
    [[nodiscard]] bool can_take_source(const Inputs& in);
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> take_source_image(ID3D12GraphicsCommandList* list, const Inputs& in,
                                                                           std::uint64_t frame_index,
                                                                           unsigned frames_in_flight);
    void give_source(Microsoft::WRL::ComPtr<ID3D12Resource> picture, const Inputs& in);
    // The game's own chain (the NATIVE setting) where it draws its kept copy back: the kept scene over the frame, as
    // the scene target held it (RGB; the game's alpha stays). Leaves this pass's root signature, pipeline and render target
    // bound.
    bool restore(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in, std::uint64_t frame_index,
                 unsigned frames_in_flight);
    // BloomEffect::postDraw (the kept scene and the glowing models now in the scene target), BloomEffectSimple::draw (the
    // scene): the bloom over the frame. Leaves this pass's root signature, pipeline and render target bound.
    bool apply(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in, const Params& params,
               std::uint64_t frame_index, unsigned frames_in_flight);
    // Once a frame: frees what the passes released, and hands back the images no pass has asked for in 600 frames (the
    // bloom switched off or a scene without one), at the frame's fence; the next bloom makes them again.
    void trim_idle(std::uint64_t frame_index, unsigned frames_in_flight);

private:
    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        unsigned width = 0;
        unsigned height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        gpu::Use use = gpu::Use::CopyDest;
        std::uint64_t used_frame = 0;   // the last frame a pass asked for it
    };
    struct Retired {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t release_after_frame = 0;
    };
    bool ensure_target(Target& target, DXGI_FORMAT format, unsigned width, unsigned height, bool unordered,
                       std::uint64_t frame_index, unsigned frames_in_flight, const wchar_t* name);
    void transition(ID3D12GraphicsCommandList* list, Target& target, gpu::Use use);
    bool copy_frame(ID3D12GraphicsCommandList* list, const Inputs& in, Target& target, const wchar_t* name,
                    std::uint64_t frame_index, unsigned frames_in_flight);
    void retire(std::uint64_t frame_index);
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE table(DescriptorRing& ring, const std::array<ID3D12Resource*, 3>& srvs,
                                                    const std::array<DXGI_FORMAT, 3>& formats,
                                                    const std::array<ID3D12Resource*, 3>& uavs);
    void draw_composite(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in,
                        ID3D12PipelineState* pipeline, const Target& scene, const Target& first, const Target& second,
                        Constants c);
    [[nodiscard]] ID3D12PipelineState* composite_pipeline(unsigned samples, bool hdr);

    ID3D12Device* device_ = nullptr;
    bool ready_ = false;
    bool kept_ = false;   // keep() ran since the last normal apply
    bool source_given_ = false;  // give_source ran since the last apply
    std::uint64_t frame_ = 0;  // the frame of the last pass call, what keep_picture() stamps the kept image with
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> down_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> box_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blur_;
    // 1, 2, 4, 8 samples; then HDR.
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 8> composite_{};
    Target keep_;         // the scene (the scene target's format, the frame's size)
    Target source_;       // the simple bloom's scene, its source (as keep_)
    Target work_;         // level 1's resolution: the frame in boxes
    Target box_tmp_;      // the footprint's horizontal box
    Target mask_;         // the selection
    std::array<Target, 3> blur_h_;  // level 1's Gaussians across
    Target level1_;
    Target level2_in_;    // level 1 at level 2's resolution
    std::array<Target, 3> blur2_h_;
    Target level2_;
    std::vector<Retired> retired_;
};

}  // namespace galaxy::render
