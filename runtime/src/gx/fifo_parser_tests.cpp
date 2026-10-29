// Tests for FifoParser, GxState and the shader key builders.

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/motion_signals.h"
#include <stdexcept>

#include <array>
#include <chrono>
#include <functional>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

namespace {

using galaxy::gx::FifoCursor;
using galaxy::gx::FifoParser;
using galaxy::gx::FifoParserProfile;
using galaxy::gx::FifoSink;
using galaxy::gx::GxFatalError;
using galaxy::gx::GxState;
using galaxy::gx::PrimitiveClass;

struct TestSink final : FifoSink {
    [[nodiscard]] std::size_t draw_payload_size(
        std::uint8_t,
        std::uint16_t vertex_count) const override {
        return vertex_count;
    }

    void on_draw(
        PrimitiveClass,
        std::uint8_t,
        FifoCursor& cursor) override {
        const std::uint16_t count = cursor.read_u16();
        const auto payload = cursor.take(count);
        last_draw_payload_first = payload.empty()
            ? 0u : static_cast<std::uint8_t>(payload.front());
        ++draws;
    }

    void on_efb_copy(std::uint32_t value) override {
        ++efb_copies;
        last_copy = value;
    }
    void on_pe_finish() override { ++pe_finishes; }
    void on_pe_token(std::uint16_t token, bool interrupt) override {
        ++pe_tokens;
        last_token = token;
        last_token_interrupt = interrupt;
    }
    void on_invalidate_textures() override {}
    void on_tlut_load() override { ++tlut_loads; }
    void on_invalidate_vertex_cache() override {}

    int draws = 0;
    std::uint8_t last_draw_payload_first = 0u;
    int pe_finishes = 0;
    int efb_copies = 0;
    std::uint32_t last_copy = 0;
    int pe_tokens = 0;
    std::uint16_t last_token = 0;
    bool last_token_interrupt = false;
    int tlut_loads = 0;
};

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}


bool test_xf_matrix_span_updates() {
    constexpr std::array<std::uint16_t, 25> bases{
        0x0000u, 0x0001u, 0x00F0u, 0x00FEu, 0x00FFu, 0x0100u,
        0x03FFu, 0x0400u, 0x0401u, 0x044Fu, 0x045Eu, 0x045Fu,
        0x0460u, 0x04FFu, 0x0500u, 0x0501u, 0x05FFu, 0x0600u,
        0x0601u, 0x066Fu, 0x067Eu, 0x067Fu, 0x0680u, 0xFFF0u, 0xFFFFu};
    constexpr std::array<std::uint16_t, 9> counts{0u, 1u, 2u, 9u, 12u, 16u, 96u, 256u, 384u};
    std::array<std::uint32_t, 384> values{};
    for (unsigned i = 0; i < values.size(); ++i) values[i] = 0x3F800001u + i;
    const auto memory_address = [](std::uint32_t address) {
        return address <= 0x00FFu ||
            (address >= 0x0400u && address <= 0x045Fu) ||
            (address >= 0x0500u && address <= 0x067Fu);
    };
    for (bool indexed : {false, true}) {
        for (const auto base : bases) {
            for (const auto count : counts) {
                bool valid = true;
                for (unsigned i = 0u; i < count; ++i) valid &= memory_address(base + i);
                GxState state;
                (void)state.consume_dirty();
                state.load_cp(galaxy::gx::cp::kMatrixIndexA, 1u);
                std::array<std::uint32_t, 0x2000> expected{};
                if (valid) for (unsigned i = 0u; i < count; ++i) expected[base + i] = values[i];
                bool caught = false;
                try {
                    if (indexed) state.load_xf_indexed(base, values.data(), count);
                    else state.load_xf(base, values.data(), count);
                } catch (const GxFatalError& error) {
                    caught = true;
                    if (!expect(error.opcode() == (indexed ? galaxy::gx::op::kLoadIndxA : galaxy::gx::op::kLoadXfReg),
                                "XF matrix span fault retains direct/indexed opcode")) return false;
                }
                const auto dirty = GxState::kDirtyVsConstants |
                    (valid && count != 0u ? GxState::kDirtyXfMatrices : 0u);
                if (!expect(caught == !valid && state.consume_dirty() == dirty &&
                        std::memcmp(state.xf_raw(), expected.data(), sizeof(expected)) == 0,
                        "XF matrix span boundaries preserve complete validation, words and dirty union")) return false;
                if (valid) {
                    if (indexed) state.load_xf_indexed(base, values.data(), count);
                    else state.load_xf(base, values.data(), count);
                    if (!expect(state.consume_dirty() == 0u,
                                "unchanged XF matrix span remains clean")) return false;
                }
            }
        }
        // Casts keep MSVC's C4244 (an error under /WX) off the braced list.
        for (const std::uint16_t bank :
             {static_cast<std::uint16_t>(0x0040u),
              static_cast<std::uint16_t>(0x0420u),
              static_cast<std::uint16_t>(0x05F0u)}) {
            for (const unsigned dst_offset : {0u, 1u, 2u}) {
                GxState state;
                state.load_xf(bank, values.data(), 32u);
                (void)state.consume_dirty();
                std::array<std::uint32_t, 0x2000> expected{};
                std::copy_n(state.xf_raw(), expected.size(), expected.begin());
                const unsigned src = bank + 1u, dst = bank + dst_offset;
                // Word-by-word copy, so overlapping writes propagate forward as in the XF
                // write loop; memmove would differ.
                for (unsigned i = 0u; i < 12u; ++i) expected[dst + i] = expected[src + i];
                if (indexed) state.load_xf_indexed(static_cast<std::uint16_t>(dst), state.xf_raw() + src, 12u);
                else state.load_xf(static_cast<std::uint16_t>(dst), state.xf_raw() + src, 12u);
                if (!expect(std::memcmp(state.xf_raw(), expected.data(), sizeof(expected)) == 0 &&
                        state.consume_dirty() == (src == dst ? 0u : GxState::kDirtyXfMatrices),
                        "XF matrix span retains aliased-source read/store order")) return false;
            }
        }
    }
    return true;
}

