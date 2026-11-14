# The programs: the game's (runtime/src/render/programs, tools/dev/build_programs.py) and the written shaders
# (runtime/src/render/materials), compiled at build into one file in every output variant
# (tools/dev/compile_programs.py). The player's machine compiles nothing. galaxy_program_blob(OUT_VAR BACKEND) adds the
# rule and sets OUT_VAR to what an executable adds: on Windows a resource holding the file; elsewhere nothing, core/program_table.cpp includes it.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(galaxy_programs_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(galaxy_program_blob out_var backend)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${dir}")
    set(blob "${dir}/programs_${backend}.bin")
    file(GLOB sources CONFIGURE_DEPENDS
        "${galaxy_programs_root}/runtime/src/render/materials/*.hlsl"
        "${galaxy_programs_root}/runtime/src/render/materials/*.hlsli"
        "${galaxy_programs_root}/runtime/src/render/programs/*.hlsl")
    # The program set may change only with the store version (compile_programs.py check_store_programs).
    list(APPEND sources "${galaxy_programs_root}/runtime/src/render/programs/table.tsv"
        "${galaxy_programs_root}/runtime/include/galaxy/store_version.inc"
        "${galaxy_programs_root}/runtime/include/galaxy/store_programs.txt")
    if(backend STREQUAL "dxil")
        add_custom_command(OUTPUT "${blob}"
            COMMAND ${CMAKE_COMMAND} -E env "GALAXY_DXC=${GALAXY_DXC}"
                "${Python3_EXECUTABLE}" "${galaxy_programs_root}/tools/dev/compile_programs.py" dxil "${blob}"
            DEPENDS ${sources} "${galaxy_programs_root}/tools/dev/compile_programs.py"
            VERBATIM)
    else()
        add_custom_command(OUTPUT "${blob}"
            COMMAND "${Python3_EXECUTABLE}" "${galaxy_programs_root}/tools/dev/compile_programs.py" spirv "${blob}"
                "$<TARGET_FILE:galaxy_native_spirv>"
            DEPENDS ${sources} "${galaxy_programs_root}/tools/dev/compile_programs.py" galaxy_native_spirv
            VERBATIM)
    endif()
    if(WIN32)
        set(holder "${dir}/programs_${backend}.rc")
        file(TO_CMAKE_PATH "${blob}" blob_path)
        file(WRITE "${holder}" "101 RCDATA \"${blob_path}\"\n")
        set_source_files_properties("${holder}" PROPERTIES OBJECT_DEPENDS "${blob}")
    else()
        set(holder "")
        set_source_files_properties("${galaxy_programs_root}/runtime/src/render/core/program_table.cpp" PROPERTIES
            COMPILE_DEFINITIONS "GALAXY_PROGRAM_BLOB=\"${blob}\"" OBJECT_DEPENDS "${blob}")
    endif()
    set(${out_var} "${holder}" PARENT_SCOPE)
endfunction()
