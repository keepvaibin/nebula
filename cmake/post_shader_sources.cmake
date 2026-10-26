# post_process.hlsl and the vendored SMAA and FidelityFX FSR 1 sources as NUL-terminated byte arrays (galaxy_post_*),
# for the Vulkan renderer: vk/post_process_vk.cpp composes each pass from them as the D3D12 build composes its fxc
# inputs, and compiles it on the device. Editing a source re-runs configure.
function(galaxy_embed_post_sources output root)
    set(body "")
    foreach(entry
            "post_process_hlsl=runtime/src/gx/post_process.hlsl"
            "game_bloom_hlsl=runtime/src/gx/game_bloom.hlsl"
            "smaa_hlsl=third_party/smaa/SMAA.hlsl"
            "ffx_a_h=third_party/fidelityfx-fsr1/ffx_a.h"
            "ffx_fsr1_h=third_party/fidelityfx-fsr1/ffx_fsr1.h")
        string(REPLACE "=" ";" entry "${entry}")
        list(GET entry 0 name)
        list(GET entry 1 path)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${root}/${path}")
        file(READ "${root}/${path}" hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
        string(APPEND body "extern const unsigned char galaxy_post_${name}[] = {${hex}0x00};\n")
    endforeach()
    file(CONFIGURE OUTPUT "${output}" CONTENT "namespace galaxy::gx {\n${body}}\n" @ONLY)
endfunction()
