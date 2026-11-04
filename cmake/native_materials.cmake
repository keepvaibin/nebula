# The native materials (E-04; runtime/src/render/materials/<name>.hlsl with native_material.hlsli), compiled at build in
# every output variant. galaxy_native_materials lists the hand-written ones in NebulaShaderId order (nebula_game_api.h,
# from 1); runtime/src/render/materials/j3d/list.cmake (tools/dev/gen_j3d_materials.py) the J3D material set
# (NebulaShaderId NEBULA_SHADER_J3D_FIRST + n - 1). galaxy_native_material_sources(OUT_VAR BACKEND) adds the build
# rules of BACKEND (dxil: dxc; spirv: the galaxy_native_spirv tool) and sets OUT_VAR to the generated headers and table
# source to add to a target.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(galaxy_native_materials whirlpool ocean_bowl screen_blur flat_color clip_fill big_bubble
    whirlpool_accelerator spin_driver_path spider_thread tex_modulate water_plant ocean_ring_pipe_inside ocean_sphere
    ocean_sphere_env ocean_ring ocean_ring_bloom water_road ocean_ring_pipe_outside color_tex_alpha
    vertex_color dd_texture dd_texture_color tex_vertex_alpha tex_alpha_lerp
    trample_star tex_const_alpha font_text j2d_picture fur particle ocean_bowl_bloom mii_face mii_texture image_effect gaussian_blur dof_final normal_map
    movie water_camera frame_ripple frame_depth)
set(galaxy_native_variants base reactive layer motion sample pixel pixel_sample
    pixel_reactive pixel_layer pixel_motion)
