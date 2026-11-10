// SPDX-License-Identifier: GPL-3.0-only
// The first start on a Mac (docs/APPLE.md, "Setting up"): the game needs the player's own copy of the disc, once. With
// no game.pak where the game reads it, a small window asks for the disc image, has the importer (crates/nebula-import,
// linked into the app: its C functions) check it and build game.pak from it, and shows how far that is. Nothing of
// the disc image is changed, and nothing is fetched from anywhere.
#import <AppKit/AppKit.h>

#include "apple_host.h"
#include "galaxy/asset_store.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <pthread.h>
#include <string>
#include <system_error>
#include <thread>

// crates/nebula-import/src/c_api.rs.
extern "C" {
char* nebula_import_identify(const char* input);
char* nebula_import_build(const char* input, const char* output, bool (*progress)(std::uint64_t, std::uint64_t, void*),
                          void* context);
void nebula_import_free(char* text);
}

namespace {

std::atomic<bool> g_cancel{false};
std::atomic<std::uint64_t> g_done{0};
std::atomic<std::uint64_t> g_total{0};

bool import_progress(std::uint64_t done, std::uint64_t total, void*) {
    g_done.store(done, std::memory_order_relaxed);
    g_total.store(total, std::memory_order_relaxed);
    return !g_cancel.load(std::memory_order_relaxed);
}

enum : NSInteger { kSetupDone = 1, kSetupQuit = 2 };

// The game's data, its own size and some to spare: an empty string when the drive has room for it, else the words.
// `replaced`: the bytes of an earlier game.pak the new one takes the place of.
std::string room_for_the_game(const std::filesystem::path& folder, double replaced) {
    constexpr double kNeeded = 4.4e9;  // game.pak is 4.02 GB (the North American disc's; the others' are close)
    @autoreleasepool {
        NSURL* const url = [NSURL fileURLWithPath:@(folder.c_str()) isDirectory:YES];
        NSNumber* free = nil;
        if (![url getResourceValue:&free forKey:NSURLVolumeAvailableCapacityForImportantUsageKey error:nil] || free == nil) {
            return {};  // not known: the import itself says if it runs out
        }
        if (free.doubleValue + replaced >= kNeeded) {
            return {};
        }
        char words[160];
        std::snprintf(words, sizeof(words), "Nebula needs %.1f GB free on this Mac to set up the game; %.1f GB is free.",
                      kNeeded / 1e9, free.doubleValue / 1e9);
        return words;
    }
}

}  // namespace

@interface NebulaSetup : NSObject <NSWindowDelegate>
@property(nonatomic) NSWindow* window;
@property(nonatomic) NSTextField* words;
@property(nonatomic) NSProgressIndicator* bar;
@property(nonatomic) NSButton* choose;
@property(nonatomic) NSButton* leave;
@property(nonatomic) NSTimer* timer;
@property(nonatomic) BOOL working;
@property(nonatomic) std::string pak;
@end

@implementation NebulaSetup

- (instancetype)initWithPak:(const std::string&)pak again:(BOOL)again {
    self = [super init];
    if (self == nil) {
        return nil;
    }
    _pak = pak;
    const NSRect frame = NSMakeRect(0, 0, 520, 230);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"Nebula";
    _window.releasedWhenClosed = NO;
    _window.restorable = NO;
    _window.delegate = self;
    NSView* const content = _window.contentView;
    // Placed by their alignment rects (what is drawn, without a control's own margins), so that the text, the bar and
    // the buttons' edges line up: Apple's 20 points from the window's edges, 12 between buttons, 8 between lines.
    const CGFloat width = frame.size.width;
    const CGFloat height = frame.size.height;
    const CGFloat margin = 20.0;
    const auto place = [](NSView* view, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
        view.frame = [view frameForAlignmentRect:NSMakeRect(x, y, w, h)];
    };
    _choose = [NSButton buttonWithTitle:@"Choose Disc Image…" target:self action:@selector(chooseImage:)];
    _choose.keyEquivalent = @"\r";
    _leave = [NSButton buttonWithTitle:@"Quit" target:self action:@selector(leave:)];
    const NSSize choose = _choose.intrinsicContentSize;
    const NSSize leave = _leave.intrinsicContentSize;
    const CGFloat chooseWidth = std::max<CGFloat>(choose.width, 96.0);
    const CGFloat leaveWidth = std::max<CGFloat>(leave.width, 96.0);
    place(_choose, width - margin - chooseWidth, margin, chooseWidth, choose.height);
    place(_leave, width - margin - chooseWidth - 12.0 - leaveWidth, margin, leaveWidth, leave.height);
    [content addSubview:_choose];
    [content addSubview:_leave];
    _bar = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    _bar.style = NSProgressIndicatorStyleBar;
    _bar.indeterminate = NO;
    _bar.minValue = 0.0;
    _bar.maxValue = 1.0;
    _bar.hidden = YES;
    const CGFloat barY = margin + choose.height + margin;
    place(_bar, margin, barY, width - margin * 2.0, std::max<CGFloat>(_bar.intrinsicContentSize.height, 6.0));
    [content addSubview:_bar];
    NSTextField* const title = [NSTextField labelWithString:@"Set up Nebula"];
    title.font = [NSFont boldSystemFontOfSize:17];
    const CGFloat titleHeight = title.intrinsicContentSize.height;
    place(title, margin, height - margin - titleHeight, width - margin * 2.0, titleHeight);
    [content addSubview:title];
    _words = [NSTextField
        wrappingLabelWithString:again ? @"Nebula's copy of the game is from an earlier version or is damaged. Choose your Super Mario Galaxy disc image to set it up again."
                                      : @"Choose your Super Mario Galaxy disc image. This is done once."];
    const CGFloat wordsTop = height - margin - titleHeight - 8.0;
    const CGFloat wordsBottom = barY + [_bar alignmentRectForFrame:_bar.frame].size.height + 12.0;
    place(_words, margin, wordsBottom, width - margin * 2.0, wordsTop - wordsBottom);
    [content addSubview:_words];
    [_window center];
    return self;
}

