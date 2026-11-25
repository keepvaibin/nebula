// galaxy/game_step.h own_frame_rate (G2.3.7, P-042's Android part): a rate is the game's own frames up to the step rate
// and its allowance of 1, whatever the step rate is (tried at 60 and at a fed 50); above that it is the app's own request.
#include "galaxy/game_step.h"

#include <cstdio>
#include <limits>

namespace {

bool expect(bool condition, const char* what) {
    if (!condition) std::printf("FAILED: %s\n", what);
    return condition;
}

}  // namespace

int main() {
    using galaxy::own_frame_rate;
    bool passed = true;
    const double infinite = std::numeric_limits<double>::infinity();

    // The game's step rate (60).
    passed &= expect(own_frame_rate(60.0), "60 is the game's own at 60 steps");
    passed &= expect(own_frame_rate(30.0), "30 is the game's own");
    passed &= expect(own_frame_rate(61.0), "61 is within the allowance");
    passed &= expect(!own_frame_rate(61.5), "61.5 is the app's own request");
    passed &= expect(!own_frame_rate(90.0), "90 is the app's own request");
    passed &= expect(!own_frame_rate(0.0), "0 (the display's own) is not the game's");
    passed &= expect(!own_frame_rate(-1.0), "a negative rate is not the game's");
    passed &= expect(!own_frame_rate(infinite), "unlimited is not the game's");
    passed &= expect(!own_frame_rate(std::numeric_limits<double>::quiet_NaN()), "NaN is not the game's");

    // A fed step rate of 50: the bound follows it, no 60 or 61 stays.
    passed &= expect(own_frame_rate(50.0, 50.0), "50 is the game's own at 50 steps");
    passed &= expect(own_frame_rate(51.0, 50.0), "51 is within the allowance at 50 steps");
    passed &= expect(!own_frame_rate(52.0, 50.0), "52 is the app's own at 50 steps");
    passed &= expect(!own_frame_rate(60.0, 50.0), "60 is the app's own at 50 steps");

    static_assert(galaxy::own_frame_rate(galaxy::kGameStepRate), "the step rate is the game's own");

    if (!passed) return 1;
    std::printf("game step tests passed\n");
    return 0;
}
