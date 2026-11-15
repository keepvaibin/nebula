// The game's own save check (native/petari/include/Game/System/SaveFileFormat.hpp) over this corpus: prints
// "<file> <answer>" and how many differ from expected.txt. Build and run from the repository root:
//   clang++ -std=c++17 -I native/petari/include -o game_check crates/luma-validate/tests/data/saves/game_check.cpp
//   ./game_check crates/luma-validate/tests/data/saves
#include "Game/System/SaveFileFormat.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    static const char* names[] = {"None", "Short", "EncryptedExport", "Version", "Length", "Table", "Chunk", "Checksum"};
    std::ifstream list(std::string(argv[1]) + "/expected.txt");
    std::string file, want;
    int wrong = 0, count = 0;
    while (list >> file >> want) {
        std::ifstream in(std::string(argv[1]) + "/" + file, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        NebulaSaveFormat::Header header;
        const char* got = names[int(NebulaSaveFormat::validate(data.empty() ? nullptr : data.data(), data.size(), header))];
        std::printf("%s %s%s\n", file.c_str(), got, want == got ? "" : "  <-- expected differently");
        wrong += want != got;
        ++count;
    }
    std::printf("%d files, %d differ\n", count, wrong);
    return wrong != 0 || argc < 2;
}
