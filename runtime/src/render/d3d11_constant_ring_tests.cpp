// The constant ring (d3d11_context.h, P-242) on this machine's Direct3D 11 device: every piece is read by its own queued
// dispatch as it was written, through a frame that hands out more than the largest ring holds (the ring grows, then
// waits on the fence it signals itself) and through many frames that reuse it; a piece's rounding is zeroed over bytes
// earlier pieces left there. Without a hardware Direct3D 11 device it says so and passes nothing else.
#include "galaxy/render/d3d11/d3d11_context.h"

#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using galaxy::render::d3d11::ComPtr;
using galaxy::render::d3d11::Context;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// One 64 KB piece (the most one binding reads): its number at the start, the middle and the end. The dispatch writes the
// number into its own word when all three agree, else ~0.
constexpr char kWholePiece[] =
    "cbuffer C : register(b0) { uint4 words[4096]; };\n"
    "RWStructuredBuffer<uint> answers : register(u0);\n"
    "[numthreads(1, 1, 1)] void main() {\n"
    "    const uint n = words[0].x;\n"
    "    answers[n] = (words[2048].y == (n ^ 0xA5A5A5A5u) && words[4095].w == n) ? n : 0xFFFFFFFFu;\n"
    "}\n";
// One 256-byte piece: its first word into answers[0], every other word OR-ed into answers[1].
constexpr char kRounding[] =
    "cbuffer C : register(b0) { uint4 words[16]; };\n"
    "RWStructuredBuffer<uint> answers : register(u0);\n"
    "[numthreads(1, 1, 1)] void main() {\n"
    "    uint rest = words[0].y | words[0].z | words[0].w;\n"
    "    [unroll] for (uint i = 1; i < 16; ++i) rest |= words[i].x | words[i].y | words[i].z | words[i].w;\n"
    "    answers[0] = words[0].x;\n"
    "    answers[1] = rest;\n"
    "}\n";

ComPtr<ID3D11ComputeShader> compile(ID3D11Device1* device, const char* source, std::size_t size) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    ComPtr<ID3D11ComputeShader> shader;
    if (FAILED(D3DCompile(source, size - 1u, "constant_ring_tests", nullptr, nullptr, "main", "cs_5_0", 0u, 0u, &code,
                          &errors)) ||
        FAILED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader))) {
        std::cerr << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no shader") << '\n';
        return nullptr;
    }
    return shader;
}

struct Answers {
    ComPtr<ID3D11Buffer> buffer;
    ComPtr<ID3D11UnorderedAccessView> view;
    ComPtr<ID3D11Buffer> readback;
    std::uint32_t count = 0u;
};

bool make_answers(ID3D11Device1* device, std::uint32_t count, Answers& out) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = count * 4u;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = 4u;
    D3D11_BUFFER_DESC back{};
    back.ByteWidth = count * 4u;
    back.Usage = D3D11_USAGE_STAGING;
    back.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    out.count = count;
    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &out.buffer)) &&
           SUCCEEDED(device->CreateUnorderedAccessView(out.buffer.Get(), nullptr, &out.view)) &&
           SUCCEEDED(device->CreateBuffer(&back, nullptr, &out.readback));
}

// The answers after every queued command has run.
std::vector<std::uint32_t> read_answers(Context& context, const Answers& answers) {
    ID3D11DeviceContext1* const immediate = context.immediate();
    immediate->CopyResource(answers.readback.Get(), answers.buffer.Get());
    (void)context.wait(context.signal());
    std::vector<std::uint32_t> values;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(immediate->Map(answers.readback.Get(), 0u, D3D11_MAP_READ, 0u, &mapped))) {
        const auto* words = static_cast<const std::uint32_t*>(mapped.pData);
        values.assign(words, words + answers.count);
        immediate->Unmap(answers.readback.Get(), 0u);
    }
    return values;
}

