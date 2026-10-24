#include "Game/Screen/NebulaMenuFocus.hpp"

#ifdef TARGET_PC
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <nw4r/lyt/pane.h>
#include <revolution/wpad.h>

namespace {
    enum Direction {
        Direction_None,
        Direction_Up,
        Direction_Down,
        Direction_Left,
        Direction_Right,
    };

    const s32 cButtonsMax = 64;
    const s32 cRepeatDelay = 20;      // frames a direction is held before it repeats
    const s32 cRepeatInterval = 6;
    const f32 cStickHold = 0.6f;      // the stick's deflection that counts as a press
    const f32 cPointerHandBack = 12.0f;  // screen units the pointer moves to take the menu back

    // Offered during a frame; at the next update they are the buttons the highlight can move to.
    ButtonPaneController* sOffered[cButtonsMax];
    s32 sOfferedCount = 0;
    ButtonPaneController* sButtons[cButtonsMax];
    s32 sButtonCount = 0;

    const ButtonPaneController* sFocus = nullptr;
    bool sActive = false;
    TVec2f sPointerAnchor(0.0f, 0.0f);  // where the pointer was when the D-pad or stick took over
    Direction sHeld = Direction_None;
    s32 sHeldFrames = 0;
    bool sHadButtons = false;

    f32 absolute(f32 value) {
        return value < 0.0f ? -value : value;
    }

    bool isFocusable(const ButtonPaneController* pButton) {
        return pButton->mIsNebulaFocusable && (pButton->isWait() || pButton->isPointing() || pButton->isNotPointing());
    }

    // The centre of the button's bounding pane in its layout's space (y up), from the pane's last calculated matrix.
    bool getCentre(const ButtonPaneController* pButton, TVec2f* pCentre) {
        const nw4r::lyt::Pane* pPane = pButton->mHost->getLayoutManager()->findPaneByName(pButton->mBoundingPaneName);
        if (pPane == nullptr) {
            return false;
        }
        f32 x = 0.0f;
        f32 y = 0.0f;
        switch (pPane->mBasePosition % 3) {
        case 0:
            x = pPane->mSize.width / 2.0f;
            break;
        case 2:
            x = -pPane->mSize.width / 2.0f;
            break;
        }
        switch (pPane->mBasePosition / 3) {
        case 0:
            y = -pPane->mSize.height / 2.0f;
            break;
        case 2:
            y = pPane->mSize.height / 2.0f;
            break;
        }
        const nw4r::math::MTX34& rMtx = pPane->mGlbMtx;
        pCentre->x = rMtx._00 * x + rMtx._01 * y + rMtx._03;
        pCentre->y = rMtx._10 * x + rMtx._11 * y + rMtx._13;
        return true;
    }

    Direction readDirection() {
        if (MR::testCorePadButtonUp(WPAD_CHAN0)) {
            return Direction_Up;
        }
        if (MR::testCorePadButtonDown(WPAD_CHAN0)) {
            return Direction_Down;
        }
        if (MR::testCorePadButtonLeft(WPAD_CHAN0)) {
            return Direction_Left;
        }
        if (MR::testCorePadButtonRight(WPAD_CHAN0)) {
            return Direction_Right;
        }
        const f32 x = MR::getSubPadStickX(WPAD_CHAN0);
        const f32 y = MR::getSubPadStickY(WPAD_CHAN0);
        if (absolute(x) < cStickHold && absolute(y) < cStickHold) {
            return Direction_None;
        }
        if (absolute(y) >= absolute(x)) {
            return y > 0.0f ? Direction_Up : Direction_Down;
        }
        return x > 0.0f ? Direction_Right : Direction_Left;
    }

    // A press, then repeats while the direction is held.
    Direction readPress() {
        const Direction direction = readDirection();
        if (direction != sHeld) {
            sHeld = direction;
            sHeldFrames = 0;
            return direction;
        }
        if (direction == Direction_None) {
            return Direction_None;
        }
        sHeldFrames++;
        if (sHeldFrames >= cRepeatDelay && (sHeldFrames - cRepeatDelay) % cRepeatInterval == 0) {
            return direction;
        }
        return Direction_None;
    }

    void toVector(Direction direction, f32* pX, f32* pY) {
        *pX = direction == Direction_Right ? 1.0f : direction == Direction_Left ? -1.0f : 0.0f;
        *pY = direction == Direction_Up ? 1.0f : direction == Direction_Down ? -1.0f : 0.0f;
    }

