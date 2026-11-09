#include "Game/Screen/NebulaMenuFocus.hpp"

#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <Game/Ui/Pane.hpp>
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Array.hpp"

namespace {
    using NebulaMenuFocus::Direction;

    const s32 cRepeatDelay = 18;         // frames a direction is held before it repeats
    const s32 cRepeatInterval = 5;       // then a step every this many frames
    const f32 cStickHold = 0.6f;         // the stick's deflection that counts as a press
    const f32 cPointerHandBack = 12.0f;  // screen units the pointer moves to take the menu back
    const s32 cGapFrames = 90;           // frames without a menu that the pad keeps the menus (a question closing)

    // A place the highlight can rest: a game button, or another target (a key) at a centre given with it.
    struct Entry {
        ButtonPaneController* mButton;
        const void* mKey;
        Vec2f mCentre;
    };

    // Offered during a frame; at the next update they are the places the highlight can move to.
    MR::GrowBuffer< Entry > sOffered;
    s32 sOfferedCount = 0;
    MR::GrowBuffer< Entry > sEntries;
    s32 sEntryCount = 0;

    const void* sFocus = nullptr;  // the button or target highlighted
    bool sActive = false;
    Vec2f sPointerAnchor(0.0f, 0.0f);  // where the pointer was when the D-pad or stick took over
    NebulaMenuFocus::Repeat sRepeat = {NebulaMenuFocus::Direction_None, 0};
    bool sHadEntries = false;
    s32 sGapFrames = 0;
    Direction sPress = NebulaMenuFocus::Direction_None;    // this frame's
    Direction sUnmoved = NebulaMenuFocus::Direction_None;  // this frame's, with nothing that way

    f32 absolute(f32 value) {
        return value < 0.0f ? -value : value;
    }

    const void* getKey(const Entry& rEntry) {
        return rEntry.mButton != nullptr ? static_cast< const void* >(rEntry.mButton) : rEntry.mKey;
    }

    bool isFocusable(const Entry& rEntry) {
        if (rEntry.mButton == nullptr) {
            return true;
        }
        const ButtonPaneController* pButton = rEntry.mButton;
        return pButton->mIsNebulaFocusable && (pButton->isWait() || pButton->isPointing() || pButton->isNotPointing());
    }

    // The centre in layout space (y up): a button's bounding pane from its last calculated matrix, or the target's own.
    bool getCentre(const Entry& rEntry, Vec2f* pCentre) {
        if (rEntry.mButton == nullptr) {
            *pCentre = rEntry.mCentre;
            return true;
        }
        const ButtonPaneController* pButton = rEntry.mButton;
        const Ui::Pane* pPane = pButton->mBoundingPane;
        if (pPane == nullptr) {
            return false;
        }
        f32 x = 0.0f;
        f32 y = 0.0f;
        switch (pPane->mBasePosition % 3) {
        case 0:
            x = pPane->GetDrawSize().width / 2.0f;
            break;
        case 2:
            x = -pPane->GetDrawSize().width / 2.0f;
            break;
        }
        switch (pPane->mBasePosition / 3) {
        case 0:
            y = -pPane->GetDrawSize().height / 2.0f;
            break;
        case 2:
            y = pPane->GetDrawSize().height / 2.0f;
            break;
        }
        const Mtx34f& rMtx = pPane->mGlbMtx;
        pCentre->x = rMtx.mMtx[0][0] * x + rMtx.mMtx[0][1] * y + rMtx.mMtx[0][3];
        pCentre->y = rMtx.mMtx[1][0] * x + rMtx.mMtx[1][1] * y + rMtx.mMtx[1][3];
        return true;
    }

    void toVector(Direction direction, f32* pX, f32* pY) {
        *pX = direction == NebulaMenuFocus::Direction_Right ? 1.0f : direction == NebulaMenuFocus::Direction_Left ? -1.0f : 0.0f;
        *pY = direction == NebulaMenuFocus::Direction_Up ? 1.0f : direction == NebulaMenuFocus::Direction_Down ? -1.0f : 0.0f;
    }

    // With nothing highlighted: the button under the pointer, else the place farthest in the pressed direction.
    const Entry* findStart(Direction direction) {
        f32 dx, dy;
        toVector(direction, &dx, &dy);
        const Entry* pBest = nullptr;
        f32 bestAlong = 0.0f;
        f32 bestAcross = 0.0f;
        for (s32 i = 0; i < sEntryCount; i++) {
            if (sEntries[i].mButton != nullptr && sEntries[i].mButton->isPointing()) {
                return &sEntries[i];
            }
        }
        for (s32 i = 0; i < sEntryCount; i++) {
            Vec2f centre;
            if (!getCentre(sEntries[i], &centre)) {
                continue;
            }
            const f32 along = centre.x * dx + centre.y * dy;
            const f32 across = absolute(centre.x * dy - centre.y * dx);
            if (pBest == nullptr || along > bestAlong + 1.0f || (along > bestAlong - 1.0f && across < bestAcross)) {
                pBest = &sEntries[i];
                bestAlong = along;
                bestAcross = across;
            }
        }
        return pBest;
    }

    // The nearest place in the pressed direction: distance along it plus twice the distance across it, so one
    // straight ahead wins over a closer one off to the side. None ahead: the highlight stays.
    const Entry* findNext(const Entry& rFrom, Direction direction) {
        Vec2f from;
        if (!getCentre(rFrom, &from)) {
            return &rFrom;
        }
        f32 dx, dy;
        toVector(direction, &dx, &dy);
        const Entry* pBest = &rFrom;
        f32 bestScore = 0.0f;
        for (s32 i = 0; i < sEntryCount; i++) {
            Vec2f centre;
            if (&sEntries[i] == &rFrom || !getCentre(sEntries[i], &centre)) {
                continue;
            }
            const f32 x = centre.x - from.x;
            const f32 y = centre.y - from.y;
            const f32 along = x * dx + y * dy;
            if (along <= 1.0f) {
                continue;
            }
            const f32 score = along + 2.0f * absolute(x * dy - y * dx);
            if (pBest == &rFrom || score < bestScore) {
                pBest = &sEntries[i];
                bestScore = score;
            }
        }
        return pBest;
    }

