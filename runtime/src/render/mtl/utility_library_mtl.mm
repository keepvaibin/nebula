// SPDX-License-Identifier: GPL-3.0-only
// The Metal renderer's own functions (utility_msl.h renderer_function): from the app's utility.metallib, compiled at build
// by platform/apple/tools/compile_metallib.py, each under the name of its text's hash. A text the library lacks (a build
// made without Apple's Metal compiler) is compiled from its text, and each such compile is logged.
#import <Metal/Metal.h>

#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/mtl/utility_msl.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace galaxy::render::mtl {

namespace {

// The functions made for the one device, shared by the threads that ask (the renderer's start runs several).
struct Functions {
    std::mutex lock;
    id<MTLDevice> device = nil;  // the device they were made for: another starts them again
    id<MTLLibrary> library = nil;
    bool library_tried = false;
    std::unordered_map<const char*, id<MTLFunction>> made;  // by the text's address; nil: it could not be made
    unsigned from_library = 0u;
    unsigned from_text = 0u;
    unsigned failed = 0u;
    std::uint64_t prepare_us = 0u;
};

Functions& functions() {
    static Functions state;
    return state;
}

std::uint64_t now_us() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// utility.metallib: in the app's Resources, or beside a program that is not in an app (a development build).
id<MTLLibrary> open_library(id<MTLDevice> device) {
    @autoreleasepool {
        NSURL* url = [NSBundle.mainBundle URLForResource:@"utility" withExtension:@"metallib"];
        if (url == nil) {
            url = [[NSBundle.mainBundle.executableURL URLByDeletingLastPathComponent] URLByAppendingPathComponent:@"utility.metallib"];
        }
        NSError* error = nil;
        id<MTLLibrary> const library = url != nil ? [device newLibraryWithURL:url error:&error] : nil;
        if (library == nil) {
            std::fprintf(stderr, "[metal] utility.metallib is not read: %s\n",
                         error != nil ? error.localizedDescription.UTF8String : "it is not with the app");
        }
        return library;
    }
}

}  // namespace

void* renderer_function(const char* text) {
    Functions& state = functions();
    id<MTLDevice> const device = (__bridge id<MTLDevice>)Context::get().device();
    if (text == nullptr || device == nil) {
        return nullptr;
    }
    id<MTLLibrary> library = nil;
    {
        const std::lock_guard<std::mutex> guard(state.lock);
        if (state.device != device) {
            state.made.clear();
            state.device = device;
            state.library = nil;
            state.library_tried = false;
            state.from_library = state.from_text = state.failed = 0u;
        }
        if (const auto it = state.made.find(text); it != state.made.end()) {
            return it->second != nil ? (__bridge_retained void*)it->second : nullptr;
        }
        if (!state.library_tried) {
            state.library_tried = true;
            state.library = open_library(device);
        }
        library = state.library;
    }
    // Taken or compiled outside the lock: the other threads' go on meanwhile; of two that make the same one, one is kept.
    const UtilityFunctionName name = utility_function_name(text);
    id<MTLFunction> function = nil;
    bool compiled = false;
    @autoreleasepool {
        if (library != nil) {
            function = [library newFunctionWithName:@(name.text)];
        }
        if (function == nil) {
            compiled = true;
            MTLCompileOptions* const options = [MTLCompileOptions new];
            options.fastMathEnabled = NO;
            options.languageVersion = MTLLanguageVersion2_4;
            NSError* error = nil;
            id<MTLLibrary> const text_library = [device newLibraryWithSource:@(text) options:options error:&error];
            function = text_library != nil ? [text_library newFunctionWithName:@"main0"] : nil;
            std::fprintf(stderr, "[metal] a renderer shader compiled from its text (%s %s): %s\n", name.text,
                         library != nil ? "is not in utility.metallib" : "has no library",
                         function != nil ? "made" : error != nil ? error.localizedDescription.UTF8String : "no main0");
        }
    }
    const std::lock_guard<std::mutex> guard(state.lock);
    if (state.device != device) {
        return function != nil ? (__bridge_retained void*)function : nullptr;
    }
    const auto [at, inserted] = state.made.emplace(text, function);
    if (inserted) {
        if (function == nil) {
            ++state.failed;
        } else if (compiled) {
            ++state.from_text;
        } else {
            ++state.from_library;
        }
    }
    return at->second != nil ? (__bridge_retained void*)at->second : nullptr;
}

void prepare_renderer_functions() {
    const std::uint64_t began = now_us();
    for (const char* const text : renderer_msl_texts()) {
        release(renderer_function(text));
    }
    Functions& state = functions();
    const std::lock_guard<std::mutex> guard(state.lock);
    state.prepare_us = now_us() - began;
}

void report_renderer_functions() {
    Functions& state = functions();
    const std::lock_guard<std::mutex> guard(state.lock);
    std::fprintf(stderr, "[metal] renderer shaders: %u from utility.metallib, %u compiled from their text, %u failed (%u texts, %.1f ms)\n",
                 state.from_library, state.from_text, state.failed, static_cast<unsigned>(renderer_msl_texts().size()),
                 static_cast<double>(state.prepare_us) / 1000.0);
}

}  // namespace galaxy::render::mtl
