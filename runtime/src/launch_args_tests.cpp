// Tests for galaxy/launch_args.h: the names --diagnostics takes.
#include "galaxy/launch_args.h"

#include <iostream>
#include <string_view>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool parses(std::string_view list, unsigned expected) {
    unsigned what = ~0u;
    std::string_view unknown;
    return galaxy::diagnostics_from_text(list, what, unknown) && what == expected;
}

bool refuses(std::string_view list, std::string_view named) {
    unsigned what = 0u;
    std::string_view unknown;
    return !galaxy::diagnostics_from_text(list, what, unknown) && unknown == named;
}

}  // namespace

int main() {
    using namespace galaxy;
    bool ok = true;
    ok &= expect(parses("present-stats,thread-cpu", kDiagnosePresentStats | kDiagnoseThreadCpu), "two names");
    ok &= expect(parses("1", kDiagnoseTimeline | kDiagnoseFrameTails | kDiagnosePointerLatency | kDiagnosePresentStats |
                                 kDiagnoseGpuPasses),
                 "the launcher's recording");
    ok &= expect(parses("0", 0u), "nothing");
    ok &= expect(parses("", 0u), "an empty list");
    ok &= expect(parses("frames", kDiagnoseFrames), "a set");
    ok &= expect(parses("heaps", kDiagnoseMemory | kDiagnoseHeaps), "heaps include memory");
    ok &= expect(parses("loads,profile", kDiagnoseSceneLoads | kDiagnoseFileReads | kDiagnoseProfile), "a set and a name");
    ok &= expect(refuses("timeline,frame", "frame"), "a prefix is not a name");
    ok &= expect(refuses("Timeline", "Timeline"), "names are lower case");
    ok &= expect(refuses("memory,", ""), "an empty name after a comma");
    unsigned every = 0u;
    for (const DiagnosticName& entry : kDiagnosticNames) {
        every |= entry.what;
    }
    ok &= expect(every == (kDiagnoseTelemetry << 1u) - 1u, "every diagnostic has a name");
    std::cout << (ok ? "launch args tests passed\n" : "launch args tests failed\n");
    return ok ? 0 : 1;
}
