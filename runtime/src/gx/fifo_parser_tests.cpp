#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/uber_constants.h"
#include <stdexcept>

#include <array>
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

    [[nodiscard]] bool begin_cached_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::size_t draw_count) override {
        ++cached_draw_run_begins;
        last_cached_draw_run_primitive = primitive;
        last_cached_draw_run_vtxfmt = vtxfmt;
        last_cached_draw_run_count = draw_count;
        return enable_cached_draw_runs;
    }

    [[nodiscard]] bool on_cached_prepared_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> prepared_payload,
        std::size_t,
        std::size_t,
        std::uint8_t,
        std::size_t source_draw_count) override {
        ++prepared_draw_run_calls;
        last_prepared_primitive = primitive;
        last_prepared_vtxfmt = vtxfmt;
        last_prepared_source_draw_count = source_draw_count;
        last_prepared_payload.assign(
            prepared_payload.begin(), prepared_payload.end());
        if (!enable_prepared_draw_runs) {
            return false;
        }
        const auto count_hi = static_cast<std::uint16_t>(
            prepared_payload.size() >= 1u
                ? static_cast<std::uint8_t>(prepared_payload[0])
                : 0u);
        const auto count_lo = static_cast<std::uint16_t>(
            prepared_payload.size() >= 2u
                ? static_cast<std::uint8_t>(prepared_payload[1])
                : 0u);
        draws += static_cast<int>((count_hi << 8) | count_lo);
        return true;
    }

    [[nodiscard]] bool on_cached_packet_draw_run(
        std::uint64_t cache_token,
        std::size_t packet_run_index,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const galaxy::gx::CachedDrawPacket> packets,
        std::uint32_t total_vertices,
        std::uint32_t total_indices,
        std::span<const std::uint16_t> precomputed_indices) override {
        ++packet_draw_run_calls;
        last_packet_cache_token = cache_token;
        last_packet_run_index = packet_run_index;
        last_packet_total_vertices = total_vertices;
        last_packet_total_indices = total_indices;
        last_packet_precomputed_indices = precomputed_indices.size();
        last_packet_primitive = primitive;
        last_packet_vtxfmt = vtxfmt;
        last_packet_source_draw_count = packets.size();
        if (!enable_packet_draw_runs) {
            return false;
        }
        for (const galaxy::gx::CachedDrawPacket& packet : packets) {
            FifoCursor cursor;
            cursor.data = bytes;
            cursor.offset = packet.draw_cursor_offset;
            cursor.base_offset = base_offset;
            cursor.command_offset = base_offset + packet.local_opcode_offset;
            cursor.recover_truncation = false;
            on_draw(primitive, vtxfmt, cursor);
        }
        return true;
    }

    void end_cached_draw_run() override {
        ++cached_draw_run_ends;
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
    bool enable_cached_draw_runs = false;
    int cached_draw_run_begins = 0;
    int cached_draw_run_ends = 0;
    bool enable_prepared_draw_runs = false;
    int prepared_draw_run_calls = 0;
    PrimitiveClass last_prepared_primitive = PrimitiveClass::Points;
    std::uint8_t last_prepared_vtxfmt = 0;
    std::size_t last_prepared_source_draw_count = 0;
    std::vector<std::byte> last_prepared_payload;
    bool enable_packet_draw_runs = false;
    int packet_draw_run_calls = 0;
    PrimitiveClass last_packet_primitive = PrimitiveClass::Points;
    std::uint8_t last_packet_vtxfmt = 0;
    std::size_t last_packet_source_draw_count = 0;
    std::uint64_t last_packet_cache_token = 0;
    std::size_t last_packet_run_index = static_cast<std::size_t>(-1);
    std::uint32_t last_packet_total_vertices = 0;
    std::uint32_t last_packet_total_indices = 0;
    std::size_t last_packet_precomputed_indices = 0;
    PrimitiveClass last_cached_draw_run_primitive = PrimitiveClass::Points;
    std::uint8_t last_cached_draw_run_vtxfmt = 0;
    std::size_t last_cached_draw_run_count = 0;
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

bool test_display_list_variant_lookup_invalidation() {
    for (const unsigned variants : {2u, 8u, 9u}) {
        std::array<std::byte, 64> bytes{};
        bytes[0] = std::byte{0x90};
        bytes[2] = std::byte{0x01};
        bytes[3] = std::byte{0xA5};
        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = bytes.data();
        memory.fast_regions[8].size = static_cast<std::uint32_t>(bytes.size());
        FifoParserProfile profile{};
        FifoParser parser;
        parser.set_profile(&profile);
        GxState state;
        TestSink sink;
        const std::array<std::byte, 9> call{
            std::byte{0x40}, std::byte{0x80}, std::byte{0}, std::byte{0}, std::byte{0},
            std::byte{0}, std::byte{0}, std::byte{0}, std::byte{16}};
        auto other = call;
        other[4] = std::byte{32};  // A different all-NOP list.
        const auto run = [&](const auto& command) {
            return parser.run_available(command, &memory, sink, state) == command.size();
        };
        for (unsigned i = 0; i < variants; ++i) {
            state.load_cp(galaxy::gx::cp::kVatABase, i + 1u);
            if (!run(call)) return false;
        }
        if (!expect(profile.call_dl_cache_stores == variants && profile.call_dl_cache_hits == 0u,
                    "distinct VAT dependencies retain display-list variants")) return false;
        if (!run(other)) return false;
        for (unsigned i = 0; i < variants; ++i) {
            state.load_cp(galaxy::gx::cp::kVatABase, i + 1u);
            // Force the keyed candidate path rather than the last-list shortcut.
            if (!run(other) || !run(call)) return false;
            if (!expect(sink.last_draw_payload_first == 0xA5u &&
                    profile.call_dl_cache_stores == variants + 1u &&
                    profile.call_dl_cache_hits == (i + 1u) * 2u,
                    "small/large candidate snapshots select the matching cached variant")) return false;
        }
        if (!run(other)) return false;
        const auto hits_before_change = profile.call_dl_cache_hits;
        bytes[3] = std::byte{0xD3};
        if (!run(call)) return false;
        if (!expect(sink.last_draw_payload_first == 0xD3u &&
                profile.call_dl_cache_hits == hits_before_change &&
                profile.call_dl_cache_stores == variants + 2u &&
                sink.draws == static_cast<int>(variants * 2u + 1u),
                "changed list bytes invalidate every variant safely during candidate traversal")) return false;
        // Reinstating old bytes cannot resurrect a variant invalidated above.
        bytes[3] = std::byte{0xA5};
        if (!run(call)) return false;
        if (!expect(sink.last_draw_payload_first == 0xA5u &&
                profile.call_dl_cache_hits == hits_before_change &&
                profile.call_dl_cache_stores == variants + 3u,
                "last-list changed-byte check preserves invalidation on restored bytes")) return false;
    }
    return true;
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
        for (const std::uint16_t bank : {0x0040u, 0x0420u, 0x05F0u}) {
            for (const unsigned dst_offset : {0u, 1u, 2u}) {
                GxState state;
                state.load_xf(bank, values.data(), 32u);
                (void)state.consume_dirty();
                std::array<std::uint32_t, 0x2000> expected{};
                std::copy_n(state.xf_raw(), expected.size(), expected.begin());
                const unsigned src = bank + 1u, dst = bank + dst_offset;
                // Independent ordered-copy model, including forward alias
                // propagation. A memmove/snapshot is not equivalent here.
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

    // Dependency capture only needs the shape of guest-memory reads. Camera,
    // lighting, and matrix values change constantly but do not alter which
    // vertex arrays, display lists, TLUTs, or texture ranges are snapshotted.
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
    // Masked future writes observe selector bits even when current samples
    // are dead. Arithmetic/swap/z-format bits do not enter dependency shape.
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

int main() try {
    if (!test_display_list_variant_lookup_invalidation()) return 1;
    if (!test_xf_matrix_span_updates()) return 1;
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
        FifoParser parser;
        GxState state;
        TestSink sink;
        std::vector<std::byte> bytes;
        const auto bp_write = [&](std::uint8_t reg, std::uint32_t value) {
            bytes.push_back(std::byte{0x61u});
            bytes.push_back(static_cast<std::byte>(reg));
            bytes.push_back(static_cast<std::byte>(value >> 16u));
            bytes.push_back(static_cast<std::byte>(value >> 8u));
            bytes.push_back(static_cast<std::byte>(value));
        };
        bp_write(0x47u, 0x1234u);
        bp_write(0xFEu, 0xFFu);
        bp_write(0x47u, 0xABCDu);
        bp_write(0xFEu, 0u);
        bp_write(0x47u, 0u); // unchanged token write still emits callback
        bp_write(0x52u, 0x55u);
        bp_write(0xFEu, 1u);
        bp_write(0x52u, 0xAAu); // effective value is 0x54
        bp_write(0x45u, 6u); // low byte 6 does not mean finish
        (void)parser.run_available(bytes, nullptr, sink, state);
        if (!expect(sink.pe_tokens == 3 && sink.last_token == 0x12CDu &&
                    sink.efb_copies == 2 && sink.last_copy == 0x54u &&
                    sink.pe_finishes == 0,
                    "ordinary masked BP side effects used raw command")) return 1;
        bp_write(0x45u, 2u);
        // Cached CALL_DL replay has the same effective-value/repeat contract.
        // CALL_DL length is in 32-byte blocks. Pad with NOPs so the final BP
        // commands are inside the declared display-list window.
        bytes.resize((bytes.size() + 31u) & ~std::size_t{31u}, std::byte{0});
        std::array<std::byte, 512> guest{};
        std::copy(bytes.begin(), bytes.end(), guest.begin() + 0x100u);
        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size = static_cast<std::uint32_t>(guest.size());
        const std::array<std::byte, 9> call{
            std::byte{0x40}, std::byte{0x80}, std::byte{0}, std::byte{1}, std::byte{0},
            std::byte{0}, std::byte{0}, std::byte{0}, static_cast<std::byte>(bytes.size())};
        (void)parser.run_available(call, &memory, sink, state);
        (void)parser.run_available(call, &memory, sink, state);
        if (!expect(sink.pe_tokens == 9 && sink.last_token == 0x12CDu &&
                    sink.efb_copies == 6 && sink.last_copy == 0x54u &&
                    sink.pe_finishes == 2,
                    "cached masked BP side effects differ from ordinary")) return 1;
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
        // These writes change GPU uniforms, not shader programs. Exercise the
        // enable/type transitions separately so narrowing dirty bits cannot
        // accidentally reuse a pipeline when codegen actually changes.
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
        // Compare the cached dependency hash to a fresh state rebuilt from the
        // entire raw register file, after every classified write. This detects
        // a missed invalidation without adding counters to production code.
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
        // Every boundary in the backend's explicitly consumed XF inventory
        // remains accepted. Gaps between these ranges are intentionally not
        // part of the inventory.
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
        // XFMEM_ERROR..XFMEM_VTXSPECS (0x1000-0x1008): real-hardware
        // status/diagnostic registers confirmed inert for rendering output
        // (Dolphin's XFRegWritten treats 0x1000-0x1006 as literal no-ops,
        // 0x1007 as unhandled-but-inert, and 0x1008/VTXSPECS as an
        // internal-only consistency-check flag). Titan/strap boot writes
        // XFMEM_ERROR=0x1000 during GX init; this must not hard-fail or
        // mark any dependent state dirty.
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
        // CP 0x00/0x10/0x20: real-hardware registers of unknown purpose,
        // confirmed inert in Dolphin's own LoadCPReg reference (named
        // UNKNOWN_00/UNKNOWN_10/UNKNOWN_20 there too). Strap/boot GX init
        // writes register=0x20 value=0; this must not hard-fail or mark
        // any dependent state dirty.
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
        // BP 0x23/0x24 (BPMEM_PERF0_TRI/BPMEM_PERF0_QUAD): real-hardware
        // perf-query triangle/quad counters, confirmed inert for rendered
        // output in Dolphin's BPWritten reference (bare `return;`, no
        // TEV/shader/blend/rasterizer effect). Strap/boot GX init writes
        // register=0x23 value=0; this must not hard-fail or mark any
        // dependent state dirty.
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
        // XF 0x1013-0x1019 (XFMEM_UNKNOWN_GROUP_1 + SETMATRIXINDA/B): the
        // unknown-group registers are inert on real hardware, and this
        // backend's matrix-index consumers (vertex_loader.cpp,
        // shader_gen.cpp, gx_backend.cpp) exclusively read the CP-side
        // kMatrixIndexA/B copy, so the XF latch is write-only/unread here.
        // Strap/boot GX init writes register=0x1018; this must not
        // hard-fail or mark any dependent state dirty.
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
        // Addresses that the old high-nibble CP dispatch aliased into a known
        // register must now fail without changing the canonical register.
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
        for (const std::uint16_t base : {0x0000u, 0x0040u, 0x0400u, 0x0500u, 0x0600u, 0x1009u}) {
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
        std::array<std::byte, 512> guest{};
        const std::size_t dl_offset = 0x100;
        guest[dl_offset + 0] = std::byte{0x90};
        guest[dl_offset + 1] = std::byte{0x00};
        guest[dl_offset + 2] = std::byte{0x01};
        guest[dl_offset + 3] = std::byte{0xEE};

        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size =
            static_cast<std::uint32_t>(guest.size());

        FifoParserProfile profile;
        parser.set_profile(&profile);
        const std::array<std::byte, 9> call_dl{
            std::byte{0x40},
            std::byte{0x80}, std::byte{0x00},
            std::byte{0x01}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x20}};
        const std::size_t call_dl_consumed =
            parser.run_available(call_dl, &memory, sink, state);
        if (!expect(
                call_dl_consumed == call_dl.size(),
                "CALL_DL using GuestMemoryV1 fast_regions was not consumed") ||
            !expect(
                sink.draws == 3,
                "CALL_DL fast-region display list did not execute draw")) {
            return 1;
        }
        if (!expect(
                profile.call_dl_cache_misses == 1 &&
                profile.call_dl_cache_stores == 1 &&
                profile.call_dl_cache_hits == 0,
                "first CALL_DL did not populate the display-list cache")) {
            return 1;
        }

        const std::size_t cached_call_dl_consumed =
            parser.run_available(call_dl, &memory, sink, state);
        if (!expect(
                cached_call_dl_consumed == call_dl.size(),
                "cached CALL_DL fast-region display list was not consumed") ||
            !expect(
                sink.draws == 4,
                "cached CALL_DL did not replay draw")) {
            return 1;
        }
        if (!expect(
                profile.call_dl_cache_hits == 1,
                "second CALL_DL did not hit the display-list cache")) {
            return 1;
        }

        guest[dl_offset + 3] = std::byte{0xEF};
        const std::size_t changed_without_dirty_consumed =
            parser.run_available(call_dl, &memory, sink, state);
        if (!expect(
                changed_without_dirty_consumed == call_dl.size(),
                "changed CALL_DL without dirty notification was not consumed") ||
            !expect(
                sink.draws == 5,
                "changed CALL_DL without dirty notification did not execute draw")) {
            return 1;
        }
        if (!expect(
                profile.call_dl_cache_misses == 2 &&
                profile.call_dl_cache_stores == 2 &&
                profile.call_dl_cache_hits == 1,
                "changed CALL_DL bytes were replayed from a stale cache entry")) {
            return 1;
        }

        const std::size_t cached_changed_consumed =
            parser.run_available(call_dl, &memory, sink, state);
        if (!expect(
                cached_changed_consumed == call_dl.size(),
                "validated changed CALL_DL was not consumed") ||
            !expect(
                sink.draws == 6,
                "validated changed CALL_DL did not replay draw")) {
            return 1;
        }
        if (!expect(
                profile.call_dl_cache_hits == 2,
                "validated changed CALL_DL did not repopulate the cache")) {
            return 1;
        }

        const std::size_t non_overlapping_same_page_invalidated =
            parser.invalidate_display_list_cache_range(0x00000000u, 0x80u);
        if (!expect(
                non_overlapping_same_page_invalidated == 0u,
                "display-list page index invalidated a non-overlapping same-page range")) {
            return 1;
        }
        const std::size_t cached_after_unrelated_dirty_consumed =
            parser.run_available(call_dl, &memory, sink, state);
        if (!expect(
                cached_after_unrelated_dirty_consumed == call_dl.size(),
                "CALL_DL after unrelated same-page dirty range was not consumed") ||
            !expect(
                sink.draws == 7,
                "CALL_DL after unrelated same-page dirty range did not replay draw")) {
            return 1;
        }
        if (!expect(
                profile.call_dl_cache_hits == 3,
                "unrelated same-page dirty range should keep the cached CALL_DL")) {
            return 1;
        }

        guest[dl_offset + 3] = std::byte{0xF0};
        const std::size_t invalidated =
            parser.invalidate_display_list_cache_range(0x0000011Fu, 1u);
        if (!expect(
                invalidated == 1u,
                "display-list dirty physical range did not invalidate aliased cache entry")) {
            return 1;
        }
        const std::size_t changed_call_dl_consumed =
            parser.run_available(call_dl, &memory, sink, state);
        if (!expect(
                changed_call_dl_consumed == call_dl.size(),
                "changed CALL_DL display list was not consumed") ||
            !expect(
                sink.draws == 8,
                "changed CALL_DL did not execute draw")) {
            return 1;
        }
        if (!expect(
                profile.call_dl_cache_misses == 3 &&
                profile.call_dl_cache_stores == 3,
                "changed CALL_DL bytes did not invalidate the cache entry")) {
            return 1;
        }
        parser.set_profile(nullptr);
    }

    {
        std::array<std::byte, 512> guest{};
        const std::size_t dl_offset = 0x140;
        guest[dl_offset + 0] = std::byte{0x61};
        guest[dl_offset + 1] = std::byte{galaxy::gx::bp::kTlutSrcAddr};
        guest[dl_offset + 2] = std::byte{0x00};
        guest[dl_offset + 3] = std::byte{0x01};
        guest[dl_offset + 4] = std::byte{0x80};
        guest[dl_offset + 5] = std::byte{0x61};
        guest[dl_offset + 6] = std::byte{galaxy::gx::bp::kTlutDest};
        guest[dl_offset + 7] = std::byte{0x00};
        guest[dl_offset + 8] = std::byte{0x40};
        guest[dl_offset + 9] = std::byte{0x04};

        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size =
            static_cast<std::uint32_t>(guest.size());

        FifoParser tlut_dl_parser;
        FifoParserProfile tlut_dl_profile;
        tlut_dl_parser.set_profile(&tlut_dl_profile);
        TestSink tlut_dl_sink;
        GxState tlut_dl_state;
        const std::array<std::byte, 9> call_dl{
            std::byte{0x40},
            std::byte{0x80}, std::byte{0x00},
            std::byte{0x01}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x20}};

        const std::size_t first_tlut_dl_consumed =
            tlut_dl_parser.run_available(
                call_dl, &memory, tlut_dl_sink, tlut_dl_state);
        const std::size_t cached_tlut_dl_consumed =
            tlut_dl_parser.run_available(
                call_dl, &memory, tlut_dl_sink, tlut_dl_state);
        if (!expect(
                first_tlut_dl_consumed == call_dl.size() &&
                cached_tlut_dl_consumed == call_dl.size(),
                "TLUT CALL_DL was not fully consumed") ||
            !expect(
                tlut_dl_sink.tlut_loads == 2,
                "TLUT load did not execute during live parse and cached replay") ||
            !expect(
                tlut_dl_state.bp(galaxy::gx::bp::kTlutSrcAddr) == 0x000180u &&
                tlut_dl_state.bp(galaxy::gx::bp::kTlutDest) == 0x004004u,
                "TLUT CALL_DL did not preserve latched BP register state") ||
            !expect(
                tlut_dl_profile.call_dl_cache_misses == 1 &&
                tlut_dl_profile.call_dl_cache_stores == 1 &&
                tlut_dl_profile.call_dl_cache_hits == 1,
                "TLUT CALL_DL did not replay from the display-list cache")) {
            return 1;
        }
        tlut_dl_parser.set_profile(nullptr);
    }

    {
        std::array<std::byte, 512> guest{};
        const std::size_t dl_offset = 0x100;
        guest[dl_offset + 0] = std::byte{0x90};
        guest[dl_offset + 1] = std::byte{0x00};
        guest[dl_offset + 2] = std::byte{0x01};
        guest[dl_offset + 3] = std::byte{0xAA};
        guest[dl_offset + 4] = std::byte{0x90};
        guest[dl_offset + 5] = std::byte{0x00};
        guest[dl_offset + 6] = std::byte{0x01};
        guest[dl_offset + 7] = std::byte{0xBB};

        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size =
            static_cast<std::uint32_t>(guest.size());

        FifoParser run_parser;
        TestSink run_sink;
        GxState run_state;
        const std::array<std::byte, 9> call_dl{
            std::byte{0x40},
            std::byte{0x80}, std::byte{0x00},
            std::byte{0x01}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x20}};

        const std::size_t first_consumed =
            run_parser.run_available(call_dl, &memory, run_sink, run_state);
        run_sink.enable_cached_draw_runs = true;
        const std::size_t second_consumed =
            run_parser.run_available(call_dl, &memory, run_sink, run_state);
        if (!expect(
                first_consumed == call_dl.size() &&
                second_consumed == call_dl.size(),
                "two-draw CALL_DL was not fully consumed") ||
            !expect(
                run_sink.draws == 4,
                "two-draw CALL_DL did not execute both live and cached draws") ||
            !expect(
                run_sink.cached_draw_run_begins == 1 &&
                run_sink.cached_draw_run_ends == 1,
                "cached two-draw CALL_DL did not bracket one draw run") ||
            !expect(
                run_sink.last_cached_draw_run_primitive ==
                    static_cast<PrimitiveClass>(0x90u >> 3u) &&
                run_sink.last_cached_draw_run_vtxfmt == 0u &&
                run_sink.last_cached_draw_run_count == 2u,
                "cached draw run metadata did not match the display list")) {
            return 1;
        }
    }

    {
        std::array<std::byte, 512> guest{};
        const std::size_t dl_offset = 0x100;
        guest[dl_offset + 0] = std::byte{0x90};
        guest[dl_offset + 1] = std::byte{0x00};
        guest[dl_offset + 2] = std::byte{0x01};
        guest[dl_offset + 3] = std::byte{0xAA};
        guest[dl_offset + 4] = std::byte{0x90};
        guest[dl_offset + 5] = std::byte{0x00};
        guest[dl_offset + 6] = std::byte{0x01};
        guest[dl_offset + 7] = std::byte{0xBB};

        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size =
            static_cast<std::uint32_t>(guest.size());

        FifoParser prepared_parser;
        FifoParserProfile prepared_profile;
        prepared_parser.set_profile(&prepared_profile);
        TestSink prepared_sink;
        GxState prepared_state;
        const std::array<std::byte, 9> call_dl{
            std::byte{0x40},
            std::byte{0x80}, std::byte{0x00},
            std::byte{0x01}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x20}};

        (void)prepared_parser.run_available(
            call_dl, &memory, prepared_sink, prepared_state);
        prepared_sink.enable_prepared_draw_runs = true;
        (void)prepared_parser.run_available(
            call_dl, &memory, prepared_sink, prepared_state);

        const std::vector<std::byte> expected_payload{};
        if (!expect(
                prepared_sink.draws == 4,
                "prepared cached draw run did not preserve source vertices") ||
            !expect(
                prepared_sink.prepared_draw_run_calls == 0,
                "incomplete triangle packets were concatenated") ||
            !expect(
                prepared_sink.last_prepared_source_draw_count == 0u,
                "incomplete triangle run reached prepared hook") ||
            !expect(
                prepared_sink.last_prepared_payload == expected_payload,
                "incomplete triangle payload was joined") ||
            !expect(
                prepared_profile.call_dl_replay_prepared_run_count == 0u &&
                prepared_profile
                    .call_dl_replay_prepared_source_draw_count == 0u &&
                prepared_profile
                    .call_dl_replay_prepared_payload_bytes ==
                    expected_payload.size(),
                "prepared cached draw run profile counters were not recorded")) {
            return 1;
        }
        prepared_parser.set_profile(nullptr);
    }

    {
        std::array<std::byte, 512> guest{};
        const std::size_t dl_offset = 0x100;
        guest[dl_offset + 0] = std::byte{0x98};
        guest[dl_offset + 1] = std::byte{0x00};
        guest[dl_offset + 2] = std::byte{0x04};
        guest[dl_offset + 3] = std::byte{0xAA};
        guest[dl_offset + 4] = std::byte{0xAB};
        guest[dl_offset + 5] = std::byte{0xAC};
        guest[dl_offset + 6] = std::byte{0xAD};
        guest[dl_offset + 7] = std::byte{0x98};
        guest[dl_offset + 8] = std::byte{0x00};
        guest[dl_offset + 9] = std::byte{0x04};
        guest[dl_offset + 10] = std::byte{0xBA};
        guest[dl_offset + 11] = std::byte{0xBB};
        guest[dl_offset + 12] = std::byte{0xBC};
        guest[dl_offset + 13] = std::byte{0xBD};

        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size =
            static_cast<std::uint32_t>(guest.size());

        FifoParser packet_parser;
        FifoParserProfile packet_profile;
        packet_parser.set_profile(&packet_profile);
        TestSink packet_sink;
        GxState packet_state;
        const std::array<std::byte, 9> call_dl{
            std::byte{0x40},
            std::byte{0x80}, std::byte{0x00},
            std::byte{0x01}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x20}};

        (void)packet_parser.run_available(
            call_dl, &memory, packet_sink, packet_state);
        packet_sink.enable_packet_draw_runs = true;
        (void)packet_parser.run_available(
            call_dl, &memory, packet_sink, packet_state);

        if (!expect(
                packet_sink.draws == 4,
                "packet cached draw run did not replay every strip packet") ||
            !expect(
                packet_sink.packet_draw_run_calls == 1,
                "packet cached draw run hook was not used") ||
            !expect(
                packet_sink.prepared_draw_run_calls == 0,
                "non-concatenatable strip run used prepared concatenation") ||
            !expect(
                packet_sink.last_packet_primitive ==
                    PrimitiveClass::TriangleStrip &&
                packet_sink.last_packet_vtxfmt == 0u &&
                packet_sink.last_packet_source_draw_count == 2u,
                "packet cached draw run metadata did not match strip packets") ||
            !expect(
                packet_sink.last_packet_cache_token != 0u &&
                packet_sink.last_packet_run_index == 0u,
                "packet cached draw run identity was not reported") ||
            !expect(
                packet_sink.last_packet_total_vertices == 8u &&
                packet_sink.last_packet_total_indices == 12u &&
                packet_sink.last_packet_precomputed_indices == 12u,
                "packet cached draw run did not precompute strip indices") ||
            !expect(
                packet_profile.call_dl_replay_packet_run_count == 1u &&
                packet_profile.call_dl_replay_packet_source_draw_count == 2u,
                "packet cached draw run profile counters were not recorded")) {
            return 1;
        }
        packet_parser.set_profile(nullptr);
    }

    {
        std::array<std::byte, 512> guest{};
        const std::size_t dl_offset = 0x100;
        guest[dl_offset + 0] = std::byte{0x90};
        guest[dl_offset + 1] = std::byte{0x00};
        guest[dl_offset + 2] = std::byte{0x01};
        guest[dl_offset + 3] = std::byte{0xEE};

        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size =
            static_cast<std::uint32_t>(guest.size());

        FifoParser dependency_parser;
        GxState dependency_state;
        TestSink dependency_sink;
        FifoParserProfile dependency_profile;
        dependency_parser.set_profile(&dependency_profile);

        dependency_state.load_cp(galaxy::gx::cp::kVcdLo, 0x11u);
        dependency_state.load_cp(galaxy::gx::cp::kVcdHi, 0x22u);
        dependency_state.load_cp(galaxy::gx::cp::kVatABase + 0u, 0x33u);
        dependency_state.load_cp(galaxy::gx::cp::kVatBBase + 0u, 0x44u);
        dependency_state.load_cp(galaxy::gx::cp::kVatCBase + 0u, 0x55u);

        const std::array<std::byte, 9> call_dl{
            std::byte{0x40},
            std::byte{0x80}, std::byte{0x00},
            std::byte{0x01}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x20}};

        (void)dependency_parser.run_available(
            call_dl, &memory, dependency_sink, dependency_state);
        dependency_state.load_cp(galaxy::gx::cp::kVatABase + 1u, 0x777u);
        (void)dependency_parser.run_available(
            call_dl, &memory, dependency_sink, dependency_state);
        if (!expect(
                dependency_profile.call_dl_cache_hits == 1 &&
                dependency_profile.call_dl_cache_misses == 1,
                "irrelevant VAT state change should still hit cached CALL_DL")) {
            return 1;
        }

        dependency_state.load_cp(galaxy::gx::cp::kVatABase + 0u, 0x888u);
        (void)dependency_parser.run_available(
            call_dl, &memory, dependency_sink, dependency_state);
        if (!expect(
                dependency_profile.call_dl_cache_misses == 2 &&
                dependency_profile.call_dl_cache_stores == 2,
                "relevant VAT state change should create a CALL_DL variant")) {
            return 1;
        }

        dependency_parser.set_profile(nullptr);
    }

    {
        std::array<std::byte, 512> guest{};
        const std::size_t dl_offset = 0x100;
        guest[dl_offset + 0] = std::byte{0x08};
        guest[dl_offset + 1] = std::byte{galaxy::gx::cp::kVcdLo};
        guest[dl_offset + 2] = std::byte{0x00};
        guest[dl_offset + 3] = std::byte{0x00};
        guest[dl_offset + 4] = std::byte{0x00};
        guest[dl_offset + 5] = std::byte{0x77};
        guest[dl_offset + 6] = std::byte{0x90};
        guest[dl_offset + 7] = std::byte{0x00};
        guest[dl_offset + 8] = std::byte{0x01};
        guest[dl_offset + 9] = std::byte{0xEE};

        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size =
            static_cast<std::uint32_t>(guest.size());

        FifoParser internal_cp_parser;
        GxState internal_cp_state;
        TestSink internal_cp_sink;
        FifoParserProfile internal_cp_profile;
        internal_cp_parser.set_profile(&internal_cp_profile);
        internal_cp_state.load_cp(galaxy::gx::cp::kVatABase + 0u, 0x33u);
        internal_cp_state.load_cp(galaxy::gx::cp::kVatBBase + 0u, 0x44u);
        internal_cp_state.load_cp(galaxy::gx::cp::kVatCBase + 0u, 0x55u);

        const std::array<std::byte, 9> call_dl{
            std::byte{0x40},
            std::byte{0x80}, std::byte{0x00},
            std::byte{0x01}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x20}};

        (void)internal_cp_parser.run_available(
            call_dl, &memory, internal_cp_sink, internal_cp_state);
        internal_cp_state.load_cp(galaxy::gx::cp::kVcdLo, 0x999u);
        (void)internal_cp_parser.run_available(
            call_dl, &memory, internal_cp_sink, internal_cp_state);
        if (!expect(
                internal_cp_profile.call_dl_cache_hits == 1 &&
                internal_cp_profile.call_dl_cache_misses == 1,
                "CALL_DL that writes VCD internally should not depend on entry VCD")) {
            return 1;
        }

        internal_cp_parser.set_profile(nullptr);
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
