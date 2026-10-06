#include "galaxy/atomic_file.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
}

bool test_collision_exception_preserves_unowned_sibling(
    const std::filesystem::path& root) {
    // This runs before the ordinary writer uses sequence 1 in this process.
    const auto target = root / "collision-settings.ini";
    const auto collided = root /
        (L"collision-settings.ini.tmp." +
         std::to_wstring(GetCurrentProcessId()) + L".1");
    std::filesystem::create_directories(root);
    {
        std::ofstream destination(target, std::ios::binary);
        destination << "old destination\n";
        std::ofstream sibling(collided, std::ios::binary);
        sibling << "preexisting sibling\n";
        if (!destination || !sibling) {
            return expect(false, "collision fixture files were created");
        }
    }
    bool collision_observed = false;
    const bool replaced =
        galaxy::io::detail::write_text_file_atomically_with_collision_hook(
            target,
            "new destination\n",
            [](void* user) {
                *static_cast<bool*>(user) = true;
                throw std::bad_alloc{};
            },
            &collision_observed);
    const bool passed = expect(
        !replaced && collision_observed &&
            read_text(target) == "old destination\n" &&
            read_text(collided) == "preexisting sibling\n",
        "exception after failed temp acquisition preserves both unowned sibling and destination");
    std::filesystem::remove(collided);
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("galaxy_atomic_file_tests_" +
         std::to_string(GetCurrentProcessId()) + "_" +
         std::to_string(GetTickCount64()));
    const std::filesystem::path target = root / "settings.ini";
    if (!std::filesystem::create_directory(root)) {
        std::cerr << "FAILED: fixture directory was not newly created\n";
        return 1;
    }

    passed &= test_collision_exception_preserves_unowned_sibling(root);

    passed &= expect(
        galaxy::io::write_text_file_atomically(target, "version=old\n"),
        "initial atomic write succeeds");

    HANDLE locked = CreateFileW(
        target.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    passed &= expect(locked != INVALID_HANDLE_VALUE, "test target is locked");
    passed &= expect(
        !galaxy::io::write_text_file_atomically(target, "version=new\n") &&
            read_text(target) == "version=old\n",
        "replace failure preserves the complete old destination");
    if (locked != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(locked);
    }

    std::size_t temporary_count = 0u;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().filename().wstring().find(L".tmp.") !=
            std::wstring::npos) {
            ++temporary_count;
        }
    }
    passed &= expect(
        temporary_count == 0u,
        "failed replacement removes its sibling temporary file");
    passed &= expect(
        galaxy::io::write_text_file_atomically(target, "version=new\n") &&
            read_text(target) == "version=new\n",
        "atomic replacement succeeds after the destination unlocks");

    const std::filesystem::path blocking_parent = root / "not-a-directory";
    {
        std::ofstream file(blocking_parent, std::ios::binary);
        file << "block";
    }
    passed &= expect(
        !galaxy::io::write_text_file_atomically(
            blocking_parent / "settings.ini",
            "unreachable=1\n"),
        "invalid parent fails without replacing another path");

    std::filesystem::remove_all(root);
    if (!passed) {
        return 1;
    }
    std::cout << "Atomic settings file tests passed\n";
    return 0;
}
