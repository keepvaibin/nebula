// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Ray tracing's shader preparation. A cold compile of the ray tracing pipelines takes over a minute (the clear pass
// alone 62 s), so they are kept in a pipeline library (rt-pipelines.bin in the shader cache folder) that every start
// loads them from.
//
// - The library is prepared once a start has compiled every pipeline of this build, at every quality, for this adapter
//   and driver. Its header names the shader set (rt_shader_manifest.h's fingerprint), the adapter and the driver; the
//   D3D12 runtime refuses a library from another adapter or driver as well.
// - A start whose library is not prepared runs before the title (the game's NebulaRtPrepare screen). Turning ray
//   tracing on in the game while it is not prepared asks for a restart instead.
// - The meter weighs each shader by its size to the power 1.66 (the measured compile cost).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace galaxy::gx::rt_prepare {

// What a library is for: the adapter (vendor:device:subsystem:revision, hex) and its user-mode driver's version
// (a.b.c.d; empty when the adapter does not tell it, which never matches a recorded one).
struct Identity {
    std::string adapter;
    std::string driver;
};

// The library's header.
struct Header {
    std::uint64_t shaders = 0u;  // the shader set's fingerprint
    std::string adapter;
    std::string driver;
    bool complete = false;       // every pipeline of the shader set is in it
};
inline constexpr std::size_t kHeaderBytes = 112u;
// The header as stored, followed by `blob_bytes` of the D3D12 library.
[[nodiscard]] std::vector<std::uint8_t> format_header(const Header& header, std::uint64_t blob_bytes);
// A stored header (the file's first kHeaderBytes): false when it is not one of this version's.
[[nodiscard]] bool parse_header(std::span<const std::uint8_t> bytes, Header& out, std::uint64_t& blob_bytes);
// Whether a library with `header` is prepared for this build's shaders (`shaders`) on `identity`.
[[nodiscard]] bool header_covers(const Header& header, std::uint64_t shaders, const Identity& identity);

// The library on disk: its header read as the renderer starts (on a GPU that runs ray tracing), the library itself
// opened by the first ray tracer's start, on its device (the D3D12 runtime may refuse it then).
void identify(const Identity& identity);
void open(ID3D12Device* device);
// Every ray tracing pipeline loads from the library (as far as known: its header, then the library itself).
[[nodiscard]] bool prepared() noexcept;
// The renderer's first start (ray tracing on, or the preparation requested): Opening while it opens the library,
// Compiling while it compiles (the title waits), then None.
enum class FirstStart : unsigned { None, Opening, Compiling };
void set_first_start(FirstStart phase) noexcept;
[[nodiscard]] FirstStart first_start() noexcept;
// Setup's or the launcher's request (rt-prepare.request beside config.ini): prepare at this start whatever the RAY
// TRACING setting. Read once; withdraw_request() removes it (prepared, or ray tracing is not offered).
[[nodiscard]] bool preparation_requested();
void withdraw_request();

// The ray tracer's root signatures and pipelines, the pipelines from the library or compiled into it. A pipeline's
// name in the library is made of its shaders' digests, its root signature's serialized form and, for graphics, its
// fixed state, so a pipeline any of them changed is another pipeline.
[[nodiscard]] bool root_signature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc,
                                  Microsoft::WRL::ComPtr<ID3D12RootSignature>& out);
[[nodiscard]] bool compute(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc,
                           Microsoft::WRL::ComPtr<ID3D12PipelineState>& out);
[[nodiscard]] bool graphics(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                            Microsoft::WRL::ComPtr<ID3D12PipelineState>& out);
// A start's end. `complete`: it made every pipeline of the shader set (a start while not prepared, which makes every
// quality's). Saves the library when it has pipelines it did not have.
void finish_start(bool complete);
// Pipelines compiled (not loaded from the library) by this process.
[[nodiscard]] unsigned compiled() noexcept;

// The meter. A shader is known by its DXIL container's digest; one outside the manifest is not counted.
void reset() noexcept;
// 0 to 1, never going back; 1 only once every counted shader is finished.
[[nodiscard]] float progress() noexcept;

// The library's file and Setup's request file.
[[nodiscard]] std::filesystem::path library_path();
[[nodiscard]] std::filesystem::path request_path();

}  // namespace galaxy::gx::rt_prepare