    // With nothing highlighted: the button under the pointer, else the one farthest in the pressed direction.
    const ButtonPaneController* findStart(Direction direction) {
        f32 dx, dy;
        toVector(direction, &dx, &dy);
        const ButtonPaneController* pBest = nullptr;
        f32 bestAlong = 0.0f;
        f32 bestAcross = 0.0f;
        for (s32 i = 0; i < sButtonCount; i++) {
            if (sButtons[i]->isPointing()) {
                return sButtons[i];
            }
        }
        for (s32 i = 0; i < sButtonCount; i++) {
            TVec2f centre;
            if (!getCentre(sButtons[i], &centre)) {
                continue;
            }
            const f32 along = centre.x * dx + centre.y * dy;
            const f32 across = absolute(centre.x * dy - centre.y * dx);
            if (pBest == nullptr || along > bestAlong + 1.0f || (along > bestAlong - 1.0f && across < bestAcross)) {
                pBest = sButtons[i];
                bestAlong = along;
                bestAcross = across;
            }
        }
        return pBest;
    }

    // The nearest button in the pressed direction: distance along it plus twice the distance across it, so a button
    // straight ahead wins over a closer one off to the side. None ahead: the highlight stays.
    const ButtonPaneController* findNext(const ButtonPaneController* pFrom, Direction direction) {
        TVec2f from;
        if (!getCentre(pFrom, &from)) {
            return pFrom;
        }
        f32 dx, dy;
        toVector(direction, &dx, &dy);
        const ButtonPaneController* pBest = pFrom;
        f32 bestScore = 0.0f;
        for (s32 i = 0; i < sButtonCount; i++) {
            TVec2f centre;
            if (sButtons[i] == pFrom || !getCentre(sButtons[i], &centre)) {
                continue;
            }
            const f32 x = centre.x - from.x;
            const f32 y = centre.y - from.y;
            const f32 along = x * dx + y * dy;
            if (along <= 1.0f) {
                continue;
            }
            const f32 score = along + 2.0f * absolute(x * dy - y * dx);
            if (pBest == pFrom || score < bestScore) {
                pBest = sButtons[i];
                bestScore = score;
            }
        }
        return pBest;
    }
};  // namespace

namespace NebulaMenuFocus {
    void update() {
        sButtonCount = 0;
        for (s32 i = 0; i < sOfferedCount; i++) {
            if (isFocusable(sOffered[i])) {
                sButtons[sButtonCount++] = sOffered[i];
            }
        }
        sOfferedCount = 0;

        bool isFocusShown = false;
        for (s32 i = 0; i < sButtonCount; i++) {
            if (sButtons[i] == sFocus) {
                isFocusShown = true;
            }
        }
        if (!isFocusShown) {
            sFocus = nullptr;
        }

        const TVec2f* pPointer = MR::getStarPointerScreenPosition(WPAD_CHAN0);
        if (sActive && MR::isStarPointerInScreen(WPAD_CHAN0) &&
            (absolute(pPointer->x - sPointerAnchor.x) > cPointerHandBack ||
             absolute(pPointer->y - sPointerAnchor.y) > cPointerHandBack)) {
            sActive = false;
            sFocus = nullptr;
        }
        if (sButtonCount == 0) {
            sActive = false;
            sHadButtons = false;
            return;
        }
        if (!sHadButtons) {
            // A direction already held as the menu opens (the stick that was walking) is not a press.
            sHadButtons = true;
            sHeld = readDirection();
            sHeldFrames = 0;
            return;
        }

        const Direction press = readPress();
        if (press == Direction_None) {
            return;
        }
        const ButtonPaneController* pNext = sFocus != nullptr ? findNext(sFocus, press) : findStart(press);
        if (pNext != nullptr) {
            sFocus = pNext;
            sActive = true;
            sPointerAnchor = *pPointer;
        }
    }

    void offer(ButtonPaneController* pButton) {
        for (s32 i = 0; i < sOfferedCount; i++) {
            if (sOffered[i] == pButton) {
                return;
            }
        }
        if (sOfferedCount < cButtonsMax) {
            sOffered[sOfferedCount++] = pButton;
        }
    }

    bool isActive() {
        return sActive;
    }

    bool isFocused(const ButtonPaneController* pButton) {
        return sActive && pButton == sFocus;
    }
};  // namespace NebulaMenuFocus
#endif
