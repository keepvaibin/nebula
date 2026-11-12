// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's system services on Apple's systems (contract services-system 3 to 6): the facts, the pasteboard and links
// on the main queue, answered on a utility queue (which also decodes and encodes), and macOS's display snapshot from
// the game window's delegate (host_window_apple.mm). Compiled with ARC.

#include "galaxy/luma_system_platform.h"

#include <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#endif
#import <Foundation/Foundation.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace galaxy::luma_system {
namespace {

constexpr std::uint64_t kTextMost = 16ull << 20u;     // services-system 1 item 5
constexpr std::uint64_t kEncodedMost = 80ull << 20u;  // a picture's file within 64 MiB of pixels, with its headers

// ---- Facts (section 3), read once ------------------------------------------------------------------------------

std::once_flag g_facts_once;
LumaSystemFacts g_facts{};

void read_facts() noexcept {
    @autoreleasepool {
#if TARGET_OS_IOS
        g_facts.platform = LUMA_SYSTEM_IOS;
#else
        g_facts.platform = LUMA_SYSTEM_MACOS;
#endif
#if defined(__aarch64__)
        g_facts.arch = LUMA_SYSTEM_ARM64;
#else
        g_facts.arch = LUMA_SYSTEM_X64;
#endif
        g_facts.renderer = LUMA_SYSTEM_METAL;
        g_facts.clipboard_images = 1u;
        NSString* const language = NSLocale.preferredLanguages.firstObject;  // BCP 47: "en-US", "zh-Hant-TW"
        if (language != nil) {
            std::snprintf(g_facts.locale, sizeof(g_facts.locale), "%s", language.UTF8String);
        }
        NSString* const version = NSProcessInfo.processInfo.operatingSystemVersionString;  // "Version 15.1 (Build 24B83)"
#if TARGET_OS_IOS
        std::snprintf(g_facts.os, sizeof(g_facts.os), "iOS %s", version != nil ? version.UTF8String : "");
#else
        std::snprintf(g_facts.os, sizeof(g_facts.os), "macOS %s", version != nil ? version.UTF8String : "");
#endif
    }
}

void facts(LumaSystemFacts& out) noexcept {
    try {
        std::call_once(g_facts_once, read_facts);
    } catch (...) {
        return;
    }
    out.platform = g_facts.platform;
    out.arch = g_facts.arch;
    out.renderer = g_facts.renderer;
    out.clipboard_images = g_facts.clipboard_images;
    std::memcpy(out.locale, g_facts.locale, sizeof(out.locale));
    std::memcpy(out.os, g_facts.os, sizeof(out.os));
}

// ---- Queues ----------------------------------------------------------------------------------------------------

dispatch_queue_t work_queue() {
    return dispatch_get_global_queue(QOS_CLASS_UTILITY, 0);
}

// An answer from the utility queue (never the main queue: a done may decode a picture). Big data travels in a
// shared_ptr so the blocks copy a pointer, not the bytes.
void answer_status(std::function<void(std::int32_t)> done, std::int32_t status) {
    dispatch_async(work_queue(), ^{
      done(status);
    });
}
void answer_text(std::function<void(std::int32_t, std::string)> done, std::int32_t status, std::shared_ptr<std::string> text) {
    dispatch_async(work_queue(), ^{
      done(status, text != nullptr ? std::move(*text) : std::string{});
    });
}
void answer_image(std::function<void(std::int32_t, Image)> done, std::int32_t status, std::shared_ptr<Image> image) {
    dispatch_async(work_queue(), ^{
      done(status, image != nullptr ? std::move(*image) : Image{});
    });
}

std::shared_ptr<Image> encoded_of(NSData* data) {
    auto image = std::make_shared<Image>();
    const auto* bytes = static_cast<const std::uint8_t*>(data.bytes);
    image->encoded.assign(bytes, bytes + data.length);
    return image;
}

// ---- The pasteboard and links ----------------------------------------------------------------------------------

void read_text(std::function<void(std::int32_t, std::string)> done) {
    dispatch_async(dispatch_get_main_queue(), ^{
      @autoreleasepool {
#if TARGET_OS_IOS
          NSString* const string = UIPasteboard.generalPasteboard.string;
#else
          NSString* const string = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
#endif
          if (string == nil || string.length == 0u) {
              answer_text(done, LUMA_E_NOT_FOUND, nullptr);
              return;
          }
          const NSUInteger bytes = [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
          if (bytes > kTextMost) {
              answer_text(done, LUMA_E_TOO_LARGE, nullptr);
              return;
          }
          const char* const utf8 = string.UTF8String;
          if (utf8 == nullptr) {
              answer_text(done, LUMA_E_IO, nullptr);
              return;
          }
          try {
              answer_text(done, LUMA_OK, std::make_shared<std::string>(utf8, std::strlen(utf8)));
          } catch (...) {
              answer_text(done, LUMA_E_MEMORY, nullptr);
          }
      }
    });
}

void write_text(std::string text, std::function<void(std::int32_t)> done) {
    auto held = std::make_shared<std::string>(std::move(text));
    dispatch_async(dispatch_get_main_queue(), ^{
      @autoreleasepool {
          NSString* const string = [[NSString alloc] initWithBytes:held->data() length:held->size() encoding:NSUTF8StringEncoding];
          if (string == nil) {
              answer_status(done, LUMA_E_IO);
              return;
          }
#if TARGET_OS_IOS
          UIPasteboard.generalPasteboard.string = string;
          answer_status(done, LUMA_OK);
#else
          NSPasteboard* const pasteboard = NSPasteboard.generalPasteboard;
          [pasteboard clearContents];
          answer_status(done, [pasteboard setString:string forType:NSPasteboardTypeString] ? LUMA_OK : LUMA_E_IO);
#endif
      }
    });
}

void read_image(std::function<void(std::int32_t, Image)> done) {
    dispatch_async(dispatch_get_main_queue(), ^{
      @autoreleasepool {
#if TARGET_OS_IOS
          UIPasteboard* const pasteboard = UIPasteboard.generalPasteboard;
          if (!pasteboard.hasImages) {
              answer_image(done, LUMA_E_NOT_FOUND, nullptr);
              return;
          }
          UIImage* const picture = pasteboard.image;
          if (picture == nil) {
              answer_image(done, LUMA_E_IO, nullptr);
              return;
          }
          // A PNG of it on the utility queue, then decoded there by the service.
          dispatch_async(work_queue(), ^{
            @autoreleasepool {
                NSData* const png = UIImagePNGRepresentation(picture);
                if (png == nil) {
                    done(LUMA_E_FORMAT, {});
                } else if (png.length > kEncodedMost) {
                    done(LUMA_E_TOO_LARGE, {});
                } else {
                    done(LUMA_OK, std::move(*encoded_of(png)));
                }
            }
          });
#else
          NSPasteboard* const pasteboard = NSPasteboard.generalPasteboard;
          NSData* const png = [pasteboard dataForType:NSPasteboardTypePNG];
          NSData* const tiff = png == nil ? [pasteboard dataForType:NSPasteboardTypeTIFF] : nil;
          if (png == nil && tiff == nil) {
              answer_image(done, LUMA_E_NOT_FOUND, nullptr);
              return;
          }
          if ((png != nil ? png.length : tiff.length) > kEncodedMost) {
              answer_image(done, LUMA_E_TOO_LARGE, nullptr);
              return;
          }
          if (png != nil) {
              answer_image(done, LUMA_OK, encoded_of(png));
              return;
          }
          // A TIFF becomes a PNG on the utility queue, then the service decodes it there.
          dispatch_async(work_queue(), ^{
            @autoreleasepool {
                NSBitmapImageRep* const rep = [NSBitmapImageRep imageRepWithData:tiff];
                NSData* const converted = rep != nil ? [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] : nil;
                if (converted == nil) {
                    done(LUMA_E_FORMAT, {});
                } else {
                    done(LUMA_OK, std::move(*encoded_of(converted)));
                }
            }
          });
#endif
      }
    });
}

void write_image(Image image, std::function<void(std::int32_t)> done) {
    auto held = std::make_shared<Image>(std::move(image));
    dispatch_async(work_queue(), ^{
      @autoreleasepool {
          if (!encode_png(*held)) {
              done(LUMA_E_MEMORY);
              return;
          }
          NSData* const png = [NSData dataWithBytes:held->encoded.data() length:held->encoded.size()];
#if TARGET_OS_IOS
          UIImage* const picture = [UIImage imageWithData:png];
          if (picture == nil) {
              done(LUMA_E_FORMAT);
              return;
          }
          dispatch_async(dispatch_get_main_queue(), ^{
            UIPasteboard.generalPasteboard.image = picture;  // iOS shows its own paste notice to the reader
            answer_status(done, LUMA_OK);
          });
#else
          // PNG and TIFF: what programs that read no PNG paste.
          NSBitmapImageRep* const rep = [NSBitmapImageRep imageRepWithData:png];
          NSData* const tiff = rep != nil ? rep.TIFFRepresentation : nil;
          dispatch_async(dispatch_get_main_queue(), ^{
            @autoreleasepool {
                NSPasteboard* const pasteboard = NSPasteboard.generalPasteboard;
                [pasteboard clearContents];
                bool set = [pasteboard setData:png forType:NSPasteboardTypePNG];
                if (tiff != nil) {
                    set = [pasteboard setData:tiff forType:NSPasteboardTypeTIFF] && set;
                }
                answer_status(done, set ? LUMA_OK : LUMA_E_IO);
            }
          });
#endif
      }
    });
}

void open_link(std::string url, std::function<void(std::int32_t)> done) {
    auto held = std::make_shared<std::string>(std::move(url));
    dispatch_async(dispatch_get_main_queue(), ^{
      @autoreleasepool {
          NSURL* const address = [NSURL URLWithString:[NSString stringWithUTF8String:held->c_str()]];
          if (address == nil) {
              answer_status(done, LUMA_E_IO);
              return;
          }
#if TARGET_OS_IOS
          [UIApplication.sharedApplication openURL:address
                                           options:@{}
                                 completionHandler:^(BOOL opened) {
                                   answer_status(done, opened ? LUMA_OK : LUMA_E_IO);
                                 }];
#else
          answer_status(done, [NSWorkspace.sharedWorkspace openURL:address] ? LUMA_OK : LUMA_E_IO);
#endif
      }
    });
}

const Platform kPlatform = {facts, read_text, read_image, write_text, write_image, open_link};

std::uint32_t rounded(double value) {
    return value > 0.0 ? static_cast<std::uint32_t>(value + 0.5) : 0u;
}

}  // namespace

const Platform* apple_platform() noexcept {
    return &kPlatform;
}

#if !TARGET_OS_IOS
void apple_window_changed(void* window) noexcept {
    @autoreleasepool {
        NSWindow* const nswindow = (__bridge NSWindow*)window;
        if (nswindow == nil) {
            return;
        }
        LumaSystemDisplay display{};
        display.size = sizeof(display);
        NSView* const view = nswindow.contentView;
        const double scale = nswindow.backingScaleFactor > 0.0 ? nswindow.backingScaleFactor : 1.0;
        if (view != nil) {
            const NSSize pixels = [view convertSizeToBacking:view.bounds.size];
            display.width = rounded(pixels.width);
            display.height = rounded(pixels.height);
            const NSEdgeInsets safe = view.safeAreaInsets;  // points: the notch's band in full screen
            display.safe_left = rounded(safe.left * scale);
            display.safe_top = rounded(safe.top * scale);
            display.safe_right = rounded(safe.right * scale);
            display.safe_bottom = rounded(safe.bottom * scale);
        }
        NSScreen* const screen = nswindow.screen != nil ? nswindow.screen : NSScreen.mainScreen;
        if (screen != nil) {
            NSNumber* const number = screen.deviceDescription[@"NSScreenNumber"];
            const CGDirectDisplayID display_id = number != nil ? number.unsignedIntValue : CGMainDisplayID();
            if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display_id)) {
                display.display_width = static_cast<std::uint32_t>(CGDisplayModeGetPixelWidth(mode));
                display.display_height = static_cast<std::uint32_t>(CGDisplayModeGetPixelHeight(mode));
                CGDisplayModeRelease(mode);
            }
            display.refresh_mhz = static_cast<std::uint32_t>(screen.maximumFramesPerSecond) * 1000u;
        }
        display.scale = rounded(scale * 1000.0);
        display.fullscreen = (nswindow.styleMask & NSWindowStyleMaskFullScreen) != 0u ? 1u : 0u;
        display.focused = nswindow.isKeyWindow && !nswindow.isMiniaturized && NSApp.isActive ? 1u : 0u;
        display_changed(display);
    }
}
#endif

}  // namespace galaxy::luma_system
