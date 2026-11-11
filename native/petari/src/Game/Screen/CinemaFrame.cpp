#include "Game/Screen/CinemaFrame.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvCinemaFrame {
    NEW_NERVE(CinemaFrameNrvScreen, CinemaFrame, Screen);
    NEW_NERVE(CinemaFrameNrvFrame, CinemaFrame, Frame);
    NEW_NERVE(CinemaFrameNrvBlank, CinemaFrame, Blank);
    NEW_NERVE(CinemaFrameNrvScreenToFrame, CinemaFrame, ScreenToFrame);
    NEW_NERVE(CinemaFrameNrvFrameToBlank, CinemaFrame, FrameToBlank);
    NEW_NERVE(CinemaFrameNrvBlankToFrame, CinemaFrame, BlankToFrame);
    NEW_NERVE(CinemaFrameNrvFrameToScreen, CinemaFrame, FrameToScreen);
};  // namespace NrvCinemaFrame

CinemaFrame::CinemaFrame(bool isConnectToScene) : LayoutActor("シネマフレーム", true), mStartAt(0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CinemaFrame >);
    if (isConnectToScene) {
        MR::connectToScene(this, MR::MovementType_Layout, MR::CalcAnimType_Layout, MR::DrawBufferType_None, MR::DrawType_CinemaFrame);
    }
}

void CinemaFrame::init(const TableIter& /*rIter*/) {
    initLayoutManager("CinemaFrame", 1);
    initNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreen));
    kill();
}

void CinemaFrame::appear() {
    LayoutActor::appear();
    mStartAt = 0.0f;
    setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreenToFrame));
}

// How far the running transition's animation has gone, 0 to 1; 0 before its first step has started it.
f32 CinemaFrame::transitionDone() {
    if (getNerveStep() < 1) {
        return 0.0f;
    }
    const f32 end = MR::getAnimCtrl(this, 0)->getEnd();
    if (end <= 0.0f) {
        return 1.0f;
    }
    const f32 done = MR::getAnimFrame(this, 0) / end;
    return done < 0.0f ? 0.0f : done > 1.0f ? 1.0f : done;
}

void CinemaFrame::startTransition(const char* pAnimName) {
    MR::startAnim(this, pAnimName, 0);
    if (mStartAt > 0.0f) {
        MR::setAnimFrame(this, mStartAt * MR::getAnimCtrl(this, 0)->getEnd(), 0);
    }
    mStartAt = 0.0f;
}

void CinemaFrame::tryScreenToFrame() {
    if (MR::isDead(this)) {
        appear();
    }

    bool isInvalid = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrame)) || isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreenToFrame)) ||
                     isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlankToFrame));

    if (!isInvalid) {
        // Bars on their way out come back from where they are: one not yet moving is still a full frame.
        mStartAt = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrameToScreen)) ? 1.0f - transitionDone() : 0.0f;
        if (mStartAt >= 1.0f) {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrame));
        } else {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreenToFrame));
        }
    }
}

void CinemaFrame::tryFrameToBlank() {
    if (MR::isDead(this)) {
        appear();
    }

    bool isInvalid = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlank)) || isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrameToBlank));

    if (!isInvalid) {
        mStartAt = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlankToFrame)) ? 1.0f - transitionDone() : 0.0f;
        if (mStartAt >= 1.0f) {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlank));
        } else {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrameToBlank));
        }
    }
}

void CinemaFrame::tryBlankToFrame() {
    if (MR::isDead(this)) {
        appear();
    }

    bool isInvalid = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrame)) || isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreenToFrame)) ||
                     isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlankToFrame));

    if (!isInvalid) {
        mStartAt = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrameToBlank)) ? 1.0f - transitionDone() : 0.0f;
        if (mStartAt >= 1.0f) {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrame));
        } else {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlankToFrame));
        }
    }
}

void CinemaFrame::tryFrameToScreen() {
    if (MR::isDead(this)) {
        appear();
    }

    bool isInvalid = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreen)) || isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrameToScreen));

    if (!isInvalid) {
        mStartAt = isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreenToFrame)) ? 1.0f - transitionDone() : 0.0f;
        if (mStartAt >= 1.0f) {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreen));
        } else {
            setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrameToScreen));
        }
    }
}

void CinemaFrame::forceToScreen() {
    if (MR::isDead(this)) {
        appear();
    }
    mStartAt = 0.0f;

    setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreen));
}

void CinemaFrame::forceToFrame() {
    if (MR::isDead(this)) {
        appear();
    }
    mStartAt = 0.0f;

    setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrame));
}

void CinemaFrame::forceToBlank() {
    if (MR::isDead(this)) {
        appear();
    }
    mStartAt = 0.0f;

    setNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlank));
}

bool CinemaFrame::isStop() const {
    return isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvScreen)) || isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvFrame)) ||
           isNerve(GET_NERVE(CinemaFrame, CinemaFrameNrvBlank));
}

void CinemaFrame::exeScreen() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "End", 0);
        MR::setAnimFrameAndStop(this, MR::getAnimCtrl(this, 0)->getEnd() - 1.0f, 0);
        kill();
    }
}

void CinemaFrame::exeFrame() {
    MR::startAnimAtFirstStep(this, "Wait", 0);
}

void CinemaFrame::exeBlank() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Open", 0);
        MR::setAnimFrameAndStop(this, 0.0f, 0);
    }
}

void CinemaFrame::exeScreenToFrame() {
    if (MR::isFirstStep(this)) {
        startTransition("Appear");
    }
    MR::setNerveAtAnimStopped(this, GET_NERVE(CinemaFrame, CinemaFrameNrvFrame), 0);
}

void CinemaFrame::exeFrameToBlank() {
    if (MR::isFirstStep(this)) {
        startTransition("Close");
    }
    MR::setNerveAtAnimStopped(this, GET_NERVE(CinemaFrame, CinemaFrameNrvBlank), 0);
}

void CinemaFrame::exeBlankToFrame() {
    if (MR::isFirstStep(this)) {
        startTransition("Open");
    }
    MR::setNerveAtAnimStopped(this, GET_NERVE(CinemaFrame, CinemaFrameNrvFrame), 0);
}

void CinemaFrame::exeFrameToScreen() {
    if (MR::isFirstStep(this)) {
        startTransition("End");
    }
    MR::setNerveAtAnimStopped(this, GET_NERVE(CinemaFrame, CinemaFrameNrvScreen), 0);
}
