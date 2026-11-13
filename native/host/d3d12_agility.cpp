// SPDX-License-Identifier: GPL-3.0-only
// The DirectX 12 Agility SDK runtime (cmake/agility_sdk.cmake) is opened by the renderer through a device factory
// (renderer_d3d12.cpp agility_device_factory), not asked for by export: an exported D3D12SDKVersion makes every
// Direct3D 12 device fail where D3D12\ is missing (l62). Here only the check that the headers are the SDK's.
#include <d3d12.h>

static_assert(D3D12_SDK_VERSION >= 620, "the Agility SDK's headers (cmake/agility_sdk.cmake) come before the Windows SDK's");
