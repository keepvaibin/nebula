// Standalone, throwaway verification tool (not part of the CMake build):
// loads a real FileSelect.arc from the user's own game dump and dumps
// decoded texture stats + a PPM, to empirically cross-check
// game_asset_extract.cpp against the independently-validated Python
// reference implementation before wiring it into the renderer.
#include "galaxy/gx/game_asset_extract.h"

#include <cstdio>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: verify_asset_extract <content_root>\n";
        return 1;
    }
    const std::string root = argv[1];
    const char* names[] = {
        "myfileselwinframe.tpl", "mybuttonatypeside.tpl", "my2picon.tpl"};
    for (const char* name : names) {
        const auto tex = galaxy::gx::load_game_texture(
            root, "files\\LayoutData\\FileSelect.arc", name);
        if (!tex.has_value()) {
            std::cout << name << ": FAILED to load\n";
            continue;
        }
        std::cout << name << ": " << tex->width << "x" << tex->height
                  << " bytes=" << tex->rgba8.size() << '\n';
        std::string ppm_path = std::string("generated\\diagnostics\\") + name + ".ppm";
        std::ofstream out(ppm_path, std::ios::binary);
        out << "P6\n" << tex->width << " " << tex->height << "\n255\n";
        for (std::size_t i = 0; i < tex->rgba8.size(); i += 4) {
            out.put(static_cast<char>(tex->rgba8[i]));
            out.put(static_cast<char>(tex->rgba8[i + 1]));
            out.put(static_cast<char>(tex->rgba8[i + 2]));
        }
        std::cout << "  wrote " << ppm_path << '\n';
    }
    return 0;
}
