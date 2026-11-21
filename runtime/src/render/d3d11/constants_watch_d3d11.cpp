// SPDX-License-Identifier: GPL-3.0-only
// The constants watch (constants_watch_d3d11.h, ledger P-332).
#include "galaxy/render/d3d11/constants_watch_d3d11.h"

#include <cstdio>
#include <cstring>

namespace galaxy::render::d3d11 {

bool ConstantsWatch::initialize(ID3D11Device1* device, std::span<const unsigned char> shader) {
    shader_.Reset();
    reads_ = {};
    if (device == nullptr || FAILED(device->CreateComputeShader(shader.data(), shader.size(), nullptr, &shader_))) {
        shader_.Reset();
        return false;
    }
    D3D11_BUFFER_DESC seen{};
    seen.ByteWidth = 4u * sizeof(std::uint32_t);
    seen.Usage = D3D11_USAGE_DEFAULT;
    seen.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    seen.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    seen.StructureByteStride = sizeof(std::uint32_t);
    D3D11_BUFFER_DESC back{};
    back.ByteWidth = seen.ByteWidth;
    back.Usage = D3D11_USAGE_STAGING;
    back.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (Read& read : reads_) {
        if (FAILED(device->CreateBuffer(&seen, nullptr, &read.seen)) ||
            FAILED(device->CreateUnorderedAccessView(read.seen.Get(), nullptr, &read.seen_view)) ||
            FAILED(device->CreateBuffer(&back, nullptr, &read.readback))) {
            shader_.Reset();
            reads_ = {};
            return false;
        }
    }
    return true;
}

ConstantsWatch::Read* ConstantsWatch::free_read() noexcept {
    for (Read& read : reads_) {
        if (read.fence == 0u) {
            return &read;
        }
    }
    return nullptr;
}

void ConstantsWatch::frame(ID3D11DeviceContext1* context) {
    if (!ready()) {
        return;
    }
    (void)check(context);
    if (free_read() != nullptr) {
        const Context::ConstantPlace place = write();
        (void)record(context, place);
    }
}

unsigned ConstantsWatch::check(ID3D11DeviceContext1* context) {
    unsigned found = 0u;
    Context& device = Context::get();
    const std::uint64_t completed = device.completed_value();
    for (Read& read : reads_) {
        if (read.fence == 0u || read.fence > completed) {
            continue;
        }
        // Its fence has passed, so the copy has run and the Map does not wait for the GPU (with DO_NOT_WAIT the runtime
        // can still answer WAS_STILL_DRAWING here, which would leave a wrong read unread for a frame or more).
        read.fence = 0u;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(read.readback.Get(), 0u, D3D11_MAP_READ, 0u, &mapped))) {
            continue;  // a lost device: the renderer reports it
        }
        std::uint32_t words[3] = {};
        std::memcpy(words, mapped.pData, sizeof(words));
        context->Unmap(read.readback.Get(), 0u);
        ++compared_;
        const Words seen{words[0], words[1], words[2]};
        if (seen == read.written) {
            continue;
        }
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[constants] watch: wrote number %u generation %u offset %u, read number %u generation %u offset %u "
                      "(ring generation now %u)",
                      read.written.number, read.written.generation, read.written.offset, seen.number, seen.generation,
                      seen.offset, device.ring_generation());
        last_line_ = line;
        std::fprintf(stderr, "%s\n", line);
        ++found;
    }
    differences_ += found;
    return found;
}

Context::ConstantPlace ConstantsWatch::write() {
    Context::ConstantPlace place;
    if (!ready() || free_read() == nullptr) {
        return place;
    }
    Context& device = Context::get();
    std::byte* const at = device.begin_constants(3u * sizeof(std::uint32_t), place);
    if (at == nullptr) {
        return {};
    }
    // The generation after begin_constants: a piece that made a larger ring is that ring's.
    piece_ = {++number_, device.ring_generation(), place.first * 16u};
    const std::uint32_t words[3] = {piece_.number, piece_.generation, piece_.offset};
    std::memcpy(at, words, sizeof(words));
    std::memset(at + sizeof(words), 0, static_cast<std::size_t>(place.count) * 16u - sizeof(words));
    device.end_constants();
    piece_written_ = true;
    return place;
}

bool ConstantsWatch::record(ID3D11DeviceContext1* context, const Context::ConstantPlace& place) {
    Read* const read = free_read();
    if (!ready() || !piece_written_ || place.buffer == nullptr || read == nullptr) {
        return false;
    }
    piece_written_ = false;
    context->CSSetShader(shader_.Get(), nullptr, 0u);
    ID3D11UnorderedAccessView* const view = read->seen_view.Get();
    context->CSSetUnorderedAccessViews(0u, 1u, &view, nullptr);
    {
        const ComputeConstants bound(context, place);
        context->Dispatch(1u, 1u, 1u);
    }
    ID3D11UnorderedAccessView* const none = nullptr;
    context->CSSetUnorderedAccessViews(0u, 1u, &none, nullptr);
    context->CopyResource(read->readback.Get(), read->seen.Get());
    read->written = piece_;
    read->fence = Context::get().submitted_value() + 1u;  // the next signal covers the commands recorded now
    return true;
}

}  // namespace galaxy::render::d3d11
