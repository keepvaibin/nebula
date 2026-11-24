# The game's unit tests: the game's own code compiled with the game module's flags (smg_game_common's compile options,
# definitions and include folders; not its link options, which build a module without a C runtime), as programs ctest
# runs. Included by native/CMakeLists.txt. `cmake --build <game build> --target native_tests` builds them (the target
# all does too) and `ctest --test-dir <game build>` runs them.
enable_testing()
add_custom_target(native_tests)

# native_test(name SOURCES ...): a test program from the test's sources and the game files it tests. Each links
# test_support.c, which stands in for the host's panic.
function(native_test name)
    cmake_parse_arguments(arg "" "" "SOURCES" ${ARGN})
    add_executable(${name} ${arg_SOURCES} "${CMAKE_CURRENT_LIST_DIR}/test_support.c")
    target_compile_options(${name} PRIVATE $<TARGET_PROPERTY:smg_game_common,INTERFACE_COMPILE_OPTIONS>)
    target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:smg_game_common,INTERFACE_COMPILE_DEFINITIONS>)
    target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:smg_game_common,INTERFACE_INCLUDE_DIRECTORIES>)
    set_target_properties(${name} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON CXX_STANDARD 17 CXX_EXTENSIONS ON)
    if(NOT WIN32 AND NOT ANDROID)
        target_link_libraries(${name} PRIVATE pthread m)
    endif()
    if(NEBULA_SANITIZE)
        # The sanitizers' runtime, for the code compiled with them (smg_game_common's compile options).
        target_link_options(${name} PRIVATE -fsanitize=${NEBULA_SANITIZE})
    endif()
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(native_tests ${name})
endfunction()

set(game_src "${petari}/src/Game")
# The math library.
native_test(native_math_tests SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/math/tests/math_tests.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/math/src/nebula_math.c")
# Text strings, UI names, formatted names and the name index.
native_test(native_text_string_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/text_string_tests.cpp" "${game_src}/Util/TextString.cpp")
native_test(native_name_text_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/name_text_tests.cpp")
# Windows Setup's uninstall questions (installer/native/uninstall_choice.h, header only): run on every platform.
native_test(native_uninstall_choice_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/uninstall_choice_tests.cpp")
# Windows Setup's update channels (installer/native/release_channel.h, header only): run on every platform.
native_test(native_release_channel_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/release_channel_tests.cpp")
native_test(native_formatted_name_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/formatted_name_tests.cpp"
    "${game_src}/Util/StringUtil.cpp" "${game_src}/Util/TextString.cpp" "${game_src}/System/NebulaFormat.c")
native_test(native_name_index_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/name_index_tests.cpp" "${game_src}/Util/HashUtil.cpp")
# The game's arenas, on the host's address services as the host gives them.
native_test(native_arena_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/arena_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c"
    "${game_src}/System/NebulaArena.cpp")
if(NEBULA_SANITIZE AND WIN32)
    # NebulaArena.cpp defines operator new/delete, as in the module: the sanitizer runtime's own give way (N-1062).
    target_link_options(native_arena_tests PRIVATE LINKER:/force:multiple)
elseif(NEBULA_SANITIZE MATCHES "thread")
    # The same on Linux: the address sanitizer's operators are weak and give way by themselves, the thread sanitizer's
    # are not (its C++ runtime, tsan_new_delete.cpp), so the program is linked without that runtime and the arena's
    # operators are the program's, as in the module (P-012).
    target_link_options(native_arena_tests PRIVATE -fno-sanitize-link-c++-runtime)
endif()
# The tables of draws that swap one map's image, registered once per distinct swap (R2.4, header only).
native_test(native_swap_tables_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/swap_tables_tests.cpp")
# The renderer's numbers for the game's textures: kept as the table grows; a table the heap cannot hold panics (P-255).
native_test(native_render_texture_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/render_texture_tests.cpp" "${game_src}/Render/RenderTexture.cpp")
# Work done once by the first thread that asks (P-183: the creator index), header only.
native_test(native_once_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/once_tests.cpp")
# The frame-list writer, against the list the renderer reads.
native_test(native_frame_list_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/frame_list_tests.cpp" "${game_src}/Render/FrameList.cpp")
# The cutscene skip's bound, in steps of play (header only).
native_test(native_cutscene_skip_bound_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/cutscene_skip_bound_tests.cpp")
# The load's slices by a fixed count of work (P-260, header only).
native_test(native_load_slice_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/load_slice_tests.cpp")
# The scene query's ray: the direction through a screen point and the sphere it meets first (P-294, header only).
native_test(native_scene_point_ray_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/scene_point_ray_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/math/src/nebula_math.c")
# The benchmark run screen's part names (header only).
native_test(native_run_parts_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/run_parts_tests.cpp")
