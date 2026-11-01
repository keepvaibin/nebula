#pragma once

// The drawing state the game's records set, as typed values: the material record's and the state ops' (requirement
// E-02: the renderer reads no register words), with what each change invalidates.

#include "galaxy/render/gx_bitfields.h"
#include "galaxy/render/native_material.h"
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
    GenMode gen{0u, 0u, 1u, CullMode::None, 0u, false};
    ZMode z{false, CompareFunc::Never, false};
    BlendMode blend{false, false, false, false, false, BlendFactor::Zero, BlendFactor::Zero, false, 0u, false, 0u};
    PeControl pe{0u, 0u, false};

    // Vertex lighting: the channel controls (colour 0, colour 1, alpha 0, alpha 1) and the material and ambient
    // colours (RGBA) of channels 0 and 1.
    XfChannelCtrl channel[4]{};
    std::uint8_t material_color[2][4]{};
    std::uint8_t ambient_color[2][4]{};
    // Texture coordinate generation and each coordinate's post-transform matrix.
    XfTexGen texgen[8]{};
    XfPostTexGen post_texgen[8]{};
    // The projection's six terms and type (0 perspective), the viewport (half width, half height, depth range,
    // centre x, centre y, far).
    float projection[6]{};
    std::uint8_t projection_type = 0u;
    float viewport[6]{};
    // The current matrices: the position/normal matrix row and each texture coordinate's.
    std::uint8_t position_matrix = 0u;
    std::uint8_t texture_matrix[8]{};
    // Lines and points: width and size (sixths of a pixel), their texture coordinate offsets.
    struct { std::uint8_t line_width, point_size, line_offset, point_offset; } line_point{};
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

    // A state op (galaxy/render/native_state.h), as the GX function it names sets the state.
    void apply(const state::Op& op);
    // A material record: the whole material, or (diff_flags) the parts J3D's change flags name.
    // `whole` false: the state already holds this program (the same one, nothing set in between); only the
    // values apply.
    void apply(const material::Program& program, const material::Values& values, bool whole = true);
    [[nodiscard]] std::uint32_t consume_dirty() noexcept {
        const std::uint32_t d = dirty;
        dirty = 0u;
        return d;
    }

private:
    void set_op(const state::Op& op);
    void set_material(const material::Program& program, const material::Values& values, bool apply_program);
public:
};

// The shaders' packed forms of the vertex state (their constants and keys).
[[nodiscard]] std::uint32_t rgba_word(const std::uint8_t* rgba);
[[nodiscard]] std::uint32_t matrix_index_a(const NativeDrawState& state);
[[nodiscard]] std::uint32_t matrix_index_b(const NativeDrawState& state);


// The frame shown (frame_show: its top left `width` x `height`, NebulaCaptureFlags) as the display copy takes it.
[[nodiscard]] EfbCopyParams frame_show_params(std::uint16_t width, std::uint16_t height, std::uint8_t flags);
// A capture (target_capture: key, rectangle left/top/width/height, NebulaTargetChannels, NebulaCaptureFlags) as the
// conversion takes it.
[[nodiscard]] EfbCopyParams capture_copy_params(std::uint64_t key, const std::uint16_t rect[4], std::uint8_t channels,
                                                std::uint8_t flags);


}  // namespace galaxy::render
