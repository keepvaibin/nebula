// SPDX-License-Identifier: GPL-3.0-only
// Crash reports: each kind of failure gets its code, the mod read last is named when it is recent, and the report
// text carries the session's mods.

#include "galaxy/crash_report.h"

#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace galaxy::crash;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::string code_of(const Facts& facts) { return classify(facts).code; }

}  // namespace

int main() {
    Facts fault;
    fault.kind = Facts::Kind::Fault;
    fault.in_game = true;
    fault.text = "exception 0xC0000005 at smg_game.dll+0x1234";
    expect(code_of(fault) == "NEB-C01", "a crash in the game's code");

    Facts overflow = fault;
    overflow.stack_overflow = true;
    expect(code_of(overflow) == "NEB-C03", "the game's stack overflowed");

    Facts with_mods = fault;
    with_mods.mods_change_game = true;
    expect(code_of(with_mods) == "NEB-M01", "a game crash with mods that change the game");

    Facts recent = with_mods;
    recent.last_mod_read = "Kaizo: /StageData/RedBlueExGalaxy/RedBlueExGalaxyMap.arc";
    recent.last_mod_seconds = 2.5;
    const Report named = classify(recent);
    expect(std::string(named.code) == "NEB-M02", "a mod's file read just before the crash");
    expect(std::strstr(named.detail, "RedBlueExGalaxyMap.arc") != nullptr, "the mod's file is named");
    Facts old_read = recent;
    old_read.last_mod_seconds = 600.0;
    expect(code_of(old_read) == "NEB-M01", "a mod read long before is not named");

    Facts game_fatal;
    game_fatal.kind = Facts::Kind::GameFatal;
    game_fatal.text = "JKRHeap: out of heap";
    expect(code_of(game_fatal) == "NEB-C02", "the game's own fatal stop");

    Facts host_fault;
    host_fault.kind = Facts::Kind::Fault;
    host_fault.in_game = false;
    host_fault.mods_change_game = true;
    expect(code_of(host_fault) == "NEB-N01", "a crash in Nebula itself is not blamed on mods");

    Facts texture = host_fault;
    texture.activity = Activity::TexturePack;
    texture.activity_file = "C:/mods/Pack/textures/tex1_64x64_abc_14.png";
    const Report pack = classify(texture);
    expect(std::string(pack.code) == "NEB-T01" && std::strstr(pack.detail, "tex1_64x64_abc_14.png") != nullptr,
           "a texture pack image");
    Facts movie = host_fault;
    movie.activity = Activity::Movie;
    movie.activity_file = "C:/mods/Movies/movies/PrologueA.mp4";
    expect(code_of(movie) == "NEB-V01", "a replacement movie");

    Facts gpu;
    gpu.kind = Facts::Kind::Startup;
    gpu.text = "renderer: [RendererD3D12] Present HRESULT=0x887A0005";
    expect(code_of(gpu) == "NEB-G01", "the graphics card was lost mid-game");
    Facts renderer;
    renderer.kind = Facts::Kind::Exception;
    renderer.text = "[RendererD3D12] CreateCommittedResource HRESULT=0x80070057";
    expect(code_of(renderer) == "NEB-G02", "another renderer error");
    Facts memory;
    memory.kind = Facts::Kind::Exception;
    memory.text = "class std::bad_alloc";
    expect(code_of(memory) == "NEB-R01", "out of memory");
    Facts exception;
    exception.kind = Facts::Kind::Exception;
    exception.text = "map/set too long";
    expect(code_of(exception) == "NEB-N02", "an uncaught exception");
    Facts aborted;
    aborted.kind = Facts::Kind::Abort;
    expect(code_of(aborted) == "NEB-N03", "abort");

    const auto startup = [](const char* text) {
        Facts f;
        f.kind = Facts::Kind::Startup;
        f.text = text;
        return code_of(f);
    };
    expect(startup("cannot open D:/Nebula/content/game.pak") == "NEB-S01", "missing content");
    expect(startup("D:/x/game.pak: boot.bin points past the partition") == "NEB-S02", "damaged content");
    expect(startup("cannot load the game module smg_game.dll") == "NEB-S03", "missing module");
    expect(startup("the game module's API version differs from the host's (15)") == "NEB-S03", "module version");
    expect(startup("the renderer did not start: no adapter") == "NEB-S04", "no renderer");
    expect(startup("the settings did not load: bad value") == "NEB-S05", "settings");
    expect(startup("unknown option --x") == "NEB-S06", "options");
    expect(startup("a GX span does not continue the last") == "NEB-N04", "another startup stop");

    // The disc thread's note and the activity of this thread.
    char line[256];
    double seconds = -1.0;
    expect(!last_mod_read(line, sizeof(line), &seconds), "no mod read yet");
    std::thread([] { note_mod_read("Kaizo", "/StageData/A.arc"); }).join();
    expect(last_mod_read(line, sizeof(line), &seconds) && std::string(line) == "Kaizo: /StageData/A.arc" &&
               seconds >= 0.0 && seconds < 5.0,
           "the last mod read");
    char file[256];
    expect(current_activity(file, sizeof(file)) == Activity::None && file[0] == '\0', "no activity");
    {
        ScopedActivity activity(Activity::Movie, "C:/m/PrologueA.mp4");
        expect(current_activity(file, sizeof(file)) == Activity::Movie && std::string(file) == "C:/m/PrologueA.mp4",
               "the thread's activity");
        Activity other = Activity::TexturePack;
        std::thread([&] { other = current_activity(nullptr, 0); }).join();
        expect(other == Activity::None, "another thread's activity is its own");
    }
    expect(current_activity(file, sizeof(file)) == Activity::None, "the activity ends with its scope");

    set_session_mods("  Kaizo (Riivolution, game files)\n", true);
    char text[2048];
    const std::size_t length = format_report(named, "exception 0xC0000005", text, sizeof(text));
    const std::string report(text, length);
    expect(report.starts_with("NEB-M02: A mod's file crashed the game\n"), "the report's first line");
    expect(report.find("Mods this session:\n  Kaizo (Riivolution, game files)") != std::string::npos, "the mods");
    expect(report.find("Technical details:\nexception 0xC0000005") != std::string::npos, "the technical line");
    char tiny[8];
    expect(format_report(named, "", tiny, sizeof(tiny)) == 7u && tiny[7] == '\0', "a short buffer is cut");

    if (failures != 0) {
        std::cerr << failures << " crash report test(s) failed\n";
        return 1;
    }
    std::cout << "crash report tests passed\n";
    return 0;
}
