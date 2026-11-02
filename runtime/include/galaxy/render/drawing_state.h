#pragma once

// The drawing state the game's records set, as typed values: the material record's and the state ops' (requirement
// E-02: the renderer reads no register words), with what each change invalidates.

#include "galaxy/render/draw_types.h"
#include "galaxy/render/native_material.h"
#include "galaxy/render/native_shapes.h"
#include "galaxy/render/native_state.h"


namespace galaxy::render {

// One texture map: its texture (a record's address or a capture's key; the record or picture has the size) and
// sampling (with the edge LOD and bias clamp enables).
struct TextureMap {
    std::uint64_t texture = 0u;
    TextureSampler mode{};
    bool edge_lod = true;
    bool bias_clamp = false;

    [[nodiscard]] bool operator==(const TextureMap&) const = default;
};

// What a change invalidates (the backend's caches): pipeline state, texture bindings, vertex shader, matrix memory,
// vertex constants.
inline constexpr std::uint32_t kStateDirtyRenderState = 1u << 1;
inline constexpr std::uint32_t kStateDirtyTex = 1u << 2;
inline constexpr std::uint32_t kStateDirtyVertexShader = 1u << 3;
inline constexpr std::uint32_t kStateDirtyMatrices = 1u << 4;
inline constexpr std::uint32_t kStateDirtyVertexConstants = 1u << 7;

struct NativeDrawState {
    // Fixed-function state: the counts and cull of the generation mode, depth, blend (as the game set it; the
    // pixel engine's effective blend is effective_blend's) and the pixel format. The shader tests its own alpha,
    // combines its own colours and applies its own fog (native materials).
    DrawCounts gen{0u, 0u, 1u, CullMode::None, 0u};
    ZMode z{false, CompareFunc::Never, false};
    BlendMode blend{false, false, false, false, BlendFactor::Zero, BlendFactor::Zero, false, 0u, false, 0u};
    bool frame_alpha = false;  // the frame keeps alpha

    // Vertex lighting: the channel controls (colour 0, colour 1, alpha 0, alpha 1) and the material and ambient
    // colours (RGBA) of channels 0 and 1.
    ChannelControl channel[4]{};
    std::uint8_t material_color[2][4]{};
    std::uint8_t ambient_color[2][4]{};
    // Texture coordinate generation and each coordinate's post-transform matrix.
    TexGen texgen[8]{};
    PostTexGen post_texgen[8]{};
    // The projection's six terms and type (0 perspective), the viewport (left, top, width, height in frame pixels,
    // near and far depth 0-1).
    float projection[6]{};
    std::uint8_t projection_type = 0u;
    float viewport[6]{};
    // The current matrices as slots of the matrix arena (0-9 position, 10-19 texture, 20 identity): the position/normal
    // matrix and each texture coordinate's.
    std::uint8_t position_matrix = 0u;
    std::uint8_t texture_matrix[8]{};
    // Lines and points: width and size (pixels), their texture coordinate offsets (1/n texel; 0 none).
    struct { float line_width, point_size; std::uint8_t line_divisor, point_divisor; } line_point{};
    // Each coordinate's rasterizer scale (s, t: the scale minus one, bias, cylindrical wrap) and its line and point
    // offset enables.
    struct {
        std::uint16_t size_minus_one[2];
        bool bias[2], wrap[2], line_offset, point_offset;
    } coord[8]{};

    TextureMap texture[8]{};

    // The scissor box (the frame's pixels, corners inclusive).
    ScissorRect scissor{0, 0, -1, -1};

    // What changed since the last consume_dirty (kStateDirty*; all at the start).
    std::uint32_t dirty = ~0u;

    // A state op (galaxy/render/native_state.h).
    // A draw's blend, depth, culling and writes.
    void apply_pass(const shapes::PassState& pass);
    void apply(const state::Op& op);
    [[nodiscard]] std::uint32_t consume_dirty() noexcept {
        const std::uint32_t d = dirty;
        dirty = 0u;
        return d;
    }

private:
    void set_op(const state::Op& op);
public:
};

}  // namespace galaxy::render
