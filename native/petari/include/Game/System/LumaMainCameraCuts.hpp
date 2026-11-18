#pragma once

// The main camera's cuts (draw.view, M-068): where the view a mod holds jumps, so nothing blends across it, neither the
// picture between two frames (MotionSignal::cameraCut) nor the player's animation (MarioActor::updateCameraInfo). A
// turn or a move of the held view is never one, however fast (a flick of the player's look): the cuts are the view's
// first showing, another mod taking it, its showing in a new scene's context, a cut its mod states (main_cut), and the
// game's own view showing again. Header only, so the native tests run it as LumaDrawView does.

#include <stdint.h>

namespace luma {
    namespace draw {
        namespace view {
            class MainCuts {
            public:
                constexpr MainCuts() = default;

                // main_eye or main_rig set a view from `pMod`: a holder other than the one before (none, or another mod)
                // is a cut at the next showing.
                void held(const void* pMod) {
                    if (pMod != mHolder) {
                        mHolder = pMod;
                        mDue = true;
                    }
                }

                // main_cut from `pMod`, or a new scene's context for the holder: false when `pMod` does not hold the main
                // camera (the view it would cut is the game's own).
                bool asked(const void* pMod) {
                    if (pMod == nullptr || pMod != mHolder) {
                        return false;
                    }
                    mDue = true;
                    return true;
                }

                // The held view put in the scene's context: true when this showing is a cut.
                bool shownNow() {
                    mShown = true;
                    if (!mDue) {
                        return false;
                    }
                    mDue = false;
                    mCount++;
                    return true;
                }

                // The main camera given back (null, the mod's end, its rig's actor gone): true when its view had been
                // shown, so the game's own view showing again is a cut.
                bool givenBack() {
                    const bool cut = mShown;
                    mHolder = nullptr;
                    mDue = false;
                    mShown = false;
                    if (cut) {
                        mCount++;
                    }
                    return cut;
                }

                // Whether the scene's context holds the mod's view: from its first showing until it is given back.
                bool shown() const {
                    return mShown;
                }

                // The cuts so far: a reader keeps the count it saw and takes a change as a cut.
                uint32_t count() const {
                    return mCount;
                }

            private:
                const void* mHolder = nullptr;
                bool mDue = false;
                bool mShown = false;
                uint32_t mCount = 0;
            };
        }  // namespace view
    }  // namespace draw
}  // namespace luma
