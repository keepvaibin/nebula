// SPDX-License-Identifier: GPL-3.0-only
// The update's install step for install.sh's install (update_install_linux.cpp, P-125), with the real install.sh
// (NEBULA_INSTALLER_LINUX) and a stand-in package: which installs it takes, the install into a portable folder, the
// running version kept, the restart's program and library path, and the failures it reports.
#include "update_install_linux.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace galaxy::update::detail;

namespace {

bool g_ok = true;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        g_ok = false;
    }
}

void write(const fs::path& path, const std::string& text, bool program = false) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    if (program) (void)chmod(path.c_str(), 0755);
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A program's head as ELF says it, for this machine or another (install.sh checks the machine at byte 18).
std::string elf(bool this_machine) {
#if defined(__aarch64__)
    const bool arm = this_machine;
#else
    const bool arm = !this_machine;
#endif
    std::string head("\x7f" "ELF\x02\x01\x01", 7);
    head.append(9, '\0');
    head += std::string("\x02\x00", 2);
    head += arm ? std::string("\xb7\x00", 2) : std::string("\x3e\x00", 2);
    return head;
}

// The release's tar.gz as package-linux.sh makes it: one folder with install.sh and files/.
fs::path make_package(const fs::path& work, const std::string& version, bool this_machine, bool with_script) {
    const fs::path folder = work / ("Nebula-" + version + "-linux-x64");
    fs::remove_all(work);
    const fs::path scripts = NEBULA_INSTALLER_LINUX;
    fs::create_directories(folder);
    if (with_script) {
        fs::copy_file(scripts / "install.sh", folder / "install.sh");
    }
    fs::copy_file(scripts / "uninstall.sh", folder / "uninstall.sh", fs::copy_options::overwrite_existing);
    fs::copy_file(scripts / "nebula-launch", folder / "nebula-launch", fs::copy_options::overwrite_existing);
    for (const char* name : {"dev.nebula.galaxy.desktop", "dev.nebula.galaxy.xml", "dev.nebula.galaxy.png"}) write(folder / name, "");
    write(folder / "VERSION", version + "\n");
    write(folder / "files" / "nebula", elf(this_machine), true);
    write(folder / "files" / "nebula-import", "#!/bin/sh\nexit 1\n", true);
    const fs::path package = work.parent_path() / ("update-" + version + ".partial");
    const std::string command = "tar -czf '" + package.string() + "' -C '" + work.string() + "' '" + folder.filename().string() + "'";
    check(std::system(command.c_str()) == 0, "the stand-in package is made");
    fs::remove_all(work);
    return package;
}

}  // namespace

int main() {
    char pattern[] = "/tmp/nebula-update-XXXXXX";
    const char* made = mkdtemp(pattern);
    if (made == nullptr) {
        std::fprintf(stderr, "FAILED: no temporary folder\n");
        return 1;
    }
    const fs::path temp = made;

    // A portable folder as install.sh leaves it, running 1.0.0.
    const fs::path root = temp / "portable";
    const fs::path running = root / "versions" / "1.0.0" / "nebula";
    write(running, elf(true), true);
    write(root / "current.json", "{\n  \"version\": \"1.0.0\",\n  \"previous\": null\n}\n");
    write(root / "nebula-launch", "", true);
    write(root / "portable.txt", "Portable Nebula.\n");
    write(root / "content" / "game.pak", "game");

    const std::optional<ScriptInstall> portable = script_install_of(running, nullptr, nullptr);
    check(portable && portable->portable && fs::equivalent(portable->root, root), "a portable folder is taken");
    check(!script_install_of(temp / "bin" / "nebula", nullptr, "/home/x"), "a program outside an install is not");

    // A user install is the one install.sh updates, under ${XDG_DATA_HOME:-$HOME/.local/share}/Nebula.
    const fs::path user = temp / "share" / "Nebula";
    write(user / "versions" / "1.0.0" / "nebula", elf(true), true);
    write(user / "current.json", "{\n  \"version\": \"1.0.0\",\n  \"previous\": null\n}\n");
    write(user / "nebula-launch", "", true);
    const std::string share = (temp / "share").string();
    check(script_install_of(user / "versions" / "1.0.0" / "nebula", share.c_str(), nullptr).has_value(),
          "the user install in XDG_DATA_HOME is taken");
    check(!script_install_of(user / "versions" / "1.0.0" / "nebula", nullptr, (temp / "elsewhere").c_str()),
          "a user install install.sh would not update is not");

    // The update into the portable folder.
    const fs::path staging = temp / "data" / "update-staging";
    fs::create_directories(staging);
    fs::path package = make_package(staging / "make", "2.0.0", true, true);
    std::string error;
    check(install_with_script(package, *portable, staging / "unpacked", error), "the update installs");
    if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
    const std::string current = read(root / "current.json");
    check(current.find("\"version\": \"2.0.0\"") != std::string::npos && current.find("\"previous\": \"1.0.0\"") != std::string::npos,
          "current.json names the new version and the one before");
    check(fs::is_regular_file(root / "versions" / "2.0.0" / "nebula") && fs::is_regular_file(running),
          "the new version beside the running one");
    check(read(root / "content" / "game.pak") == "game", "the game stays");
    check(!fs::exists(package) && !fs::exists(staging / "unpacked"), "nothing of the package is left");

    // The restart: the new program, its folder in the library path where the running one's was.
    check(next_program_of(running) == root / "versions" / "2.0.0" / "nebula", "the restart starts the new version");
    check(next_program_of(root / "versions" / "2.0.0" / "nebula").empty(), "the new version starts itself");
    check(library_path_for((root / "versions" / "1.0.0").string() + ":/usr/lib", root / "versions" / "1.0.0",
                           root / "versions" / "2.0.0") == (root / "versions" / "2.0.0").string() + ":/usr/lib",
          "the library path follows");
    check(library_path_for("", root, root) == "" && library_path_for(":/a:", "/b", "/c") == ":/a:", "other library paths stay");

    // Failures say why, and leave the install as it was.
    package = make_package(staging / "make", "3.0.0", false, true);
    error.clear();
    check(!install_with_script(package, *portable, staging / "unpacked", error) &&
              error.find("this package is for") != std::string::npos,
          "a package for another processor: install.sh's reason");
    check(read(root / "current.json") == current, "and the install is as it was");
    package = make_package(staging / "make", "3.0.1", true, false);
    error.clear();
    check(!install_with_script(package, *portable, staging / "unpacked", error) && error == "the package holds no install.sh",
          "a package without install.sh");
    write(staging / "broken.partial", "not an archive");
    error.clear();
    check(!install_with_script(staging / "broken.partial", *portable, staging / "unpacked", error) &&
              error == "the package could not be unpacked",
          "a package that is not an archive");

    std::error_code ignored;
    fs::remove_all(temp, ignored);
    std::printf("%s\n", g_ok ? "linux update install: every case holds" : "linux update install: FAILED");
    return g_ok ? 0 : 1;
}