- (void)say:(NSString*)text {
    self.words.stringValue = text;
}

- (void)leave:(id)sender {
    if (self.working) {
        // The importer stops at its next step; the window goes on showing until it has.
        g_cancel.store(true, std::memory_order_relaxed);
        [self say:@"Stopping…"];
        self.leave.enabled = NO;
        return;
    }
    [NSApp stopModalWithCode:kSetupQuit];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    [self leave:sender];
    return NO;
}

- (void)chooseImage:(id)sender {
    NSOpenPanel* const panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;  // a folder the disc was extracted to
    panel.allowsMultipleSelection = NO;
    panel.message = @"Choose your Super Mario Galaxy disc image";
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse result) {
                      if (result == NSModalResponseOK && panel.URL.fileSystemRepresentation != nullptr) {
                          [self importFrom:std::string(panel.URL.fileSystemRepresentation)];
                      }
                  }];
}

- (void)importFrom:(std::string)input {
    self.working = YES;
    self.choose.enabled = NO;
    self.leave.title = @"Cancel";
    self.bar.hidden = NO;
    self.bar.doubleValue = 0.0;
    [self say:@"Checking…"];
    g_cancel.store(false, std::memory_order_relaxed);
    g_done.store(0, std::memory_order_relaxed);
    g_total.store(0, std::memory_order_relaxed);
    self.timer = [NSTimer timerWithTimeInterval:0.1 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [NSRunLoop.currentRunLoop addTimer:self.timer forMode:NSRunLoopCommonModes];
    const std::string output = self.pak;
    // The player asked for it and waits for it: macOS neither slows the app (App Nap, as it does an app whose window
    // is behind others) nor lets the Mac sleep until it is done.
    id const activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated
                                                                     reason:@"Setting up the game from the disc image"];
    std::thread([self, input, output, activity] {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
        const auto began = std::chrono::steady_clock::now();
        const auto seconds = [began] {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        };
        std::string failure;
        // What the image is first: a wrong disc is said in the importer's own words before anything is written.
        char* const identity = nebula_import_identify(input.c_str());
        bool supported = false;
        if (identity != nullptr) {
            @autoreleasepool {
                NSData* const json = [NSData dataWithBytes:identity length:std::strlen(identity)];
                NSDictionary* const fields = [NSJSONSerialization JSONObjectWithData:json options:0 error:nil];
                if ([fields isKindOfClass:NSDictionary.class]) {
                    supported = [fields[@"supported"] boolValue];
                    if (!supported && [fields[@"message"] isKindOfClass:NSString.class]) {
                        failure = [(NSString*)fields[@"message"] UTF8String];
                    }
                }
            }
            nebula_import_free(identity);
        }
        if (!supported && failure.empty()) {
            failure = "This disc image can't be used.";
        }
        std::fprintf(stderr, "[setup] disc image checked in %.1f s\n", seconds());
        if (supported) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self say:@"Setting up…"];
            });
            // Built beside its place and moved there whole: a stopped import leaves no half of a game.pak. What an
            // import that never finished (the Mac turned off) left is cleared first.
            std::error_code error;
            const std::filesystem::path place(output);
            std::filesystem::create_directories(place.parent_path(), error);
            const std::filesystem::path partial = place.string() + ".partial";
            std::filesystem::remove(partial, error);
            std::filesystem::remove(place.parent_path() / ("." + partial.filename().string() + ".importing"), error);
            const std::uintmax_t earlier = std::filesystem::file_size(place, error);
            failure = room_for_the_game(place.parent_path(), error ? 0.0 : static_cast<double>(earlier));
        }
        if (supported && failure.empty()) {
            std::error_code error;
            const std::filesystem::path place(output);
            const std::filesystem::path partial = place.string() + ".partial";
            char* const refused = nebula_import_build(input.c_str(), partial.c_str(), &import_progress, nullptr);
            std::fprintf(stderr, "[setup] game.pak built in %.1f s\n", seconds());
            if (refused != nullptr) {
                failure = g_cancel.load(std::memory_order_relaxed) ? "Stopped." : refused;
                nebula_import_free(refused);
                std::filesystem::remove(partial, error);
            } else {
                std::filesystem::rename(partial, place, error);
                if (error) {
                    failure = "Couldn't save the game's data.";
                } else if (!galaxy::apple::game_data_opens(output)) {
                    // Read back as the game reads it: what was written is what the game will find.
                    std::filesystem::remove(place, error);
                    failure = "The game's data couldn't be read back from this Mac's drive. Try again.";
                }
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSProcessInfo.processInfo endActivity:activity];
            [self finished:failure];
        });
    }).detach();
}

