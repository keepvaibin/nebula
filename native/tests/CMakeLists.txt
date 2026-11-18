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
# The binder keeps every plane a bind meets (P-166), on the game's arenas.
native_test(native_binder_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/binder_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp" "${game_src}/LiveActor/Binder.cpp" "${game_src}/System/NebulaArena.cpp")
# Every message Mario sends in a step goes out (N-3006).
native_test(native_mario_messenger_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/mario_messenger_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c" "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp"
    "${game_src}/Player/MarioMessenger.cpp" "${game_src}/System/NebulaArena.cpp")
# Mario's state timers stop at their top instead of reading 0 after 65536 steps (N-3011).
native_test(native_frame_count_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/frame_count_tests.cpp")
# A Life Up's change of the most health ends when a step is refused (N-3015); the ceiling probe of a hanging Mario
# probes from above once (N-3016).
native_test(native_mario_life_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/mario_life_tests.cpp")
native_test(native_mario_ceil_probe_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/mario_ceil_probe_tests.cpp")
# The running rabbit's turn up from the map ends (N-3018).
native_test(native_rabbit_run_turn_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/rabbit_run_turn_tests.cpp")
# MarioActor's model index holds every mod power-up (N-3020).
native_test(native_mario_model_index_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/mario_model_index_tests.cpp")
# The dash ring's times hold every time its placement gives (N-3023).
native_test(native_mario_ring_dash_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/mario_ring_dash_tests.cpp")
# A sequence's data reads stay in its program, and a tick that never waits ends (N-3025, N-3026).
native_test(native_seq_read_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/seq_read_tests.cpp")
# A Me track's note ranges move a note by whole octaves and always end (N-3027).
native_test(native_note_octave_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/note_octave_tests.cpp")
# A placement argument names a table's entry or takes the fallback (N-3031, N-3032).
native_test(native_placement_index_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/placement_index_tests.cpp")
# The frame-list writer, against the list the renderer reads.
native_test(native_frame_list_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/frame_list_tests.cpp" "${game_src}/Render/FrameList.cpp")
# The cutscene skip's bound, in steps of play (header only).
native_test(native_cutscene_skip_bound_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/cutscene_skip_bound_tests.cpp")
if(NEBULA_SANITIZE AND WIN32)
    # NebulaArena.cpp defines operator new/delete, as in the module: the sanitizer runtime's own give way (N-1062).
    foreach(arena_test native_arena_tests native_binder_tests native_mario_messenger_tests)
        target_link_options(${arena_test} PRIVATE LINKER:/force:multiple)
    endforeach()
endif()