// `frames` frames of `per_frame` 64 KB pieces, each read by its own dispatch; a frame ends with the fence's signal.
bool whole_pieces(Context& context, ID3D11ComputeShader* shader, std::uint32_t frames, std::uint32_t per_frame,
                  const char* what) {
    const std::uint32_t total = frames * per_frame;
    Answers answers;
    if (!make_answers(context.device(), total, answers)) {
        return expect(false, "the answers' buffers");
    }
    ID3D11DeviceContext1* const immediate = context.immediate();
    const UINT ones[4] = {0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu};
    immediate->ClearUnorderedAccessViewUint(answers.view.Get(), ones);
    immediate->CSSetShader(shader, nullptr, 0u);
    ID3D11UnorderedAccessView* const view = answers.view.Get();
    immediate->CSSetUnorderedAccessViews(0u, 1u, &view, nullptr);
    std::uint32_t n = 0u;
    for (std::uint32_t frame = 0u; frame < frames; ++frame) {
        for (std::uint32_t i = 0u; i < per_frame; ++i, ++n) {
            Context::ConstantPlace place;
            std::byte* const piece = context.begin_constants(65536u, place);
            if (piece == nullptr) {
                return expect(false, what);
            }
            const std::uint32_t mixed = n ^ 0xA5A5A5A5u;
            std::memcpy(piece, &n, 4u);
            std::memcpy(piece + 2048u * 16u + 4u, &mixed, 4u);
            std::memcpy(piece + 65536u - 4u, &n, 4u);
            context.end_constants();
            const galaxy::render::d3d11::ComputeConstants bound(immediate, place);
            immediate->Dispatch(1u, 1u, 1u);
        }
        (void)context.signal();
    }
    const std::vector<std::uint32_t> values = read_answers(context, answers);
    std::uint32_t wrong = values.size() == total ? 0u : total;
    for (std::uint32_t i = 0u; i < values.size(); ++i) {
        wrong += values[i] != i ? 1u : 0u;
    }
    if (wrong != 0u) {
        std::cerr << what << ": " << wrong << " of " << total << " pieces read wrong\n";
    }
    return expect(wrong == 0u, what);
}

bool rounding_zeroed(Context& context, ID3D11ComputeShader* shader) {
    Answers answers;
    if (!make_answers(context.device(), 2u, answers)) {
        return expect(false, "the answers' buffers");
    }
    ID3D11DeviceContext1* const immediate = context.immediate();
    immediate->CSSetShader(shader, nullptr, 0u);
    ID3D11UnorderedAccessView* const view = answers.view.Get();
    immediate->CSSetUnorderedAccessViews(0u, 1u, &view, nullptr);
    // Every place has held numbers by now (the pieces before): a 4-byte put's other 252 bytes read zero.
    const std::uint32_t value = 0x13572468u;
    const Context::ConstantPlace place = context.put_constants(&value, sizeof(value));
    bool ok = expect(place.buffer != nullptr && place.count == 16u, "a 4-byte put is one 256-byte piece");
    {
        const galaxy::render::d3d11::ComputeConstants bound(immediate, place);
        immediate->Dispatch(1u, 1u, 1u);
    }
    const std::vector<std::uint32_t> values = read_answers(context, answers);
    ok &= expect(values.size() == 2u && values[0] == value, "the put's value is read");
    ok &= expect(values.size() == 2u && values[1] == 0u, "the rest of its piece reads zero");
    return ok;
}

}  // namespace

int main() {
    Context& context = Context::get();
    if (!context.initialize(std::string{}, false)) {
        std::cout << "no Direct3D 11 device (" << context.failure() << "): the constant ring is not tested here\n";
        return 0;
    }
    bool passed = true;
    const ComPtr<ID3D11ComputeShader> whole = compile(context.device(), kWholePiece, sizeof(kWholePiece));
    const ComPtr<ID3D11ComputeShader> rounding = compile(context.device(), kRounding, sizeof(kRounding));
    passed &= expect(whole != nullptr && rounding != nullptr, "the tests' shaders compile");
    if (passed) {
        // One frame past the largest ring (128 MB is 2048 pieces of 64 KB): it grows, then waits on its own signal.
        passed &= whole_pieces(context, whole.Get(), 1u, 2600u, "one frame larger than the largest ring");
        // Many frames, the ring reused behind the fence.
        passed &= whole_pieces(context, whole.Get(), 60u, 150u, "frames reusing the ring");
        passed &= rounding_zeroed(context, rounding.Get());
    }
    context.shutdown();
    std::cout << (passed ? "constant ring tests passed\n" : "constant ring tests FAILED\n");
    return passed ? 0 : 1;
}