    void addOffered(const Entry& rEntry) {
        for (s32 i = 0; i < sOfferedCount; i++) {
            if (getKey(sOffered[i]) == getKey(rEntry)) {
                sOffered[i] = rEntry;
                return;
            }
        }
        Entry* const pOffered = sOffered.room(sOfferedCount + 1);
        pOffered[sOfferedCount++] = rEntry;
    }
};  // namespace

namespace NebulaMenuFocus {
    Direction readDirection() {
        if (MR::isHeldUp(PLAYER_1)) {
            return Direction_Up;
        }
        if (MR::isHeldDown(PLAYER_1)) {
            return Direction_Down;
        }
        if (MR::isHeldLeft(PLAYER_1)) {
            return Direction_Left;
        }
        if (MR::isHeldRight(PLAYER_1)) {
            return Direction_Right;
        }
        const f32 x = MR::getMoveStickX(PLAYER_1);
        const f32 y = MR::getMoveStickY(PLAYER_1);
        if (absolute(x) < cStickHold && absolute(y) < cStickHold) {
            return Direction_None;
        }
        if (absolute(y) >= absolute(x)) {
            return y > 0.0f ? Direction_Up : Direction_Down;
        }
        return x > 0.0f ? Direction_Right : Direction_Left;
    }

    Direction readPress(Repeat* pRepeat) {
        const Direction direction = readDirection();
        if (direction != pRepeat->mHeld) {
            pRepeat->mHeld = direction;
            pRepeat->mHeldFrames = 0;
            return direction;
        }
        if (direction == Direction_None) {
            return Direction_None;
        }
        pRepeat->mHeldFrames++;
        if (pRepeat->mHeldFrames >= cRepeatDelay && (pRepeat->mHeldFrames - cRepeatDelay) % cRepeatInterval == 0) {
            return direction;
        }
        return Direction_None;
    }

    void holdCurrent(Repeat* pRepeat) {
        pRepeat->mHeld = readDirection();
        pRepeat->mHeldFrames = 0;
    }

    void update() {
        sPress = Direction_None;
        sUnmoved = Direction_None;
        sEntryCount = 0;
        Entry* const pEntries = sEntries.room(sOfferedCount);
        for (s32 i = 0; i < sOfferedCount; i++) {
            if (isFocusable(sOffered[i])) {
                pEntries[sEntryCount++] = sOffered[i];
            }
        }
        sOfferedCount = 0;

        const Entry* pFocus = nullptr;
        for (s32 i = 0; i < sEntryCount; i++) {
            if (getKey(sEntries[i]) == sFocus) {
                pFocus = &sEntries[i];
            }
        }
        if (pFocus == nullptr) {
            sFocus = nullptr;
        }

        const Vec2f* pPointer = MR::getStarPointerScreenPosition(PLAYER_1);
        if (sActive && MR::isStarPointerInScreen(PLAYER_1) &&
            (absolute(pPointer->x - sPointerAnchor.x) > cPointerHandBack ||
             absolute(pPointer->y - sPointerAnchor.y) > cPointerHandBack)) {
            sActive = false;
            sFocus = nullptr;
            pFocus = nullptr;
        }
        // Between two menus (a question closing) the pad keeps them; longer without one, play or a screen with
        // nothing to choose, and the pointer has them again.
        if (sEntryCount == 0) {
            sHadEntries = false;
            if (++sGapFrames > cGapFrames) {
                sActive = false;
                sFocus = nullptr;
            }
            return;
        }
        sGapFrames = 0;
        if (!sHadEntries) {
            sHadEntries = true;
            holdCurrent(&sRepeat);
            return;
        }

        const Direction press = readPress(&sRepeat);
        if (press == Direction_None) {
            return;
        }
        sPress = press;
        const Entry* pNext = pFocus != nullptr ? findNext(*pFocus, press) : findStart(press);
        if (pNext != nullptr && pNext == pFocus) {
            sUnmoved = press;
        }
        if (pNext != nullptr) {
            sFocus = getKey(*pNext);
            sActive = true;
            sPointerAnchor = *pPointer;
        }
    }

    void offer(ButtonPaneController* pButton) {
        const Entry entry = {pButton, nullptr, Vec2f(0.0f, 0.0f)};
        addOffered(entry);
    }

    void offerTarget(const void* pKey, const Vec2f& rScreenPos) {
        Entry entry = {nullptr, pKey, Vec2f(0.0f, 0.0f)};
        MR::convertScreenPosToLayoutPos(&entry.mCentre, rScreenPos);
        addOffered(entry);
    }

    bool isActive() {
        return sActive;
    }

    bool isFocused(const ButtonPaneController* pButton) {
        return sActive && pButton == sFocus;
    }

    bool isTargetFocused(const void* pKey) {
        return sActive && pKey == sFocus;
    }

    Direction getPress() {
        return sPress;
    }

    Direction getUnmovedPress() {
        return sUnmoved;
    }

    void setFocus(const ButtonPaneController* pButton) {
        sFocus = pButton;
        sActive = true;
        sPointerAnchor = *MR::getStarPointerScreenPosition(PLAYER_1);
    }
};  // namespace NebulaMenuFocus
