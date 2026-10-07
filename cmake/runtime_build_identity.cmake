# Runs at build time, not launch time. Never modifies source or stages files.
include("${CONFIG_FILE}")
set(revision "unknown")
set(revision_kind "unknown")
set(source_commit "unknown")
set(git_tree "unknown")
set(dirty "unknown")
set(export_tree "unknown")
find_program(identity_git git)
if(identity_git)
    execute_process(COMMAND "${identity_git}" -C "${NEBULA_ID_SOURCE}" rev-parse --show-toplevel
        OUTPUT_VARIABLE git_root OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
    file(TO_CMAKE_PATH "${git_root}" git_root)
    file(TO_CMAKE_PATH "${NEBULA_ID_SOURCE}" source_root)
    string(TOLOWER "${git_root}" git_root)
    string(TOLOWER "${source_root}" source_root)
    if(git_result EQUAL 0 AND git_root STREQUAL source_root)
        execute_process(COMMAND "${identity_git}" -C "${NEBULA_ID_SOURCE}" rev-parse HEAD
            OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        set(revision_kind "commit")
        set(source_commit "${revision}")
        execute_process(COMMAND "${identity_git}" -C "${NEBULA_ID_SOURCE}" rev-parse "HEAD^{tree}"
            OUTPUT_VARIABLE git_tree OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND "${identity_git}" -C "${NEBULA_ID_SOURCE}" status --porcelain --untracked-files=normal
            OUTPUT_VARIABLE status ERROR_QUIET RESULT_VARIABLE status_result)
        if(status_result EQUAL 0)
            if(status STREQUAL "")
                set(dirty "0")
            else()
                set(dirty "1")
            endif()
        endif()
    endif()
endif()
if(revision STREQUAL "unknown" AND NOT NEBULA_ID_REVISION STREQUAL "")
    set(revision "${NEBULA_ID_REVISION}")
    set(revision_kind "${NEBULA_ID_REVISION_KIND}")
    set(git_tree "${NEBULA_ID_GIT_TREE}")
    if(revision_kind STREQUAL "commit")
        set(source_commit "${revision}")
    elseif(revision_kind STREQUAL "tree")
        set(git_tree "${revision}")
    endif()
    set(export_tree "${NEBULA_ID_TREE_SHA256}")
    set(dirty "${NEBULA_ID_DIRTY}")
endif()

# Fingerprint runtime source inputs even for dirty or exported source trees.
# Compiler recipe is separately recorded and CMake files participate here.
file(GLOB_RECURSE inputs LIST_DIRECTORIES false RELATIVE "${NEBULA_ID_SOURCE}"
    "${NEBULA_ID_SOURCE}/runtime/*.cpp" "${NEBULA_ID_SOURCE}/runtime/*.c"
    "${NEBULA_ID_SOURCE}/runtime/*.h" "${NEBULA_ID_SOURCE}/runtime/*.hpp"
    "${NEBULA_ID_SOURCE}/runtime/*.inc" "${NEBULA_ID_SOURCE}/runtime/*.hlsl"
    "${NEBULA_ID_SOURCE}/third_party/*.c" "${NEBULA_ID_SOURCE}/third_party/*.cpp"
    "${NEBULA_ID_SOURCE}/third_party/*.h" "${NEBULA_ID_SOURCE}/third_party/*.hpp"
    "${NEBULA_ID_SOURCE}/cmake/*.cmake" "${NEBULA_ID_SOURCE}/cmake/*.in")
list(APPEND inputs "CMakeLists.txt")
list(SORT inputs)
set(input_manifest "")
foreach(input IN LISTS inputs)
    file(SHA256 "${NEBULA_ID_SOURCE}/${input}" digest)
    string(APPEND input_manifest "${input}:${digest}\n")
endforeach()
string(SHA256 inputs_sha256 "${input_manifest}")
string(TOUPPER "${BUILD_CONFIG}" config_upper)
set(flags "${NEBULA_ID_FLAGS} ${NEBULA_ID_FLAGS_${config_upper}}")
set(identity "source-commit=${source_commit} source-revision=${revision} revision-kind=${revision_kind} git-tree=${git_tree} export-tree-sha256=${export_tree} dirty=${dirty} runtime-inputs-sha256=${inputs_sha256} compiler=\"${NEBULA_ID_COMPILER}\" config=${BUILD_CONFIG} flags=\"${flags}\" target-options=\"${NEBULA_ID_TARGET_OPTIONS}\" directory-options=\"${NEBULA_ID_DIRECTORY_OPTIONS}\" isa=\"${NEBULA_ID_ISA}\" dsp-module=${NEBULA_ID_DSP_MODULE} recipe=CMakeLists.txt")
string(REPLACE "\\" "\\\\" identity "${identity}")
string(REPLACE "\"" "\\\"" identity "${identity}")
string(REPLACE "\n" "\\n" identity "${identity}")
string(REPLACE "\r" "" identity "${identity}")
file(WRITE "${OUTPUT_HEADER}.tmp" "#pragma once\n#define NEBULA_RUNTIME_BUILD_IDENTITY \"${identity}\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUTPUT_HEADER}.tmp" "${OUTPUT_HEADER}"
    COMMAND_ERROR_IS_FATAL ANY)