set(galaxy_native_defines_base "")
set(galaxy_native_defines_reactive "NATIVE_REACTIVE")
set(galaxy_native_defines_layer "NATIVE_REACTIVE;NATIVE_LAYER")
set(galaxy_native_defines_motion "NATIVE_MOTION")
# An alpha-tested material under MSAA: only the pixel stage differs (its inputs per sample), the vertex stage is base's.
set(galaxy_native_defines_sample "NATIVE_SAMPLE")
# Lit at the pixel, plainly or enhanced by a constant (j3d_material.hlsli): one variant for both modes.
set(galaxy_native_defines_pixel "NATIVE_PIXEL_LIT")
set(galaxy_native_defines_pixel_sample "NATIVE_PIXEL_LIT;NATIVE_SAMPLE")
set(galaxy_native_defines_pixel_reactive "NATIVE_PIXEL_LIT;NATIVE_REACTIVE")
set(galaxy_native_defines_pixel_layer "NATIVE_PIXEL_LIT;NATIVE_REACTIVE;NATIVE_LAYER")
set(galaxy_native_defines_pixel_motion "NATIVE_PIXEL_LIT;NATIVE_MOTION")
get_filename_component(galaxy_native_cmake_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(galaxy_native_material_dir "${galaxy_native_cmake_root}/runtime/src/render/materials")
set(galaxy_j3d_materials "")
if(EXISTS "${galaxy_native_material_dir}/j3d/list.cmake")
    include("${galaxy_native_material_dir}/j3d/list.cmake")
endif()

# One material's rules in every variant: appends its headers to `outputs`, its includes to `includes` and its table
# row to `rows` (in the caller's scope).
macro(galaxy_native_material_rules material source)
    set(row "    {")
    file(STRINGS "${source}" material_discards REGEX "discard")
    file(STRINGS "${source}" material_pixel_lit REGEX "NATIVE_PIXEL_LIT")
    foreach(variant IN LISTS galaxy_native_variants)
        if(variant MATCHES "^pixel" AND material_pixel_lit STREQUAL "")
            string(APPEND row "{}, ")
            continue()
        endif()
        # Only an alpha-tested material shades per sample.
        if(variant MATCHES "sample" AND material_discards STREQUAL "")
            string(APPEND row "{}, ")
            continue()
        endif()
        set(entries "")
        foreach(stage vs ps)
            if(stage STREQUAL "vs" AND (variant STREQUAL "reactive" OR variant STREQUAL "layer"))
                set(base_vs "g_native_${material}_base_vs")
                string(APPEND entries "${base_vs}, sizeof(${base_vs}), ")
                continue()
            endif()
            if(stage STREQUAL "vs" AND (variant STREQUAL "pixel_reactive" OR variant STREQUAL "pixel_layer"))
                set(base_vs "g_native_${material}_pixel_vs")
                string(APPEND entries "${base_vs}, sizeof(${base_vs}), ")
                continue()
            endif()
            if(stage STREQUAL "vs" AND variant STREQUAL "sample")
                set(base_vs "g_native_${material}_base_vs")
                string(APPEND entries "${base_vs}, sizeof(${base_vs}), ")
                continue()
            endif()
            if(stage STREQUAL "vs" AND variant STREQUAL "pixel_sample")
                set(base_vs "g_native_${material}_pixel_vs")
                string(APPEND entries "${base_vs}, sizeof(${base_vs}), ")
                continue()
            endif()
            set(name "g_native_${material}_${variant}_${stage}")
            set(header "${dir}/${material}_${variant}_${stage}.h")
            if(backend STREQUAL "dxil")
                set(defines "")
                foreach(define IN LISTS galaxy_native_defines_${variant})
                    list(APPEND defines "-D${define}=1")
                endforeach()
                add_custom_command(OUTPUT "${header}"
                    COMMAND "${GALAXY_DXC}" -nologo -T ${stage}_6_0 -E native_${stage} -O3 -WX -Qstrip_debug
                        -Qstrip_reflect ${defines} -I "${galaxy_native_material_dir}" -Vn ${name} -Fh "${header}"
                        "${source}"
                    DEPENDS "${source}" ${frameworks}
                    VERBATIM)
            else()
                add_custom_command(OUTPUT "${header}"
                    COMMAND galaxy_native_spirv "${source}" "${galaxy_native_material_dir}" ${stage} ${name} "${header}"
                        ${galaxy_native_defines_${variant}}
                    DEPENDS "${source}" ${frameworks} galaxy_native_spirv
                    VERBATIM)
            endif()
            list(APPEND outputs "${header}")
            string(APPEND includes "#include \"${material}_${variant}_${stage}.h\"\n")
            string(APPEND entries "${name}, sizeof(${name}), ")
        endforeach()
        string(APPEND row "{${entries}}, ")
    endforeach()
    string(APPEND rows "${row}},\n")
endmacro()

function(galaxy_native_material_sources out_var backend)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/generated/native_materials_${backend}")
    file(MAKE_DIRECTORY "${dir}")
    set(frameworks "${galaxy_native_material_dir}/native_material.hlsli"
                   "${galaxy_native_material_dir}/j3d_material.hlsli")
    set(outputs "")
    set(includes "")
    if(backend STREQUAL "dxil")
        set(element "unsigned char")
    else()
        set(element "unsigned int")
    endif()
    set(rows "    {},\n")
    foreach(material IN LISTS galaxy_native_materials)
        galaxy_native_material_rules(${material} "${galaxy_native_material_dir}/${material}.hlsl")
    endforeach()
    set(hand_rows "${rows}")
    # Each J3D material at its own number (m<NNNN>: row NNNN - 1), an empty row for a number the list leaves out (a
    # subset build: tools/dev/gen_j3d_materials.py --subset).
    set(rows "")
    set(next 1)
    foreach(material IN LISTS galaxy_j3d_materials)
        string(REGEX REPLACE "^m0*" "" number "${material}")
        while(next LESS number)
            string(APPEND rows "    {},
")
            math(EXPR next "${next} + 1")
        endwhile()
        galaxy_native_material_rules(j3d_${material} "${galaxy_native_material_dir}/j3d/${material}.hlsl")
        math(EXPR next "${next} + 1")
    endforeach()
    set(j3d_count 0)
    math(EXPR j3d_count "${next} - 1")
    if(rows STREQUAL "")
        set(rows "    {},\n")
    endif()
    # What a mod's compiled shaders must match (LumaLink: nebula_import::cook::shader_source::interface_id).
    file(READ "${galaxy_native_material_dir}/j3d_material.hlsli" interface_text)
    file(READ "${galaxy_native_material_dir}/native_material.hlsli" interface_more)
    string(APPEND interface_text "${interface_more}")
    string(REPLACE "\r" "" interface_text "${interface_text}")
    string(SHA1 shader_interface "${interface_text}")
    set(table "${dir}/native_shader_table.cpp")
    # Identical read-only shader blobs can share one linker COMDAT.
    if(MSVC)
        set_source_files_properties("${table}" PROPERTIES COMPILE_OPTIONS "/Gw")
    endif()
    file(WRITE "${table}.in"
"// Generated by cmake/native_materials.cmake: the native materials' shaders by NebulaShaderId and output variant.
#include \"galaxy/render/mod_shaders.h\"
#include \"galaxy/render/native_shaders.h\"
${includes}
namespace galaxy::render {
namespace {
struct Pair {
    const ${element}* vs;
    std::size_t vs_size;
    const ${element}* ps;
    std::size_t ps_size;
};
const Pair kShaders[][kNativeVariants] = {
${hand_rows}};
const Pair kJ3DShaders[][kNativeVariants] = {
${rows}};
constexpr std::uint32_t kJ3DCount = ${j3d_count}u;
}  // namespace

const char* native_shader_file_name() noexcept {
    return \"${backend}.shaders\";
}

const char* native_shader_interface() noexcept {
    return \"${shader_interface}\";
}

unsigned native_draw_variant(std::uint32_t shader, unsigned variant, unsigned samples_log2, bool per_sample, bool pixel_lit) noexcept {
    const auto exists = [shader](unsigned v) { return native_shader_bytes(shader, v, true).size != 0u; };
    if (pixel_lit && (variant == kNativeVariantReactive || variant == kNativeVariantLayer || variant == kNativeVariantMotion)) {
        const unsigned lit = variant == kNativeVariantReactive ? kNativeVariantPixelReactive
                             : variant == kNativeVariantLayer  ? kNativeVariantPixelLayer
                                                               : kNativeVariantPixelMotion;
        return exists(lit) ? lit : variant;
    }
    if (variant != kNativeVariantBase) {
        return variant;
    }
    const bool sampled = per_sample && samples_log2 != 0u;
    if (pixel_lit) {
        if (sampled && exists(kNativeVariantPixelSample)) return kNativeVariantPixelSample;
        if (exists(kNativeVariantPixel)) return kNativeVariantPixel;
    }
    return sampled && exists(kNativeVariantSample) ? kNativeVariantSample : variant;
}

NativeShaderBytes native_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept {
    if (variant >= kNativeVariants) {
        return {};
    }
    const Pair* p = nullptr;
    if (shader >= kNativeShaderJ3DFirst && shader - kNativeShaderJ3DFirst >= kJ3DCount) {
        return mod_shader_bytes(shader, variant, pixel);
    }
    if (shader >= kNativeShaderJ3DFirst && shader - kNativeShaderJ3DFirst < kJ3DCount) {
        p = &kJ3DShaders[shader - kNativeShaderJ3DFirst][variant];
    } else if (shader < sizeof(kShaders) / sizeof(kShaders[0])) {
        p = &kShaders[shader][variant];
    } else {
        return {};
    }
    return pixel ? NativeShaderBytes{p->ps, p->ps_size} : NativeShaderBytes{p->vs, p->vs_size};
}
}  // namespace galaxy::render
")
    add_custom_command(OUTPUT "${table}"
        COMMAND "${Python3_EXECUTABLE}" -I "${galaxy_native_cmake_root}/tools/dev/pack_native_shaders.py"
                "${table}.in" "${table}"
        DEPENDS "${table}.in" ${outputs} "${galaxy_native_cmake_root}/tools/dev/pack_native_shaders.py"
        VERBATIM)
    set(${out_var} ${outputs} "${table}" PARENT_SCOPE)
    set(galaxy_native_include_dir_${backend} "${dir}" PARENT_SCOPE)
endfunction()