// GxState::xf_palette_dirty() must arm every chunk a write touches: an unarmed chunk would let
// the renderer reuse a palette snapshot that lacks the write.
bool test_xf_palette_chunk_tracking() {
    GxState state;
    if (!expect(state.xf_palette_dirty() == 0u,
                "palette dirty mask starts clear")) return false;

    const std::array<std::uint32_t, 12> object_matrix{
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u};
    state.load_xf(0x0040u, object_matrix.data(), 12u);
    (void)state.consume_dirty();
    if (!expect(state.xf_palette_dirty() == (1ull << 2u),
                "one object matrix arms exactly its own palette chunk")) return false;

    state.load_xf(0x0400u, object_matrix.data(), 12u);
    (void)state.consume_dirty();
    if (!expect(state.xf_palette_dirty() == ((1ull << 2u) | (1ull << 32u)),
                "a distant second matrix adds only its own chunk bit")) return false;

    state.load_xf(0x001Eu, object_matrix.data(), 4u);
    (void)state.consume_dirty();
    if (!expect(state.xf_palette_dirty() ==
                    (((1ull << 2u) | (1ull << 32u)) | 0x1u | 0x2u),
                "a span crossing a chunk boundary arms both chunks")) return false;
    state.clear_xf_palette_dirty(0u, GxState::kMatrixPaletteChunkCount - 1u);
    if (!expect(state.xf_palette_dirty() == 0u,
                "clearing the whole palette span leaves no armed chunk")) return false;

    // LOAD_INDX (object and skin matrices) arms the same bits as a direct write; rewriting
    // identical data arms nothing.
    const std::array<std::uint32_t, 12> skin_matrix{
        21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u};
    state.load_xf_indexed(0x0080u, skin_matrix.data(), 12u);
    (void)state.consume_dirty();
    if (!expect(state.xf_palette_dirty() == (1ull << 4u),
                "an indexed matrix write arms its chunk like a direct one")) return false;
    state.load_xf_indexed(0x0080u, skin_matrix.data(), 12u);
    (void)state.consume_dirty();
    if (!expect(state.xf_palette_dirty() == (1ull << 4u),
                "rewriting identical indexed matrix data arms nothing new")) return false;
    state.clear_xf_palette_dirty(4u, 4u);
    if (!expect(state.xf_palette_dirty() == 0u,
                "clearing one chunk clears exactly that chunk")) return false;
    // Clearing a span that was never armed is a no-op.
    state.load_xf_indexed(0x0080u, skin_matrix.data(), 12u);
    (void)state.consume_dirty();
    state.clear_xf_palette_dirty(4u, GxState::kMatrixPaletteChunkCount - 1u);
    if (!expect(state.xf_palette_dirty() == 0u,
                "clearing a trailing span clears only what was armed")) return false;

    state.load_xf(
        static_cast<std::uint16_t>(galaxy::gx::xf::kNumTexGens),
        object_matrix.data(), 1u);
    state.load_cp(galaxy::gx::cp::kMatrixIndexA, 7u);
    if (!expect(state.xf_palette_dirty() == 0u,
                "high-bank and CP writes never arm a palette chunk")) return false;
    return true;
}

}  // namespace

namespace galaxy::gx {
inline std::uint64_t dependency_reference_mix_u32(std::uint64_t hash, std::uint32_t value) noexcept {
    for (unsigned shift=0;shift<32u;shift+=8u) {
        hash ^= (value >> shift) & 0xFFu;
        hash *= 1099511628211ull;
    }
    return hash;
}
std::uint64_t reference_dependency_shape_state_hash(const GxState& state) noexcept {
    std::uint64_t hash = 1469598103934665603ull;

    // The inputs of GxState::dependency_shape_hash, written out independently.
    hash = dependency_reference_mix_u32(hash, state.cp(cp::kVcdLo));
    hash = dependency_reference_mix_u32(hash, state.cp(cp::kVcdHi));
    for (std::uint8_t fmt = 0; fmt < 8u; ++fmt) {
        hash = dependency_reference_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kVatABase + fmt)));
        hash = dependency_reference_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kVatBBase + fmt)));
        hash = dependency_reference_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kVatCBase + fmt)));
    }
    for (std::uint8_t attr = 0; attr < 16u; ++attr) {
        hash = dependency_reference_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kArrayBaseBase + attr)));
        hash = dependency_reference_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kArrayStrideBase + attr)));
    }

    hash = dependency_reference_mix_u32(hash, state.bp(bp::kGenMode));
    // Selector bits count even for unused stages (a later masked write can expose them);
    // arithmetic, swap and z-format bits do not.
    for (unsigned stage = 0; stage < kMaxTevStages; ++stage) {
        const std::uint32_t color = state.bp(static_cast<std::uint8_t>(
            bp::kTevColorEnvBase + 2u * stage)) & 0xFFFFu;
        const std::uint32_t alpha = state.bp(static_cast<std::uint8_t>(
            bp::kTevAlphaEnvBase + 2u * stage)) & 0xFFF0u;
        hash = dependency_reference_mix_u32(hash, color | (alpha << 12u));
    }
    hash = dependency_reference_mix_u32(hash, state.bp(bp::kTevZEnv1) & 0xCu);
    for (std::uint8_t reg = bp::kTevOrderBase;
         reg < bp::kTevOrderBase + 8u;
         ++reg) {
        hash = dependency_reference_mix_u32(hash, state.bp(reg));
    }
    hash = dependency_reference_mix_u32(hash, state.bp(bp::kIndRef));
    for (std::uint8_t reg = bp::kIndCmdBase;
         reg < bp::kIndCmdBase + kMaxTevStages;
         ++reg) {
        hash = dependency_reference_mix_u32(hash, state.bp(reg));
    }
    hash = dependency_reference_mix_u32(hash, state.bp(bp::kTlutSrcAddr));
    hash = dependency_reference_mix_u32(hash, state.bp(bp::kTlutDest));

    for (std::uint8_t bank = 0; bank <= bp::kTexHighBankOffset;
         bank += bp::kTexHighBankOffset) {
        for (std::uint8_t slot = 0; slot < 4u; ++slot) {
            hash = dependency_reference_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexMode0Base + bank + slot)));
            hash = dependency_reference_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexMode1Base + bank + slot)));
            hash = dependency_reference_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexImage0Base + bank + slot)));
            hash = dependency_reference_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexImage3Base + bank + slot)));
            hash = dependency_reference_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexTlutBase + bank + slot)));
        }
    }

    return hash;
}
}

