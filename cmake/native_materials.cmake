# The native materials (E-04; runtime/src/render/materials/<name>.hlsl with native_material.hlsli), compiled at build in
# every output variant. galaxy_native_materials lists the hand-written ones in NebulaShaderId order (nebula_game_api.h,
# from 1); runtime/src/render/materials/j3d/list.cmake (tools/dev/gen_j3d_materials.py) the J3D material set
# (NebulaShaderId NEBULA_SHADER_J3D_FIRST + n - 1). galaxy_native_material_sources(OUT_VAR BACKEND) adds the build
# rules of BACKEND (dxil: dxc; spirv: the galaxy_native_spirv tool) and sets OUT_VAR to the generated headers and table
# source to add to a target.

set(galaxy_native_materials whirlpool ocean_bowl screen_blur flat_color clip_fill big_bubble
    whirlpool_accelerator spin_driver_path spider_thread tex_modulate water_plant ocean_ring_pipe_inside ocean_sphere
    ocean_sphere_env ocean_ring ocean_ring_bloom water_road ocean_ring_pipe_outside color_tex_alpha
    vertex_color dd_texture dd_texture_color tex_vertex_alpha tex_alpha_lerp
    trample_star tex_const_alpha font_text j2d_picture fur particle ocean_bowl_bloom mii_face mii_texture)
set(galaxy_native_variants base reactive layer motion dual)
set(galaxy_native_defines_base "")
set(galaxy_native_defines_reactive "NATIVE_REACTIVE")
set(galaxy_native_defines_layer "NATIVE_REACTIVE;NATIVE_LAYER")
set(galaxy_native_defines_motion "NATIVE_MOTION")
set(galaxy_native_defines_dual "NATIVE_DUAL")
set(galaxy_native_material_dir "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/materials")
set(galaxy_j3d_materials "")
if(EXISTS "${galaxy_native_material_dir}/j3d/list.cmake")
    include("${galaxy_native_material_dir}/j3d/list.cmake")
endif()

# One material's rules in every variant: appends its headers to `outputs`, its includes to `includes` and its table
# row to `rows` (in the caller's scope).
macro(galaxy_native_material_rules material source)
    set(row "    {")
    foreach(variant IN LISTS galaxy_native_variants)
        set(entries "")
        foreach(stage vs ps)
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
    # The curved surfaces' patch stages (native_curved.hlsl), once per output layout: 0 without, 1 with motion vectors.
    set(curved_rows "")
    foreach(layout 0 1)
        set(entries "")
        foreach(stage hs ds)
            set(name "g_native_curved_${layout}_${stage}")
            set(header "${dir}/curved_${layout}_${stage}.h")
            set(source "${galaxy_native_material_dir}/native_curved.hlsl")
            if(backend STREQUAL "dxil")
                set(defines "")
                if(layout STREQUAL "1")
                    set(defines "-DNATIVE_MOTION=1")
                endif()
                add_custom_command(OUTPUT "${header}"
                    COMMAND "${GALAXY_DXC}" -nologo -T ${stage}_6_0 -E native_${stage} -O3 -WX -Qstrip_debug
                        -Qstrip_reflect ${defines} -I "${galaxy_native_material_dir}" -Vn ${name} -Fh "${header}"
                        "${source}"
                    DEPENDS "${source}" ${frameworks}
                    VERBATIM)
            else()
                set(defines "")
                if(layout STREQUAL "1")
                    set(defines "NATIVE_MOTION")
                endif()
                add_custom_command(OUTPUT "${header}"
                    COMMAND galaxy_native_spirv "${source}" "${galaxy_native_material_dir}" ${stage} ${name} "${header}"
                        ${defines}
                    DEPENDS "${source}" ${frameworks} galaxy_native_spirv
                    VERBATIM)
            endif()
            list(APPEND outputs "${header}")
            string(APPEND includes "#include \"curved_${layout}_${stage}.h\"\n")
            string(APPEND entries "${name}, sizeof(${name}), ")
        endforeach()
        string(APPEND curved_rows "    {${entries}},\n")
    endforeach()
    # Lines and points (native_lines.hlsl): the geometry stages that widen them, once per output layout.
    set(line_rows "")
    foreach(layout 0 1)
        set(entries "")
        foreach(stage gs_line gs_point)
            set(name "g_native_${stage}_${layout}")
            set(header "${dir}/${stage}_${layout}.h")
            set(source "${galaxy_native_material_dir}/native_lines.hlsl")
            if(backend STREQUAL "dxil")
                set(defines "")
                if(layout STREQUAL "1")
                    set(defines "-DNATIVE_MOTION=1")
                endif()
                add_custom_command(OUTPUT "${header}"
                    COMMAND "${GALAXY_DXC}" -nologo -T gs_6_0 -E native_${stage} -O3 -WX -Qstrip_debug
                        -Qstrip_reflect ${defines} -I "${galaxy_native_material_dir}" -Vn ${name} -Fh "${header}"
                        "${source}"
                    DEPENDS "${source}" ${frameworks}
                    VERBATIM)
            else()
                set(defines "")
                if(layout STREQUAL "1")
                    set(defines "NATIVE_MOTION")
                endif()
                add_custom_command(OUTPUT "${header}"
                    COMMAND galaxy_native_spirv "${source}" "${galaxy_native_material_dir}" ${stage} ${name} "${header}"
                        ${defines}
                    DEPENDS "${source}" ${frameworks} galaxy_native_spirv
                    VERBATIM)
            endif()
            list(APPEND outputs "${header}")
            string(APPEND includes "#include \"${stage}_${layout}.h\"\n")
            string(APPEND entries "${name}, sizeof(${name}), ")
        endforeach()
        string(APPEND line_rows "    {${entries}},\n")
    endforeach()
    set(table "${dir}/native_shader_table.cpp")
    file(WRITE "${table}.in"
"// Generated by cmake/native_materials.cmake: the native materials' shaders by NebulaShaderId and output variant.
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
const Pair kCurved[2] = {
${curved_rows}};
const Pair kLinePoint[2] = {
${line_rows}};
}  // namespace

NativeShaderBytes native_line_point_bytes(bool motion, bool point) noexcept {
    const Pair& p = kLinePoint[motion ? 1 : 0];
    return point ? NativeShaderBytes{p.ps, p.ps_size} : NativeShaderBytes{p.vs, p.vs_size};
}

NativeShaderBytes native_curved_bytes(bool motion, bool domain) noexcept {
    const Pair& p = kCurved[motion ? 1 : 0];
    return domain ? NativeShaderBytes{p.ps, p.ps_size} : NativeShaderBytes{p.vs, p.vs_size};
}

NativeShaderBytes native_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept {
    if (variant >= kNativeVariants) {
        return {};
    }
    const Pair* p = nullptr;
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
    configure_file("${table}.in" "${table}" COPYONLY)
    set_source_files_properties("${table}" PROPERTIES OBJECT_DEPENDS "${outputs}")
    set(${out_var} ${outputs} "${table}" PARENT_SCOPE)
    set(galaxy_native_include_dir_${backend} "${dir}" PARENT_SCOPE)
endfunction()
