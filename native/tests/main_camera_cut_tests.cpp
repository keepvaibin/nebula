// Game/System/LumaMainCameraCuts.hpp: the main camera's cuts as draw.view counts them (M-068). Before it, a turn of 30
// degrees in one step of a mod's view (a flick of the player's look in first person) was a cut: the picture between two
// frames jumped instead of blending, and the player's animation blend was cut.
#include "Game/System/LumaMainCameraCuts.hpp"

#include <cstdio>

using luma::draw::view::MainCuts;

namespace {
    int failures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            failures++;
        }
    }
}  // namespace

int main() {
    int modA = 0;
    int modB = 0;

    {
        MainCuts cuts;
        expect(!cuts.shown() && cuts.count() == 0, "nothing held: no view shown, no cut");
        expect(!cuts.givenBack() && cuts.count() == 0, "given back before any view: no cut");
        cuts.held(&modA);
        expect(!cuts.shown(), "held, not yet shown: the context holds the game's view");
        expect(!cuts.givenBack() && cuts.count() == 0, "given back before its first showing: no cut");
    }
    {
        MainCuts cuts;
        cuts.held(&modA);
        expect(cuts.shownNow() && cuts.count() == 1, "the first showing of a taken view is a cut");
        expect(cuts.shown(), "shown: the context holds the mod's view");
        for (int frame = 0; frame < 100; frame++) {
            cuts.held(&modA);  // the view set again each frame, turned however far
            expect(!cuts.shownNow(), "the same mod's view set and shown again is never a cut");
            expect(!cuts.shownNow(), "the second showing in a frame (calcAnim) is never a cut");
        }
        expect(cuts.count() == 1, "a view turned for 100 frames: one cut, its first showing");
        expect(cuts.asked(&modA) && cuts.shownNow() && cuts.count() == 2, "main_cut by the holder: the next showing cuts");
        expect(!cuts.shownNow() && cuts.count() == 2, "a stated cut cuts once");
        expect(!cuts.asked(&modB) && !cuts.shownNow() && cuts.count() == 2, "main_cut by a mod that does not hold it: refused");
        expect(!cuts.asked(nullptr) && cuts.count() == 2, "main_cut with no mod: refused");
        cuts.held(&modB);
        expect(cuts.shownNow() && cuts.count() == 3, "another mod taking the main camera: its first showing cuts");
        expect(cuts.givenBack() && cuts.count() == 4 && !cuts.shown(), "given back after a showing: the game's view cuts in");
        expect(!cuts.givenBack() && cuts.count() == 4, "given back twice: one cut");
        expect(!cuts.asked(&modB) && cuts.count() == 4, "main_cut after giving it back: refused");
        cuts.held(&modB);
        expect(cuts.shownNow() && cuts.count() == 5, "taken again by the same mod after giving it back: a cut");
    }
    {
        MainCuts cuts;
        cuts.held(&modA);
        (void)cuts.shownNow();
        expect(cuts.asked(&modA) && cuts.asked(&modA) && cuts.shownNow() && cuts.count() == 2,
               "two main_cut calls before one showing: one cut");
        cuts.held(&modA);
        expect(cuts.asked(&modA), "a new scene's context: the holder's view there is due");
        expect(cuts.givenBack() && cuts.count() == 3, "given back while a cut is due: the game's view cuts in");
        cuts.held(&modA);
        expect(!cuts.givenBack() && cuts.count() == 3, "taken and given back before a showing: no cut, nothing due after");
        expect(!cuts.shownNow() && cuts.count() == 3, "a showing with nothing held: no cut");
    }

    std::printf(failures ? "main_camera_cut_tests: %d failed\n" : "main_camera_cut_tests: pass\n", failures);
    return failures ? 1 : 0;
}