namespace {
static std::uint64_t dependency_hash_checks=0;
static void verify_dependency_hash(const GxState& s) {
    const auto reference=galaxy::gx::reference_dependency_shape_state_hash(s);
    for(int i=0;i<3;++i) {
        if(s.dependency_shape_hash()!=reference)throw std::runtime_error("stale or changed hash");
        ++dependency_hash_checks;
    }
}
void test_dependency_hash_cache() {
    GxState s;
    std::uint32_t random=0x832C941Du;
    std::vector<std::uint8_t> cp_regs{0,0x10,0x20,0x30,0x40,0x50,0x60};
    for(unsigned i=0;i<8;++i)for(unsigned b: {0x70u,0x80u,0x90u})cp_regs.push_back(static_cast<std::uint8_t>(b+i));
    for(unsigned i=0;i<16;++i)for(unsigned b: {0xA0u,0xB0u})cp_regs.push_back(static_cast<std::uint8_t>(b+i));

        verify_dependency_hash(s);
        for(unsigned i=0;i<32768;++i) {
            random=random*1664525u+1013904223u;
            switch(i%11u) {
            case 0:s.load_cp(cp_regs[i%cp_regs.size()],random);break;
            case 1:
                try{s.load_bp(((i%256u)<<24)|(random&0xFFFFFFu));}
                catch(const galaxy::gx::GxFatalError&){verify_dependency_hash(s);}break;
            case 2:s.load_bp(0xFE000000u|(random&0xFFFFFFu));s.load_bp(0x00000000u|(random>>8));break;
            case 3:s.load_bp(0xE0000000u|(random&0xFFFFFFu));s.load_bp(0xFE800000u);s.load_bp(0xE0800000u);break;
            case 4:s.load_xf(static_cast<std::uint16_t>(i%0x100u),&random,1);break;
            case 5:{GxState copy=s;verify_dependency_hash(copy);copy.load_cp(0x50,random);verify_dependency_hash(copy);s=copy;break;}
            case 6:s.load_cp(0x50,s.cp(0x50));break;
            case 7:(void)s.consume_dirty();s.mark_all_dirty();break;
            case 8:{GxState replacement;replacement.load_cp(0xA0,random);verify_dependency_hash(replacement);s=replacement;break;}
            case 9:
                try{s.load_cp(0xFF,random);throw std::runtime_error("unknown CP accepted");}
                catch(const galaxy::gx::GxFatalError&){verify_dependency_hash(s);}break;
            case 10:s.load_bp(0xFE000000u);s.load_bp(0x80000000u|(random&0xFFFFFFu));break;
            }
            verify_dependency_hash(s);
        }
        for(unsigned r=0;r<256;++r) {
            try{s.load_bp((r<<24)|(random&0xFFFFFFu));}catch(const galaxy::gx::GxFatalError&){}
            verify_dependency_hash(s);
        }
        std::cout<<"PASS 32768 mutation/copy/reset/XF/mask/TEV/dirty/unknown sequences plus256BP registers, "<<dependency_hash_checks<<" independent exact hash dependency_hash_checks\n";


}
}

namespace {
// Digest of GxState's observable state across 46422 BP writes (every address and bit, masks,
// shared TEV banks, copies and faults); main compares it with a golden value.
std::uint64_t bp_write_observation_digest() {
    GxState state;
    std::uint64_t digest=1469598103934665603ull;
    const auto word=[&](std::uint32_t value) {
        for(unsigned i=0;i<4;++i) {
            const auto byte=static_cast<unsigned char>(value>>(i*8));
            digest=(digest^byte)*1099511628211ull;
        }
    };
    const auto observe=[&](std::uint32_t command) {
        bool fault=false;
        try { state.load_bp(command); } catch(const galaxy::gx::GxFatalError& e) {
            fault=true; word(e.opcode()); word(static_cast<std::uint32_t>(e.fifo_offset()));
            for(char c:std::string(e.what())) word(static_cast<unsigned char>(c));
        }
        word(command);word(fault);word(state.pending_bp_write_mask());word(state.consume_dirty());
        auto revision=state.dependency_shape_revision();word(static_cast<std::uint32_t>(revision));word(static_cast<std::uint32_t>(revision>>32));
        auto hash=state.dependency_shape_hash();word(static_cast<std::uint32_t>(hash));word(static_cast<std::uint32_t>(hash>>32));
        for(unsigned i=0;i<256;++i)word(state.bp(static_cast<std::uint8_t>(i)));
        for(const auto& color:state.tev_register_values())for(auto value:color)word(static_cast<std::uint32_t>(value));
        for(auto value:state.tev_konst_colors())word(value);
        for(auto value:state.dependency_shape_words())word(value);
    };
    for(unsigned reg=0;reg<256;++reg)for(unsigned bit=0;bit<24;++bit) {
        observe(0xfeffffffu);observe((reg<<24)|(1u<<bit));
        observe(0xfe000000u|(1u<<bit));observe(reg<<24);
    }
    std::uint32_t random=0xAB72831u;
    for(unsigned i=0;i<16384;++i) {
        random=random*1664525u+1013904223u;
        if(i%3==0)observe(0xfe000000u|(random&0xffffff));
        observe(((i&255u)<<24)|(random&0xffffff));
        if(i%7==0){GxState copy=state;state=copy;}
    }
    return digest;
}
}

