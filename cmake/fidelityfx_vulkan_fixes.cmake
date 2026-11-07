# FidelityFX SDK 1.1.4 creates FP16 luma history but its Vulkan shader declares rgba8.
# https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/issues/161
# Patch a private SDK copy so external dependency caches and other builds remain untouched.
function(nebula_fidelityfx_vulkan_sdk source destination result)
    set(callback "include/FidelityFX/gpu/fsr3upscaler/ffx_fsr3upscaler_callbacks_glsl.h")
    if(NOT EXISTS "${destination}/${callback}")
        file(MAKE_DIRECTORY "${destination}")
        file(COPY "${source}/" DESTINATION "${destination}")
    endif()
    file(READ "${source}/${callback}" contents)
    string(REPLACE "FSR3UPSCALER_BIND_UAV_LUMA_HISTORY, rgba8)"
                   "FSR3UPSCALER_BIND_UAV_LUMA_HISTORY, rgba16f)" contents "${contents}")
    file(WRITE "${destination}/${callback}.new" "${contents}")
    configure_file("${destination}/${callback}.new" "${destination}/${callback}" COPYONLY)
    set(${result} "${destination}" PARENT_SCOPE)
endfunction()
