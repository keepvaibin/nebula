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
native_test(native_formatted_name_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/formatted_name_tests.cpp"
    "${game_src}/Util/StringUtil.cpp" "${game_src}/Util/TextString.cpp" "${game_src}/System/NebulaFormat.c")
native_test(native_name_index_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/name_index_tests.cpp" "${game_src}/Util/HashUtil.cpp")
# The game's arenas, on the host's address services as the host gives them.
native_test(native_arena_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/arena_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/arena_test_host.c"
    "${game_src}/System/NebulaArena.cpp")
# The frame-list writer, against the list the renderer reads.
native_test(native_frame_list_tests SOURCES "${CMAKE_CURRENT_LIST_DIR}/frame_list_tests.cpp" "${game_src}/Render/FrameList.cpp")