- (void)tick:(NSTimer*)timer {
    const std::uint64_t total = g_total.load(std::memory_order_relaxed);
    if (total != 0u) {
        self.bar.doubleValue = static_cast<double>(g_done.load(std::memory_order_relaxed)) / static_cast<double>(total);
    }
}

- (void)finished:(std::string)failure {
    [self.timer invalidate];
    self.timer = nil;
    self.working = NO;
    if (failure.empty()) {
        [NSApp stopModalWithCode:kSetupDone];
        return;
    }
    self.bar.hidden = YES;
    self.choose.enabled = YES;
    self.leave.enabled = YES;
    self.leave.title = @"Quit";
    [self say:@(failure.c_str())];
}

@end

namespace galaxy::apple {

bool game_data_opens(const std::string& pak) {
    galaxy::assets::AssetStore store;
    if (!store.open(pak)) {
        return false;
    }
    // The releases the game plays (host_main.cpp open_pak).
    const std::string_view id = store.game_id();
    return id == "RMGE01" || id == "RMGP01" || id == "RMGJ01";
}

bool ensure_game_data(int argc, char** argv) {
    std::string pak;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--pak") {
            pak = argv[i + 1];
        }
    }
    std::error_code error;
    if (pak.empty()) {
        return true;
    }
    // A game.pak the game can open is the game's; one of an earlier store version (an update changed what the
    // importer writes) or a damaged one is set up again from the disc image, as the game cannot use it.
    const bool present = std::filesystem::exists(pak, error);
    if (present && game_data_opens(pak)) {
        return true;
    }
    if (present) {
        std::fprintf(stderr, "[setup] %s is not a game.pak this version of Nebula reads\n", pak.c_str());
    }
    // A test run or a run with no screen has nobody to ask (but a test of the setup itself).
    bool tested = false;
#if defined(NEBULA_DEV)
    tested = std::getenv("NEBULA_TEST_SETUP_IMAGE") != nullptr;
#endif
    for (int i = 1; i < argc && !tested; ++i) {
        if (std::string(argv[i]) == "--seconds") {
            return true;
        }
    }
    @autoreleasepool {
        start_app();  // the app as the game has it, its delegate first
        NebulaSetup* const setup = [[NebulaSetup alloc] initWithPak:pak again:present];
        [setup.window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
#if defined(NEBULA_DEV)
        // A test of the first start with nobody at the window: the disc image as if it had been chosen.
        if (const char* const image = std::getenv("NEBULA_TEST_SETUP_IMAGE"); image != nullptr) {
            const std::string chosen(image);
            dispatch_async(dispatch_get_main_queue(), ^{
                [setup importFrom:chosen];
            });
        }
#endif
        const NSModalResponse answer = [NSApp runModalForWindow:setup.window];
        [setup.window orderOut:nil];
        std::fprintf(stderr, "[setup] %s\n", answer == kSetupDone ? "game.pak made from the player's disc image" : "left without setting up");
        return answer == kSetupDone;
    }
}

}  // namespace galaxy::apple
