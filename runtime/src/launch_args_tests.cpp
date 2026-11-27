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

bool parses(std::string_view list, unsigned expected, unsigned share = galaxy::kHintShareDefault) {
    unsigned what = ~0u;
    unsigned hint_share = galaxy::kHintShareDefault;
    std::string_view unknown;
    return galaxy::diagnostics_from_text(list, what, hint_share, unknown) && what == expected && hint_share == share;
}

bool refuses(std::string_view list, std::string_view named) {
    unsigned what = 0u;
    unsigned hint_share = galaxy::kHintShareDefault;
    std::string_view unknown;
    return !galaxy::diagnostics_from_text(list, what, hint_share, unknown) && unknown == named;
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
    ok &= expect(every == (kDiagnoseHintPipeline << 1u) - 1u, "every diagnostic has a name");
    // The hint's target as a share of its cycle (G10.4): a whole per cent from 10 to 100, 100 when not given.
    ok &= expect(LaunchArgs{}.hint_share == 100u && kHintShareDefault == 100u, "the hint's share is 100 by default");
    ok &= expect(parses("timeline", kDiagnoseTimeline, 100u), "a list without the share leaves it at 100");
    ok &= expect(parses("hint-share=80", 0u, 80u), "the share 80");
    ok &= expect(parses("hint-share=10", 0u, 10u) && parses("hint-share=100", 0u, 100u) && parses("hint-share=67", 0u, 67u),
                 "the share's ends, 10 and 100, and 67");
    ok &= expect(parses("hint-share=20,timeline", kDiagnoseTimeline, 20u), "the share beside a name");
    ok &= expect(refuses("hint-share=0", "hint-share=0"), "the share 0 is refused");
    ok &= expect(refuses("hint-share=101", "hint-share=101"), "the share 101 is refused");
    ok &= expect(refuses("hint-share=9", "hint-share=9"), "a share under 10 is refused");
    ok &= expect(refuses("hint-share=eighty", "hint-share=eighty"), "a share in words is refused");
    ok &= expect(refuses("hint-share=8x", "hint-share=8x") && refuses("hint-share=-80", "hint-share=-80") &&
                     refuses("hint-share=080", "hint-share=080") && refuses("hint-share=", "hint-share=") &&
                     refuses("hint-share=1000", "hint-share=1000"),
                 "a share that is not a plain whole number is refused");
    ok &= expect(refuses("hint-share", "hint-share"), "the share's name alone is refused");
    ok &= expect(parses("hint-per-step", kDiagnoseHintPerStep), "the one-report-a-step rule has its name");
    ok &= expect(parses("hint-pipeline", kDiagnoseHintPipeline), "the graphics-pipeline session has its name");
    ok &= expect(parses("hint-share=90,hint-pipeline", kDiagnoseHintPipeline, 90u), "a share and a variant together");
    ok &= expect(parses("old-governors", kDiagnoseOldGovernors), "the old governors have their name");
    ok &= expect(parses("pace-fault", kDiagnosePaceFault), "the planted pacing fault has its name");
    ok &= expect(parses("pace-depth", kDiagnosePaceDepth), "the planted deeper queue has its name");
    ok &= expect(parses("pace-fault,pace-depth", kDiagnosePaceFault | kDiagnosePaceDepth), "both planted faults at once");
    ok &= expect(refuses("pace", "pace"), "a prefix of pace-fault is not a name");
    ok &= expect(parses("constants-watch", kDiagnoseConstantsWatch), "the constants watch has its name");
    ok &= expect(parses("slot-spin", kDiagnoseSlotSpin), "the slot spin has its name");
    std::cout << (ok ? "launch args tests passed\n" : "launch args tests failed\n");
    return ok ? 0 : 1;
}
