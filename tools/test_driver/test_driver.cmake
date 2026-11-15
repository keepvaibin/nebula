# The test runs' targets: a test build passes this file to the root CMakeLists.txt as NEBULA_EXTRA_CMAKE, which
# includes it after galaxy_test. Never in an install.

# The test runs' extension (run.sh: --extension): scripted controls, pictures, sound, menus and device losses from the
# run's GALAXY_* environment.
set(test_extension ${CMAKE_CURRENT_LIST_DIR}/extension)
add_library(nebula_test_extension MODULE ${test_extension}/test_extension.cpp ${test_extension}/harness.cpp
    ${test_extension}/test_services.cpp ${test_extension}/test_plugin.cpp ${test_extension}/data_guard.cpp
    ${rt}/hidden_start.cpp)
target_link_libraries(nebula_test_extension PRIVATE galaxy_runtime_abi)
if(WIN32)
    target_link_libraries(nebula_test_extension PRIVATE shell32)
else()
    target_link_libraries(nebula_test_extension PRIVATE ${CMAKE_DL_LIBS})
endif()
target_compile_features(nebula_test_extension PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(nebula_test_extension PRIVATE /W4 /WX /permissive- /EHsc)
endif()

# The extension's data guard: a test run never touches a player's folders.
galaxy_test(galaxy_test_data_paths_tests SOURCES ${CMAKE_CURRENT_LIST_DIR}/tests/test_data_paths_tests.cpp ${rt}/hidden_start.cpp
    ${test_extension}/data_guard.cpp LIBS shell32)
target_include_directories(galaxy_test_data_paths_tests PRIVATE ${test_extension})

# The extension's work-folder policy holds for this test too: its folders are in the build's test-work.
galaxy_test(galaxy_player_input_tests SOURCES ${CMAKE_CURRENT_LIST_DIR}/tests/player_input_tests.cpp LIBS galaxy_render
    ARGS --settings "${CMAKE_CURRENT_BINARY_DIR}/test-work/settings" --crash-dumps "${CMAKE_CURRENT_BINARY_DIR}/test-work/crashes")
target_compile_definitions(galaxy_player_input_tests PRIVATE NEBULA_TEST_EXTENSION_FILE="$<TARGET_FILE:nebula_test_extension>"
    NEBULA_TEST_WORK="${CMAKE_CURRENT_BINARY_DIR}/test-work")
add_dependencies(galaxy_player_input_tests nebula_test_extension)
