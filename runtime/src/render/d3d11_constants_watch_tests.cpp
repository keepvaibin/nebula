// The constants watch (d3d11/constants_watch_d3d11.h, ledger P-332) on this machine's Direct3D 11 device: reads of the
// pieces the watch writes, bound as the renderer binds them, print nothing; a deliberately wrong binding (an older
// piece bound in place of the newest, as a binding left from before would be) prints one "[constants] watch" line
// naming what was written and what was read; the watch goes on reading right after it. Without a hardware Direct3D 11
// device it says so and passes nothing else.
#include "galaxy/render/d3d11/constants_watch_d3d11.h"
#include "galaxy/render/renderer_common.h"

#include <d3dcompiler.h>

#include <iostream>
#include <span>
#include <string>

namespace {

using galaxy::render::d3d11::ComPtr;
using galaxy::render::d3d11::ConstantsWatch;
using galaxy::render::d3d11::Context;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// Every command recorded so far has run.
void finish(Context& context) {
    (void)context.wait(context.signal());
}

}  // namespace

int main() {
    Context& context = Context::get();
    if (!context.initialize(std::string{}, false)) {
        std::cout << "no Direct3D 11 device (" << context.failure() << "): the constants watch is not tested here\n";
        return 0;
    }
    const std::string_view source = galaxy::render::constants_watch_shader_source();
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    bool passed = expect(SUCCEEDED(D3DCompile(source.data(), source.size(), "constants_watch", nullptr, nullptr, "main",
                                              "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0u,
                                              &code, &errors)),
                         "the watch's shader compiles");
    ConstantsWatch watch;
    if (passed) {
        const std::span<const unsigned char> bytes(static_cast<const unsigned char*>(code->GetBufferPointer()),
                                                   code->GetBufferSize());
        passed &= expect(watch.initialize(context.device(), bytes), "the watch's objects are made");
    }
    if (passed) {
        ID3D11DeviceContext1* const immediate = context.immediate();
        // Frames as the renderer runs them: each compares the reads before it and writes a piece numbered 1, 2, 3.
        for (int frame = 0; frame < 3; ++frame) {
            watch.frame(immediate);
            finish(context);
        }
        passed &= expect(watch.check(immediate) == 0u && watch.compared() == 3u && watch.differences() == 0u &&
                             watch.last_line().empty(),
                         "the three right reads are each compared, and print nothing");

        // An older piece (4) bound in place of the newest (5).
        const Context::ConstantPlace older = watch.write();
        passed &= expect(watch.record(immediate, older), "the read of piece 4 is recorded");
        finish(context);
        passed &= expect(watch.check(immediate) == 0u && watch.compared() == 4u,
                         "piece 4 read through its own binding is compared and prints nothing");
        const Context::ConstantPlace newest = watch.write();
        passed &= expect(newest.buffer != nullptr && newest.first != older.first, "piece 5 has a place of its own");
        passed &= expect(watch.record(immediate, older), "the wrong binding's read is recorded");
        finish(context);
        passed &= expect(watch.check(immediate) == 1u && watch.compared() == 5u && watch.differences() == 1u,
                         "the wrong binding's read, once its frame has passed, is one difference");
        const std::string& line = watch.last_line();
        std::cout << "the watch printed: " << line << '\n';
        passed &= expect(line.rfind("[constants] watch", 0u) == 0u, "the line starts with [constants] watch");
        passed &= expect(line.find("wrote number 5 ") != std::string::npos && line.find("read number 4 ") != std::string::npos,
                         "the line names the piece written (5) and the piece read (4)");
        passed &= expect(line.find("offset " + std::to_string(newest.first * 16u) + ",") != std::string::npos &&
                             line.find("offset " + std::to_string(older.first * 16u) + " ") != std::string::npos,
                         "the line names both pieces' places in the ring");

        // The watch goes on: the next frames' right reads print nothing more.
        for (int frame = 0; frame < 2; ++frame) {
            watch.frame(immediate);
            finish(context);
        }
        passed &= expect(watch.check(immediate) == 0u && watch.compared() == 7u && watch.differences() == 1u,
                         "after the wrong binding, the next right reads are compared and print nothing more");
    }
    context.shutdown();
    std::cout << (passed ? "constants watch tests passed\n" : "constants watch tests FAILED\n");
    return passed ? 0 : 1;
}
