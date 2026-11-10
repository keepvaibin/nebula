// SPDX-License-Identifier: GPL-3.0-only
// The host on Apple's systems, outside SDL (apple_host.h).
#include "apple_host.h"

#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <CoreAudio/CoreAudio.h>
#endif

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::apple {

namespace {

namespace fs = std::filesystem;

// The arguments main reads, kept for the process's life.
std::vector<std::string> g_argument_text;
std::vector<char*> g_arguments;

fs::path path_of(NSString* text) {
    return text != nil ? fs::path(text.fileSystemRepresentation) : fs::path{};
}

// The data folder as platform_paths.h names it without --data: the user's Library on a Mac, the app's container on an
// iPhone or iPad (NSHomeDirectory there).
fs::path default_data_folder() {
    NSArray<NSString*>* const folders =
        NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (folders.count == 0u) {
        return {};
    }
    return path_of(folders.firstObject) / "Nebula";
}

// The game module in the app: Contents/Frameworks on a Mac, Frameworks on an iPhone or iPad, else beside the program
// (a build folder).
fs::path default_module() {
    NSBundle* const bundle = NSBundle.mainBundle;
    const fs::path frameworks = path_of(bundle.privateFrameworksPath);
    std::error_code error;
    for (const fs::path& candidate : {frameworks / "libsmg_game.dylib", frameworks / "smg_game.framework" / "smg_game"}) {
        if (!frameworks.empty() && fs::exists(candidate, error)) {
            return candidate;
        }
    }
    const fs::path program = path_of(bundle.executablePath);
    return program.empty() ? fs::path{} : program.parent_path() / "libsmg_game.dylib";
}

// The output to <data>/logs/game.log (the run before: game.previous.log) when no terminal, pipe or file takes it: an
// app started from the Finder or the home screen writes to nothing anyone reads.
void redirect_output(const fs::path& data) {
    if (isatty(STDERR_FILENO) != 0 || data.empty()) {
        return;
    }
#if TARGET_OS_OSX
    // A shell or a test that sent the output to a pipe or a file keeps it; launchd gives an app /dev/null.
    char target[1024]{};
    if (fcntl(STDERR_FILENO, F_GETPATH, target) != 0 || std::strcmp(target, "/dev/null") != 0) {
        return;
    }
#endif
    std::error_code error;
    const fs::path logs = data / "logs";
    fs::create_directories(logs, error);
    fs::rename(logs / "game.log", logs / "game.previous.log", error);
    const std::string log = (logs / "game.log").string();
    if (std::freopen(log.c_str(), "w", stdout) != nullptr) {
        dup2(fileno(stdout), STDERR_FILENO);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
}

// The system's first language as LANG ("fr_CA.UTF-8"), which the game's language follows (runtime_settings.cpp): an app
// has no locale variables of its own.
void name_language() {
    if (std::getenv("LC_ALL") != nullptr || std::getenv("LANG") != nullptr) {
        return;
    }
    NSString* const language = NSLocale.preferredLanguages.firstObject;
    if (language == nil) {
        return;
    }
    NSLocale* const locale = [NSLocale localeWithLocaleIdentifier:language];
    NSString* const code = locale.languageCode;
    if (code == nil) {
        return;
    }
    NSString* const region = locale.countryCode ?: NSLocale.currentLocale.countryCode;
    NSString* const lang = region != nil ? [NSString stringWithFormat:@"%@_%@.UTF-8", code, region]
                                         : [NSString stringWithFormat:@"%@.UTF-8", code];
    setenv("LANG", lang.UTF8String, 0);
}

std::atomic<bool> g_audio_answers{true};
std::atomic<bool> g_audio_checked{false};

#if TARGET_OS_OSX
// A Mac's audio system can stop answering (a driver that hangs in the kernel): every program that asks it for its
// devices then waits for ever, and so would the game at its start. Asked here first, on a thread of its own: without an
// answer in two seconds this run has no sound device (SDL's dummy driver) and says so. An SDL_AUDIO_DRIVER already
// chosen stays.
void check_audio_answers() {
    g_audio_checked.store(true, std::memory_order_relaxed);
    if (std::getenv("SDL_AUDIO_DRIVER") != nullptr) {
        return;
    }
    const dispatch_semaphore_t answered = dispatch_semaphore_create(0);
    [NSThread detachNewThreadWithBlock:^{
        const AudioObjectPropertyAddress devices{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                                 kAudioObjectPropertyElementMain};
        UInt32 size = 0;
        (void)AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devices, 0, nullptr, &size);
        dispatch_semaphore_signal(answered);
    }];
    if (dispatch_semaphore_wait(answered, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) != 0) {
        std::fprintf(stderr, "[audio] the system's audio does not answer: this run has no sound (restarting the Mac "
                             "brings it back)\n");
        setenv("SDL_AUDIO_DRIVER", "dummy", 1);
        g_audio_answers.store(false, std::memory_order_relaxed);
    }
}
#endif

}  // namespace

void launch_arguments(int& argc, char**& argv) {
    @autoreleasepool {
        fs::path data;
        bool has_data = false, has_pak = false, has_saves = false, has_module = false;
        g_argument_text.clear();
        for (int i = 0; i < argc; ++i) {
            // The Finder's own argument (-psn_...) and the debugger's (-NSDocumentRevisionsDebugMode YES) are not the game's.
            const std::string_view argument = argv[i];
            if (i > 0 && argument.starts_with("-psn_")) {
                continue;
            }
            if (i > 0 && argument.starts_with("-NS") && i + 1 < argc) {
                ++i;
                continue;
            }
            if (i + 1 < argc) {
                has_data = has_data || argument == "--data";
                has_pak = has_pak || argument == "--pak";
                has_saves = has_saves || argument == "--saves";
                has_module = has_module || argument == "--module";
                if (argument == "--data") {
                    data = argv[i + 1];
                }
            }
            g_argument_text.emplace_back(argument);
        }
        if (!has_data) {
            data = default_data_folder();
            if (!data.empty()) {
                g_argument_text.insert(g_argument_text.end(), {"--data", data.string()});
            }
        }
        if (!data.empty()) {
            std::error_code error;
            fs::create_directories(data, error);
            if (!has_pak) {
                g_argument_text.insert(g_argument_text.end(), {"--pak", (data / "game" / "game.pak").string()});
            }
            if (!has_saves) {
                fs::create_directories(data / "saves", error);
                g_argument_text.insert(g_argument_text.end(), {"--saves", (data / "saves").string()});
            }
        }
        if (!has_module) {
            if (const fs::path module = default_module(); !module.empty()) {
                g_argument_text.insert(g_argument_text.end(), {"--module", module.string()});
            }
        }
        redirect_output(data);
        name_language();
#if TARGET_OS_OSX
        check_audio_answers();
#endif
        g_arguments.clear();
        for (std::string& text : g_argument_text) {
            g_arguments.push_back(text.data());
        }
        g_arguments.push_back(nullptr);
        argc = static_cast<int>(g_argument_text.size());
        argv = g_arguments.data();
    }
}

bool audio_answers() noexcept {
    return g_audio_answers.load(std::memory_order_relaxed);
}

}  // namespace galaxy::apple
