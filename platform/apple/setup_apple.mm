// SPDX-License-Identifier: GPL-3.0-only
// The first start on a Mac (docs/APPLE.md, "Setting up"): the game needs the player's own copy of the disc, once. With
// no game.pak where the game reads it, a small window asks for the disc image, has the importer (crates/nebula-import,
// linked into the app: its C functions) check it and build game.pak from it, and shows how far that is. Nothing of
// the disc image is changed, and nothing is fetched from anywhere.
#import <AppKit/AppKit.h>

#include "apple_host.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
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

- (instancetype)initWithPak:(const std::string&)pak {
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
    _window.delegate = self;
    NSView* const content = _window.contentView;
    NSTextField* const title = [NSTextField labelWithString:@"Set up Nebula"];
    title.font = [NSFont boldSystemFontOfSize:17];
    title.frame = NSMakeRect(24, 180, 472, 26);
    [content addSubview:title];
    _words = [NSTextField wrappingLabelWithString:@"Choose your Super Mario Galaxy disc image. This is done once."];
    _words.frame = NSMakeRect(24, 92, 472, 80);
    [content addSubview:_words];
    _bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(24, 66, 472, 16)];
    _bar.indeterminate = NO;
    _bar.minValue = 0.0;
    _bar.maxValue = 1.0;
    _bar.hidden = YES;
    [content addSubview:_bar];
    _choose = [NSButton buttonWithTitle:@"Choose Disc Image…" target:self action:@selector(chooseImage:)];
    _choose.frame = NSMakeRect(316, 16, 184, 32);
    _choose.keyEquivalent = @"\r";
    [content addSubview:_choose];
    _leave = [NSButton buttonWithTitle:@"Quit" target:self action:@selector(leave:)];
    _leave.frame = NSMakeRect(212, 16, 100, 32);
    [content addSubview:_leave];
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
    std::thread([self, input, output] {
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
        if (supported) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self say:@"Setting up…"];
            });
            // Built beside its place and moved there whole: a stopped import leaves no half of a game.pak.
            std::error_code error;
            const std::filesystem::path place(output);
            std::filesystem::create_directories(place.parent_path(), error);
            const std::filesystem::path partial = place.string() + ".partial";
            std::filesystem::remove(partial, error);
            char* const refused = nebula_import_build(input.c_str(), partial.c_str(), &import_progress, nullptr);
            if (refused != nullptr) {
                failure = g_cancel.load(std::memory_order_relaxed) ? "Stopped." : refused;
                nebula_import_free(refused);
                std::filesystem::remove(partial, error);
            } else {
                std::filesystem::rename(partial, place, error);
                if (error) {
                    failure = "Couldn't save the game's data.";
                }
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
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

bool ensure_game_data(int argc, char** argv) {
    std::string pak;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--pak") {
            pak = argv[i + 1];
        }
    }
    std::error_code error;
    if (pak.empty() || std::filesystem::exists(pak, error)) {
        return true;
    }
    // A test run or a run with no screen has nobody to ask.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--seconds") {
            return true;
        }
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NebulaSetup* const setup = [[NebulaSetup alloc] initWithPak:pak];
        [setup.window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        const NSModalResponse answer = [NSApp runModalForWindow:setup.window];
        [setup.window orderOut:nil];
        std::fprintf(stderr, "[setup] %s\n", answer == kSetupDone ? "game.pak made from the player's disc image" : "left without setting up");
        return answer == kSetupDone;
    }
}

}  // namespace galaxy::apple
