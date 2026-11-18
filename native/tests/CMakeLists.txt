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
# lumacore's classes of living objects: exactClassOf gives no class to an object whose constructor never names one.
native_test(native_lumacore_class_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/lumacore_class_tests.cpp"
    "${game_src}/System/LumaCore.cpp")
target_compile_definitions(native_lumacore_class_tests PRIVATE NEBULA_PLAIN_MODULE=1)
# The game's arenas, on the host's address services as the host gives them.
native_test(native_arena_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/arena_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c"
    "${game_src}/System/NebulaArena.cpp")
# The binder keeps every plane a bind meets (P-166), on the game's arenas.
native_test(native_binder_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/binder_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp" "${game_src}/LiveActor/Binder.cpp" "${game_src}/System/NebulaArena.cpp")
# The same in the plain module, which checks a still actor's place once (P-138).
native_test(native_binder_plain_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/binder_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp" "${game_src}/LiveActor/Binder.cpp" "${game_src}/System/NebulaArena.cpp")
target_compile_definitions(native_binder_plain_tests PRIVATE NEBULA_PLAIN_MODULE=1)
# The plain module's collision change log: a change the game's line test could find touching a line reaches it (P-140).
native_test(native_collision_changes_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/collision_changes_tests.cpp"
    "${game_src}/Map/CollisionChanges.cpp")
target_compile_definitions(native_collision_changes_tests PRIVATE NEBULA_PLAIN_MODULE=1)
# A sensor keeps every sensor it touches (P-148), on the game's arenas.
native_test(native_hit_sensor_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/hit_sensor_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c" "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp"
    "${game_src}/LiveActor/HitSensor.cpp" "${game_src}/System/NebulaArena.cpp")
# The plain module's sensor pairs: the sweep keeps every pair and its order (P-039), and is timed against the walk.
native_test(native_sensor_hit_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/sensor_hit_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c" "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp"
    "${game_src}/LiveActor/SensorHitChecker.cpp" "${game_src}/LiveActor/HitSensor.cpp" "${game_src}/System/NebulaArena.cpp")
target_compile_definitions(native_sensor_hit_tests PRIVATE NEBULA_PLAIN_MODULE=1)
# The plain module's gravity and area bounds hold every point a gravity or a form takes (P-139, P-152).
native_test(native_gravity_bound_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/gravity_bound_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c" "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp"
    "${game_src}/Gravity/PlanetGravity.cpp" "${game_src}/Gravity/PointGravity.cpp" "${game_src}/Gravity/SegmentGravity.cpp"
    "${game_src}/Gravity/DiskGravity.cpp" "${game_src}/Gravity/DiskTorusGravity.cpp" "${game_src}/Gravity/ConeGravity.cpp"
    "${game_src}/Gravity/CubeGravity.cpp" "${game_src}/Gravity/ParallelGravity.cpp" "${game_src}/Gravity/WireGravity.cpp"
    "${game_src}/AreaObj/AreaForm.cpp" "${game_src}/Util/MathUtil.cpp" "${game_src}/Util/MtxUtil.cpp"
    "${game_src}/System/NebulaArena.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/math/src/nebula_math.c")
target_compile_definitions(native_gravity_bound_tests PRIVATE NEBULA_PLAIN_MODULE=1)
# The plain module's shape-drawer packets keep the array's order at a cost that does not grow with them (P-038).
native_test(native_draw_run_list_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/draw_run_list_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c" "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp"
    "${game_src}/System/DrawRunList.cpp" "${game_src}/System/NebulaArena.cpp")
target_compile_definitions(native_draw_run_list_tests PRIVATE NEBULA_PLAIN_MODULE=1)
# The plain module's collision parts by place give every part the keeper's tests take, in order (P-129).
native_test(native_collision_index_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/collision_index_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c" "${CMAKE_CURRENT_LIST_DIR}/arena_test_tables.cpp"
    "${game_src}/Map/CollisionPartIndex.cpp" "${game_src}/System/NebulaArena.cpp")
target_compile_definitions(native_collision_index_tests PRIVATE NEBULA_PLAIN_MODULE=1 NEBULA_COLLISION_INDEX_VERIFY=1)
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
# The frame-list writer, against the list the renderer reads.
native_test(native_frame_list_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/frame_list_tests.cpp" "${game_src}/Render/FrameList.cpp")