int main() try {
    // Unknown GX commands throw, except in the cases below that test the default (skip).
    galaxy::gx::set_unknown_gx_commands_fatal(true);
    if (const std::uint64_t digest = bp_write_observation_digest(); digest != 0x38ae405df5b176beull) {
        std::cerr << "BP write observation regression: digest 0x" << std::hex << digest << std::dec << '\n';
        return 1;
    }
    if (!test_xf_matrix_span_updates()) return 1;
    if (!test_xf_palette_chunk_tracking()) return 1;
    test_dependency_hash_cache();
    static_assert(galaxy::gx::bp::kBpMask == 0xFEu);

    {
        GxState state;
        state.load_bp(0xE0000011u); // ordinary red
        state.load_bp(0xE0800022u); // konst red, shared latch now selects konst
        state.load_bp(0xFE0007FFu);
        state.load_bp(0xE0000033u); // selector outside mask: remains konst
        if (!expect(state.bp(0xE0u) == 0x11u &&
                    (state.tev_konst_colors()[0] >> 24u) == 0x33u,
                    "masked TEV write routed using raw selector")) return 1;
        state.load_bp(0xFE800000u);
        state.load_bp(0xE0000000u); // selector-only write copies shared payload
        if (!expect(state.bp(0xE0u) == 0x33u &&
                    (state.tev_konst_colors()[0] >> 24u) == 0x33u,
                    "TEV mask merged with wrong bank instead of shared latch")) return 1;
    }


    {
        GxState bp_state;
        bp_state.load_bp((0x41u << 24u) | 0x000018u);
        bp_state.load_bp(
            (static_cast<std::uint32_t>(galaxy::gx::bp::kBpMask) << 24u) |
            0x000007u);
        bp_state.load_bp((0x41u << 24u) | 0x000001u);
        if (!expect(
                bp_state.bp(0x41u) == 0x000019u,
                "BP 0xFE mask did not preserve unmasked PE write bits")) {
            return 1;
        }

        // BP 0x0F is the indirect-texture mask register, not BPMEM_BP_MASK.
        bp_state.load_bp((0x0Fu << 24u) | 0x000001u);
        bp_state.load_bp((0x41u << 24u) | 0x000001u);
        if (!expect(
                bp_state.bp(0x0Fu) == 0x000001u,
                "BP 0x0F write was swallowed as a write mask") ||
            !expect(
                bp_state.bp(0x41u) == 0x000001u,
                "BP 0x0F incorrectly masked the following BP write")) {
            return 1;
        }
    }

    {
        GxState channel_state;
        channel_state.load_bp(1u << 4u);  // BP 0x00: one color channel.
        // Lit channel parts keep their complete control word; unlit parts
        // keep only the material source (see build_vertex_shader_key).
        const std::uint32_t controls[4]{
            0x00000113u, 0x00000222u, 0x00000333u, 0x00000444u};
        channel_state.load_xf(
            galaxy::gx::xf::kChannelCtrlBase, controls, 4u);
        const galaxy::gx::VertexShaderKey key =
            galaxy::gx::build_vertex_shader_key(channel_state);
        if (!expect(
                key.channel[0] == controls[0] &&
                key.channel[1] == 0u &&
                key.channel[2] == controls[2] &&
                key.channel[3] == 0u,
                "one-channel VS key did not pair color0 with alpha0")) {
            return 1;
        }
    }

    {
        // Uniform-only writes keep every key; the enable and type transitions below must change
        // the pixel key.
        GxState uniform_state;
        const auto write = [&](std::uint8_t reg, std::uint32_t value) {
            uniform_state.load_bp((static_cast<std::uint32_t>(reg) << 24u) | value);
        };
        const auto uniform_only = [&](std::uint8_t reg, std::uint32_t value) {
            const auto ps = galaxy::gx::build_pixel_shader_key(uniform_state, 6u);
            const auto vs = galaxy::gx::build_vertex_shader_key(uniform_state);
            const auto rs = galaxy::gx::build_render_state_key(uniform_state);
            (void)uniform_state.consume_dirty();
            write(reg, value);
            const auto dirty = uniform_state.consume_dirty();
            return expect((dirty & GxState::kDirtyTevConstants) != 0u &&
                    (dirty & (GxState::kDirtyTev | GxState::kDirtyXfShader |
                              GxState::kDirtyRenderState)) == 0u &&
                    ps == galaxy::gx::build_pixel_shader_key(uniform_state, 6u) &&
                    vs == galaxy::gx::build_vertex_shader_key(uniform_state) &&
                    rs == galaxy::gx::build_render_state_key(uniform_state),
                "uniform-only BP update changed shader/pipeline state or lost its upload");
        };
        for (unsigned i = 0; i < galaxy::gx::bp::kIndMtxCount; ++i) {
            if (!uniform_only(static_cast<std::uint8_t>(galaxy::gx::bp::kIndMtxBase + i), 0x123u + i)) return 1;
        }
        for (unsigned i = 0; i < galaxy::gx::bp::kTexCoordSizeCount; ++i) {
            if (!uniform_only(static_cast<std::uint8_t>(galaxy::gx::bp::kTexCoordSizeBase + i), 0x123u + i)) return 1;
        }
        if (!uniform_only(galaxy::gx::bp::kTevZEnv0, 0x123456u) ||
            !uniform_only(galaxy::gx::bp::kAlphaCompare, 0x3456u) ||
            !uniform_only(galaxy::gx::bp::kFogParam3, 0x12345u) ||
            !uniform_only(galaxy::gx::bp::kFogParam3, 0x112345u)) return 1;
        write(galaxy::gx::bp::kPeControl, 1u); // RGBA6
        write(galaxy::gx::bp::kBlendMode, 1u << 4u); // alpha update
        write(galaxy::gx::bp::kConstAlpha, 0x100u);
        if (!uniform_only(galaxy::gx::bp::kConstAlpha, 0x17fu)) return 1;
        for (const auto& transition : std::array<std::pair<std::uint8_t, std::uint32_t>, 4>{
                std::pair{galaxy::gx::bp::kConstAlpha, 0x7fu},
                std::pair{galaxy::gx::bp::kAlphaCompare, 0x013456u},
                std::pair{galaxy::gx::bp::kFogParam3, 0x212345u},
                std::pair{galaxy::gx::bp::kTevZEnv1, 4u}}) {
            const auto ps = galaxy::gx::build_pixel_shader_key(uniform_state, 6u);
            (void)uniform_state.consume_dirty();
            write(transition.first, transition.second);
            if (!expect((uniform_state.consume_dirty() & GxState::kDirtyTev) != 0u &&
                    !(ps == galaxy::gx::build_pixel_shader_key(uniform_state, 6u)),
                    "shader-changing BP update failed to invalidate the pixel key")) return 1;
        }
        // Classify the effective masked value, not the unmasked command.
        write(galaxy::gx::bp::kBpMask, 0xffffu);
        if (!uniform_only(galaxy::gx::bp::kAlphaCompare, 0xffabcdu)) return 1;
        const auto masked_ps = galaxy::gx::build_pixel_shader_key(uniform_state, 6u);
        write(galaxy::gx::bp::kBpMask, 0xff0000u);
        (void)uniform_state.consume_dirty();
        write(galaxy::gx::bp::kAlphaCompare, 0xaa0000u);
        if (!expect((uniform_state.consume_dirty() & GxState::kDirtyTev) != 0u &&
                !(masked_ps == galaxy::gx::build_pixel_shader_key(uniform_state, 6u)),
                "masked alpha predicate write retained a stale pixel key")) return 1;
    }

    {
        // After each classified write the cached dependency hash must match a fresh state rebuilt
        // from the raw registers, which catches a missed invalidation.
        GxState state;
        for (unsigned reg = 0; reg < 256u; ++reg) {
            if (reg == galaxy::gx::bp::kBpMask) continue;
            (void)state.dependency_shape_hash();
            try {
                state.load_bp((reg << 24u) | (0x321u + reg));
            } catch (const GxFatalError&) { continue; }
            GxState fresh;
            for (unsigned source = 0; source < 256u; ++source) {
                if (source == galaxy::gx::bp::kBpMask) continue;
                try { fresh.load_bp((source << 24u) | state.bp(static_cast<std::uint8_t>(source))); }
                catch (const GxFatalError&) { }
            }
            if (!expect(state.dependency_shape_hash() == fresh.dependency_shape_hash(),
                    "BP dependency shape hash retained stale inputs")) return 1;
        }
        for (unsigned reg = 0; reg < 256u; ++reg) {
            (void)state.dependency_shape_hash();
            try { state.load_cp(static_cast<std::uint8_t>(reg), 0x123u + reg); }
            catch (const GxFatalError&) { continue; }
            // Rebuild CP/BP to force an uncached hash of the same inputs.
            GxState fresh;
            for (unsigned source = 0; source < 256u; ++source) {
                try { fresh.load_cp(static_cast<std::uint8_t>(source), state.cp(static_cast<std::uint8_t>(source))); }
                catch (const GxFatalError&) { }
                if (source == galaxy::gx::bp::kBpMask) continue;
                try { fresh.load_bp((source << 24u) | state.bp(static_cast<std::uint8_t>(source))); }
                catch (const GxFatalError&) { }
            }
            if (!expect(state.dependency_shape_hash() == fresh.dependency_shape_hash(),
                    "CP dependency shape hash retained stale inputs")) return 1;
        }
    }

    {
        GxState known_bp_state;
        (void)known_bp_state.consume_dirty();

        std::ostringstream captured_stderr;
        std::streambuf* previous_stderr =
            std::cerr.rdbuf(captured_stderr.rdbuf());

        const std::array<std::uint8_t, 25> classified_bp_regs{
            0x69u, 0x46u, 0x58u, 0xE8u, 0x44u, 0x01u, 0x02u,
            0x03u, 0x04u, 0x55u, 0x56u, 0x30u, 0x31u, 0x0Cu,
            0x0Du, 0x0Eu, 0x32u, 0x33u, 0x34u, 0x35u, 0xEDu,
            0x4Cu, 0x9Cu, 0xBCu, 0xFEu};
        for (std::size_t i = 0; i < classified_bp_regs.size(); ++i) {
            const std::uint8_t reg = classified_bp_regs[i];
            known_bp_state.load_bp(
                (static_cast<std::uint32_t>(reg) << 24u) |
                (0x100u + static_cast<std::uint32_t>(i)));
        }

        std::cerr.rdbuf(previous_stderr);
        if (!expect(
                captured_stderr.str().empty(),
                "known GX BP registers were still reported as unknown")) {
            return 1;
        }
        if (!expect(
                known_bp_state.bp(0x0Eu) == 0x10Fu,
                "third indirect matrix column was not stored") ||
            !expect(
                (known_bp_state.consume_dirty() &
                 (GxState::kDirtyTev | GxState::kDirtyVsConstants |
                  GxState::kDirtyRenderState | GxState::kDirtyTevConstants)) != 0,
                "known GX BP state did not mark dependent state dirty")) {
            return 1;
        }
    }

    {
        // Both ends of every classified XF range are accepted.
        GxState classified_xf_state;
        constexpr std::array<std::uint16_t, 23> kClassifiedXfAddresses{
            0x0000u, 0x00FFu,
            0x0400u, 0x045Fu,
            0x0500u, 0x05FFu,
            0x0600u, 0x067Fu,
            0x1000u, 0x1008u,
            0x1009u, 0x1012u,
            0x1013u, 0x1019u,
            0x101Au, 0x101Fu,
            0x1020u, 0x1026u,
            0x103Fu,
            0x1040u, 0x1047u,
            0x1050u, 0x1057u};
        for (std::size_t i = 0; i < kClassifiedXfAddresses.size(); ++i) {
            const std::uint32_t value =
                0x01000000u + static_cast<std::uint32_t>(i);
            classified_xf_state.load_xf(
                kClassifiedXfAddresses[i],
                &value,
                1u);
            if (!expect(
                    classified_xf_state.xf(kClassifiedXfAddresses[i]) == value,
                    "classified XF boundary write was not retained")) {
                return 1;
            }
        }
    }

    {
        // XF 0x1000-0x1008 (status and diagnostic registers, written during GX init) are stored
        // without dirty bits.
        GxState status_reg_state;
        (void)status_reg_state.consume_dirty();  // discard initial ~0u
        for (std::uint16_t addr = galaxy::gx::xf::kStatusRegsBase;
             addr < galaxy::gx::xf::kNumChannels;
             ++addr) {
            const std::uint32_t value = 0x0000003Fu;
            status_reg_state.load_xf(addr, &value, 1u);
            if (!expect(
                    status_reg_state.xf(addr) == value,
                    "XF status/diagnostic register write was not retained")) {
                return 1;
            }
            if (!expect(
                    status_reg_state.consume_dirty() == 0u,
                    "XF status/diagnostic register write incorrectly "
                    "marked dependent state dirty")) {
                return 1;
            }
        }
    }

    {
        // CP 0x00/0x10/0x20 (unknown purpose; GX init writes 0x20 = 0) are accepted without
        // dirty bits.
        GxState unknown_cp_state;
        (void)unknown_cp_state.consume_dirty();  // discard initial ~0u
        for (const std::uint8_t reg : {galaxy::gx::cp::kUnknown00,
                                        galaxy::gx::cp::kUnknown10,
                                        galaxy::gx::cp::kUnknown20}) {
            unknown_cp_state.load_cp(reg, 0x00000000u);
            if (!expect(
                    unknown_cp_state.consume_dirty() == 0u,
                    "unknown CP register write incorrectly marked "
                    "dependent state dirty")) {
                return 1;
            }
        }
    }

    {
        // BP 0x23/0x24 (performance counters; GX init writes 0x23 = 0) are accepted without
        // dirty bits.
        GxState perf_bp_state;
        (void)perf_bp_state.consume_dirty();  // discard initial ~0u
        for (std::uint8_t reg = galaxy::gx::bp::kPerf0TriBase;
             reg < galaxy::gx::bp::kPerf0TriBase + 2u;
             ++reg) {
            const std::uint32_t command =
                (static_cast<std::uint32_t>(reg) << 24) | 0x000000u;
            perf_bp_state.load_bp(command);
            if (!expect(
                    perf_bp_state.consume_dirty() == 0u,
                    "perf-query BP register write incorrectly marked "
                    "dependent state dirty")) {
                return 1;
            }
        }
    }

    {
        // XF 0x1013-0x1019 (unknown group and the XF matrix-index copy; GX init writes 0x1018)
        // are stored without dirty bits.
        GxState matrix_ind_state;
        (void)matrix_ind_state.consume_dirty();  // discard initial ~0u
        for (std::uint16_t addr = galaxy::gx::xf::kUnknownGroup1Base;
             addr < galaxy::gx::xf::kViewportBase;
             ++addr) {
            const std::uint32_t value = 0x3CF3CF00u;
            matrix_ind_state.load_xf(addr, &value, 1u);
            if (!expect(
                    matrix_ind_state.xf(addr) == value,
                    "XF unknown-group/matrix-index write was not "
                    "retained")) {
                return 1;
            }
            if (!expect(
                    matrix_ind_state.consume_dirty() == 0u,
                    "XF unknown-group/matrix-index write incorrectly "
                    "marked dependent state dirty")) {
                return 1;
            }
        }
    }

    {
        // An unknown CP address beside a known register (0x31) is rejected without changing the
        // known one.
        GxState cp_state;
        cp_state.load_cp(galaxy::gx::cp::kMatrixIndexA, 0x12345678u);
        (void)cp_state.consume_dirty();
        bool caught = false;
        try {
            cp_state.load_cp(0x31u, 0xA5A5A5A5u);
        } catch (const GxFatalError& error) {
            const std::string message = error.what();
            caught = error.opcode() == galaxy::gx::op::kLoadCpReg &&
                message.find("[GxState] unknown CP state write") !=
                    std::string::npos &&
                message.find("register=0x31") != std::string::npos &&
                message.find("value=0xA5A5A5A5") != std::string::npos;
        }
        if (!expect(caught, "unknown CP write did not hard-fail with context") ||
            !expect(
                cp_state.cp(galaxy::gx::cp::kMatrixIndexA) == 0x12345678u &&
                    cp_state.cp(0x31u) == 0u &&
                    cp_state.consume_dirty() == 0u,
                "unknown CP write mutated register or dirty state")) {
            return 1;
        }
    }

    {
        // A multiword XF transfer is one transaction: a bad trailing address
        // must be detected before the valid leading word is stored.
        GxState xf_state;
        constexpr std::uint32_t kOriginal = 0x11223344u;
        xf_state.load_xf(0x00FFu, &kOriginal, 1u);
        (void)xf_state.consume_dirty();
        const std::array<std::uint32_t, 2> values{
            0x55667788u,
            0x99AABBCCu};
        bool caught = false;
        try {
            xf_state.load_xf(0x00FFu, values.data(), 2u);
        } catch (const GxFatalError& error) {
            const std::string message = error.what();
            caught = error.opcode() == galaxy::gx::op::kLoadXfReg &&
                message.find("[GxState] unknown XF state write") !=
                    std::string::npos &&
                message.find("register=0x0100") != std::string::npos &&
                message.find("value=0x99AABBCC") != std::string::npos &&
                message.find("transfer-base=0x00FF") != std::string::npos &&
                message.find("transfer-count=2") != std::string::npos;
        }
        if (!expect(caught, "unknown XF write did not hard-fail with context") ||
            !expect(
                xf_state.xf(0x00FFu) == kOriginal &&
                    xf_state.xf(0x0100u) == 0u &&
                    xf_state.consume_dirty() == 0u,
                "unknown XF transaction partially mutated state")) {
            return 1;
        }

        constexpr std::uint32_t kIndexedUnknown = 0xDEADC0DEu;
        caught = false;
        try {
            xf_state.load_xf_indexed(0x00FFu, values.data(), 2u);
        } catch (const GxFatalError& error) {
            caught = error.opcode() == galaxy::gx::op::kLoadIndxA &&
                std::string(error.what()).find("register=0x0100") != std::string::npos;
        }
        if (!expect(caught && xf_state.xf(0x00FFu) == kOriginal &&
                xf_state.xf(0x0100u) == 0u && xf_state.consume_dirty() == 0u,
                "indexed XF transaction rejects a bad trailing word before any mutation")) return 1;
        caught = false;
        try {
            xf_state.load_xf_indexed(0x1048u, &kIndexedUnknown, 1u);
        } catch (const GxFatalError& error) {
            const std::string message = error.what();
            caught = error.opcode() == galaxy::gx::op::kLoadIndxA &&
                message.find("register=0x1048") != std::string::npos &&
                message.find("value=0xDEADC0DE") != std::string::npos;
        }
        if (!expect(
                caught,
                "unknown indexed XF write did not identify LOAD_INDX family") ||
            !expect(
                xf_state.xf(0x1048u) == 0u &&
                    xf_state.consume_dirty() == 0u,
                "unknown indexed XF write mutated state")) {
            return 1;
        }
    }

    {
        GxState direct, indexed;
        (void)direct.consume_dirty();
        (void)indexed.consume_dirty();
        std::array<std::uint32_t, 12> words{};
        for (unsigned i = 0u; i < words.size(); ++i) words[i] = 0x3f800000u + i;
        for (const std::uint16_t base :
             {static_cast<std::uint16_t>(0x0000u),
              static_cast<std::uint16_t>(0x0040u),
              static_cast<std::uint16_t>(0x0400u),
              static_cast<std::uint16_t>(0x0500u),
              static_cast<std::uint16_t>(0x0600u),
              static_cast<std::uint16_t>(0x1009u)}) {
            const auto count = static_cast<std::uint16_t>(base == 0x1009u ? 4u : words.size());
            direct.load_xf(base, words.data(), count);
            indexed.load_xf_indexed(base, words.data(), count);
            if (!expect(std::memcmp(direct.xf_raw(), indexed.xf_raw(), 0x2000u * sizeof(std::uint32_t)) == 0 &&
                    direct.consume_dirty() == indexed.consume_dirty(),
                    "direct and indexed XF transfers retain identical register and dirty effects")) return 1;
        }
    }

    {
        // An unknown BP write must not store its raw value or consume the
        // one-shot write mask armed by the preceding classified command.
        GxState bp_state;
        bp_state.load_bp((0x41u << 24u) | 0x000018u);
        (void)bp_state.consume_dirty();
        bp_state.load_bp(
            (static_cast<std::uint32_t>(galaxy::gx::bp::kBpMask) << 24u) |
            0x000007u);
        bool caught = false;
        try {
            bp_state.load_bp((0x05u << 24u) | 0x00ABCDEFu);
        } catch (const GxFatalError& error) {
            const std::string message = error.what();
            caught = error.opcode() == galaxy::gx::op::kLoadBpReg &&
                message.find("[GxState] unknown BP state write") !=
                    std::string::npos &&
                message.find("register=0x05") != std::string::npos &&
                message.find("value=0xABCDEF") != std::string::npos &&
                message.find("command=0x05ABCDEF") != std::string::npos;
        }
        if (!expect(caught, "unknown BP write did not hard-fail with context") ||
            !expect(
                bp_state.bp(0x05u) == 0u &&
                    bp_state.bp(0x41u) == 0x000018u &&
                    bp_state.consume_dirty() == 0u,
                "unknown BP write mutated register or dirty state")) {
            return 1;
        }
        bp_state.load_bp((0x41u << 24u) | 0x000001u);
        if (!expect(
                bp_state.bp(0x41u) == 0x000019u,
                "unknown BP write consumed the one-shot write mask")) {
            return 1;
        }
    }

    {
        // Non-fatal mode (the default): an unknown CP, XF or BP write and a TMEM load of lines are
        // skipped without changing any register, the write mask or the dirty state, and a
        // transfer that mixes known and unknown XF addresses keeps its known words.
        galaxy::gx::set_unknown_gx_commands_fatal(false);
        GxState state;
        state.load_cp(galaxy::gx::cp::kMatrixIndexA, 0x12345678u);
        state.load_bp((0x41u << 24u) | 0x000018u);
        state.load_bp((static_cast<std::uint32_t>(galaxy::gx::bp::kBpMask) << 24u) | 0x000007u);
        (void)state.consume_dirty();
        bool threw = false;
        try {
            state.load_cp(0x31u, 0xA5A5A5A5u);
            state.load_bp((0x05u << 24u) | 0x00ABCDEFu);
            state.load_bp((0x05u << 24u) | 0x00ABCDEFu);  // a repeat is skipped silently
        } catch (const GxFatalError&) {
            threw = true;
        }
        if (!expect(!threw, "unknown CP and BP writes are skipped by default") ||
            !expect(state.cp(0x31u) == 0u && state.bp(0x05u) == 0u &&
                        state.cp(galaxy::gx::cp::kMatrixIndexA) == 0x12345678u && state.consume_dirty() == 0u,
                    "a skipped write changes no register or dirty state")) {
            return 1;
        }
        state.load_bp((0x41u << 24u) | 0x000001u);
        if (!expect(state.bp(0x41u) == 0x000019u, "a skipped BP write leaves the one-shot mask armed")) return 1;

        // 0x1057 is the last post-texgen register; 0x1058 is not modelled.
        const std::uint32_t mixed[2] = {0x00000123u, 0xDEADBEEFu};
        const std::uint16_t known_xf = static_cast<std::uint16_t>(galaxy::gx::xf::kPostTexGenBase + 7u);
        threw = false;
        try {
            state.load_xf(known_xf, mixed, 2u);
        } catch (const GxFatalError&) {
            threw = true;
        }
        if (!expect(!threw, "unknown XF addresses are skipped by default") ||
            !expect(state.xf(known_xf) == 0x00000123u && state.xf(static_cast<std::uint16_t>(known_xf + 1u)) == 0u,
                    "a mixed XF transfer keeps its known word and drops the unknown one")) {
            return 1;
        }

        threw = false;
        try {
            state.load_bp((static_cast<std::uint32_t>(galaxy::gx::bp::kTexLoadStart) << 24u) | 0x000010u);
        } catch (const GxFatalError&) {
            threw = true;
        }
        if (!expect(!threw, "a TMEM load of lines is skipped by default")) return 1;
        galaxy::gx::set_unknown_gx_commands_fatal(true);
    }

    {
        // GXTexModeSync's FIFO command (RMGE01 0x804BA0F8: 0x61, then 0x63000000), a TMEM load
        // of nothing: it parses, changes no register or dirty state and consumes an armed write
        // mask. A load of lines is still rejected.
        FifoParser sync_parser;
        GxState sync_state;
        TestSink sync_sink;
        sync_state.load_bp((0x41u << 24u) | 0x000018u);
        (void)sync_state.consume_dirty();
        const std::array<std::byte, 10> sync_bytes{
            std::byte{0x61}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00}, std::byte{0x07},
            std::byte{0x61}, std::byte{0x63}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
        bool threw = false;
        std::size_t consumed = 0u;
        try {
            consumed = sync_parser.run_available(sync_bytes, nullptr, sync_sink, sync_state);
        } catch (const GxFatalError&) {
            threw = true;
        }
        if (!expect(!threw && consumed == sync_bytes.size(), "GXTexModeSync command did not parse") ||
            !expect(sync_state.bp(galaxy::gx::bp::kTexLoadStart) == 0u && sync_state.consume_dirty() == 0u,
                    "GXTexModeSync changed register or dirty state")) {
            return 1;
        }
        sync_state.load_bp((0x41u << 24u) | 0x000001u);
        if (!expect(sync_state.bp(0x41u) == 0x000001u, "GXTexModeSync left the write mask armed")) {
            return 1;
        }
        bool rejected = false;
        try {
            sync_state.load_bp((static_cast<std::uint32_t>(galaxy::gx::bp::kTexLoadStart) << 24u) | 0x000010u);
        } catch (const GxFatalError& error) {
            rejected = std::string(error.what()).find("TMEM load not emulated") != std::string::npos;
        }
        if (!expect(rejected && sync_state.bp(galaxy::gx::bp::kTexLoadStart) == 0u, "a TMEM load of lines was accepted")) {
            return 1;
        }
    }

    FifoParser parser;
    GxState state;
    TestSink sink;

    const std::array<std::byte, 3> bp_head{
        std::byte{0x61}, std::byte{0x40}, std::byte{0x00}};
    const std::size_t bp_consumed =
        parser.run_available(bp_head, nullptr, sink, state);
    if (!expect(bp_consumed == 0, "split BP command consumed a partial prefix")) {
        return 1;
    }

    const std::array<std::byte, 5> bp_full{
        std::byte{0x61}, std::byte{0x40}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x17}};
    const std::size_t bp_full_consumed =
        parser.run_available(bp_full, nullptr, sink, state);
    if (!expect(bp_full_consumed == bp_full.size(), "complete BP command not consumed") ||
        !expect(state.bp(0x40) == 0x17u, "complete BP command not applied")) {
        return 1;
    }

    const std::array<std::byte, 10> pe_tokens{
        std::byte{0x61}, std::byte{0x48}, std::byte{0x00},
        std::byte{0x12}, std::byte{0x34},
        std::byte{0x61}, std::byte{0x47}, std::byte{0x00},
        std::byte{0xAB}, std::byte{0xCD}};
    const std::size_t pe_consumed =
        parser.run_available(pe_tokens, nullptr, sink, state);
    if (!expect(pe_consumed == pe_tokens.size(), "PE token commands not consumed") ||
        !expect(sink.pe_tokens == 2, "PE token callbacks not issued") ||
        !expect(sink.last_token == 0xABCDu, "PE token did not use low 16 bits") ||
        !expect(!sink.last_token_interrupt, "BP 0x47 incorrectly raised an interrupt")) {
        return 1;
    }

    const std::array<std::byte, 10> pe_done{
        std::byte{0x61}, std::byte{0x45}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
        std::byte{0x61}, std::byte{0x45}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x02}};
    const std::size_t pe_done_consumed =
        parser.run_available(pe_done, nullptr, sink, state);
    if (!expect(pe_done_consumed == pe_done.size(), "PE done commands not consumed") ||
        !expect(sink.pe_finishes == 1, "BP 0x45 raised finish without bit 1 set")) {
        return 1;
    }

    {
        const int tlut_loads_before = sink.tlut_loads;
        const std::array<std::byte, 5> tlut_src{
            std::byte{0x61},
            std::byte{galaxy::gx::bp::kTlutSrcAddr},
            std::byte{0x00}, std::byte{0x12}, std::byte{0x34}};
        const std::size_t tlut_src_consumed =
            parser.run_available(tlut_src, nullptr, sink, state);
        if (!expect(
                tlut_src_consumed == tlut_src.size(),
                "TLUT source BP command was not consumed") ||
            !expect(
                sink.tlut_loads == tlut_loads_before,
                "TLUT source latch incorrectly triggered a palette load") ||
            !expect(
                state.bp(galaxy::gx::bp::kTlutSrcAddr) == 0x001234u,
                "TLUT source BP command was not latched")) {
            return 1;
        }

        const std::array<std::byte, 5> tlut_dest{
            std::byte{0x61},
            std::byte{galaxy::gx::bp::kTlutDest},
            std::byte{0x00}, std::byte{0x40}, std::byte{0x10}};
        const std::size_t tlut_dest_consumed =
            parser.run_available(tlut_dest, nullptr, sink, state);
        if (!expect(
                tlut_dest_consumed == tlut_dest.size(),
                "TLUT destination BP command was not consumed") ||
            !expect(
                sink.tlut_loads == tlut_loads_before + 1,
                "TLUT destination BP command did not trigger a palette load") ||
            !expect(
                state.bp(galaxy::gx::bp::kTlutDest) == 0x004010u,
                "TLUT destination BP command was not latched")) {
            return 1;
        }
    }

    // One complete NOP followed by a draw split inside its payload. Only the
    // NOP is consumed; retrying the retained tail executes the draw once.
    const std::array<std::byte, 6> draw_head{
        std::byte{0x00}, std::byte{0x90}, std::byte{0x00},
        std::byte{0x03}, std::byte{0xAA}, std::byte{0xBB}};
    const std::size_t draw_consumed =
        parser.run_available(draw_head, nullptr, sink, state);
    if (!expect(draw_consumed == 1, "split draw did not retain its opcode") ||
        !expect(sink.draws == 0, "split draw executed before payload completed")) {
        return 1;
    }

    std::vector<std::byte> draw_tail(
        draw_head.begin() + static_cast<std::ptrdiff_t>(draw_consumed),
        draw_head.end());
    draw_tail.push_back(std::byte{0xCC});
    const std::size_t draw_tail_consumed =
        parser.run_available(draw_tail, nullptr, sink, state);
    if (!expect(
            draw_tail_consumed == draw_tail.size(),
            "completed draw tail not consumed") ||
        !expect(sink.draws == 1, "completed draw did not execute exactly once")) {
        return 1;
    }

    const std::array<std::byte, 7> hw_noop_then_draw{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x3F},
        std::byte{0x90}, std::byte{0x00}, std::byte{0x01},
        std::byte{0xDD}};
    const std::size_t noop_draw_consumed =
        parser.run_available(hw_noop_then_draw, nullptr, sink, state);
    if (!expect(
            noop_draw_consumed == hw_noop_then_draw.size(),
            "hardware no-op command bytes were not consumed") ||
        !expect(sink.draws == 2, "draw after hardware no-ops did not execute")) {
        return 1;
    }








    {
        // The pixel shader key itself is the oracle for the BP dirty bits: every blend word and
        // bit flip, for each PE format, constant-alpha mode and EFB scale.
        std::uint64_t checked = 0u;
        for (std::uint32_t format = 0u; format < 4u; ++format) {
            for (std::uint32_t replacement : {0u, 0x17fu}) {
                for (std::uint32_t scale : {1u, 6u}) {
                    GxState blend_state;
                    const auto write = [&](std::uint8_t reg, std::uint32_t word) {
                        blend_state.load_bp((static_cast<std::uint32_t>(reg) << 24u) | word);
                    };
                    write(galaxy::gx::bp::kGenMode, 15u << 10u);
                    write(galaxy::gx::bp::kPeControl, format);
                    write(galaxy::gx::bp::kConstAlpha, replacement);
                    for (std::uint32_t word = 0u; word < 0x10000u; ++word) {
                        write(galaxy::gx::bp::kBlendMode, word);
                        const auto before = galaxy::gx::build_pixel_shader_key(blend_state, scale);
                        for (unsigned bit = 0u; bit < 16u; ++bit) {
                            (void)blend_state.consume_dirty();
                            write(galaxy::gx::bp::kBlendMode, word ^ (1u << bit));
                            const auto dirty = blend_state.consume_dirty();
                            const auto after = galaxy::gx::build_pixel_shader_key(blend_state, scale);
                            if ((dirty & GxState::kDirtyRenderState) == 0u ||
                                (dirty & GxState::kDirtyTevConstants) != 0u ||
                                (!(before == after) && (dirty & GxState::kDirtyTev) == 0u) ||
                                ((bit == 1u || bit == 3u || bit >= 12u) &&
                                 (dirty & GxState::kDirtyTev) != 0u)) {
                                std::cerr << "blend dependency mismatch format=" << format
                                          << " replacement=" << replacement << " scale=" << scale
                                          << " word=" << word << " bit=" << bit << '\n';
                                return 1;
                            }
                            write(galaxy::gx::bp::kBlendMode, word);
                            ++checked;
                        }
                    }
                }
            }
        }
        // Partial BP writes must classify the effective merged value; the next
        // full write must not inherit the previous one-shot register mask.
        GxState masked_blend;
        masked_blend.load_bp((0x41u << 24u) | 0x411u);
        (void)masked_blend.consume_dirty();
        masked_blend.load_bp((0xfeu << 24u) | (1u << 3u));
        masked_blend.load_bp((0x41u << 24u) | 0x408u);
        const auto masked_dirty = masked_blend.consume_dirty();
        if (!expect(masked_blend.bp(0x41u) == 0x419u &&
                    masked_dirty == GxState::kDirtyRenderState,
                    "masked color write must retain blend and skip shader/constants work")) return 1;
        masked_blend.load_bp((0x41u << 24u) | 0x511u);
        if (!expect(masked_blend.consume_dirty() == GxState::kDirtyRenderState,
                    "source alpha factor4-to5 changes blend PSO but not shader/constants")) return 1;
        masked_blend.load_bp((0x42u << 24u) | 0x180u);
        masked_blend.load_bp((0x43u << 24u) | 1u);
        const auto coalesced = masked_blend.consume_dirty();
        if (!expect((coalesced & (GxState::kDirtyTev | GxState::kDirtyTevConstants |
                                  GxState::kDirtyRenderState)) ==
                    (GxState::kDirtyTev | GxState::kDirtyTevConstants | GxState::kDirtyRenderState),
                    "destination-alpha/format writes must retain coalesced invalidation")) return 1;
        std::cout << "blend dependency oracle: " << checked << " bit transitions passed\n";
    }

    bool strict_failed = false;
    try {
        parser.run(bp_head, nullptr, sink, state);
    } catch (const GxFatalError&) {
        strict_failed = true;
    }
    if (!expect(strict_failed, "strict parser accepted a truncated command")) {
        return 1;
    }

    std::cout << "fifo_parser_tests: PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "fifo_parser_tests: unexpected exception: " << error.what() << '\n';
    return 1;
}
