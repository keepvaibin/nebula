// SPDX-License-Identifier: GPL-3.0-only
// Where only FSR 3's upscaler is built (Android): the backend's interface names frame generation's swap chain, which never runs.
#include <FidelityFX/host/ffx_interface.h>

extern "C" FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) {
    return FFX_ERROR_BACKEND_API_ERROR;
}
