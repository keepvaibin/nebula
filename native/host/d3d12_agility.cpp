// SPDX-License-Identifier: GPL-3.0-only
// The DirectX 12 Agility SDK runtime this program asks Windows for (cmake/agility_sdk.cmake): the version of the headers
// it was built with, from D3D12\ beside the program. Without that folder Windows' own runtime runs.
#include <d3d12.h>

static_assert(D3D12_SDK_VERSION >= 620, "the Agility SDK's headers (cmake/agility_sdk.cmake) come before the Windows SDK's");

extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION;
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
