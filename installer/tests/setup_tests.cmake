# Setup's own code (installer/native) under test, built as Setup builds it (installer/build-native-setup.ps1): its file
# helpers (P-121) and its windows when the DPI changes (P-115). Included from the root CMakeLists.txt on Windows.
set(setup_native "${CMAKE_CURRENT_SOURCE_DIR}/installer/native")
set(setup_miniz "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")
set(setup_gen "${CMAKE_CURRENT_BINARY_DIR}/setup_tests")
# The release facts Setup is built with; the tests read none of them.
file(CONFIGURE OUTPUT "${setup_gen}/build_info.h" CONTENT [=[
#pragma once
namespace nebula::build {
inline constexpr const wchar_t kVersion[] = L"0.0.0-test";
inline constexpr const wchar_t kFileVersion[] = L"0.0.0.0";
inline constexpr const wchar_t kCommit[] = L"test";
inline constexpr const wchar_t kRepository[] = L"test/test";
inline constexpr const wchar_t kReleasePublicKeyXml[] = L"";
inline constexpr unsigned kStoreVersion = 0;
}
]=])

add_library(setup_tests_miniz STATIC "${setup_miniz}/miniz.c")
target_include_directories(setup_tests_miniz PUBLIC "${setup_miniz}")

function(setup_test name)
    cmake_parse_arguments(arg "" "" "SOURCES" ${ARGN})
    add_executable(${name} ${arg_SOURCES})
    target_include_directories(${name} PRIVATE "${setup_gen}" "${setup_native}")
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_compile_options(${name} PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus)
    target_link_libraries(${name} PRIVATE setup_tests_miniz)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

setup_test(nebula_setup_helpers_tests SOURCES installer/tests/setup_helpers_tests.cpp
    "${setup_native}/common.cpp" "${setup_native}/archive.cpp" "${setup_native}/text.cpp")
setup_test(nebula_setup_ui_dpi_tests SOURCES installer/tests/setup_ui_dpi_tests.cpp
    "${setup_native}/ui.cpp" "${setup_native}/common.cpp" "${setup_native}/text.cpp")
