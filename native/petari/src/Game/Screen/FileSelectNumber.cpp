#include "Game/Screen/FileSelectNumber.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEW_NERVE(FileSelectNumberNrvAppear, FileSelectNumber, Appear);
    NEW_NERVE(FileSelectNumberNrvWait, FileSelectNumber, Wait);
    NEW_NERVE(FileSelectNumberNrvEnd, FileSelectNumber, End);
};  // namespace

FileSelectNumber::FileSelectNumber(const char* pName) : LayoutActor(pName, true), mNumber(-1), mSelectAnimCtrl() {
    lumacore::setClass(this, lumacore::kClassId< FileSelectNumber >);
}

void FileSelectNumber::init(const TableIter& /*rIter*/) {
    initLayoutManager("FileNumber", 2);
    MR::connectToSceneLayout(this);
    initNerve(GET_NERVE_ANON(FileSelectNumberNrvWait));

    mSelectAnimCtrl = new FileSelectNumberSub::SelectAnimController(this);
}

void FileSelectNumber::appear() {
    f32 animFrame;

    if (MR::isDead(this)) {
        LayoutActor::appear();
        MR::startAnim(this, "Appear", 0);
        setNerve(GET_NERVE_ANON(FileSelectNumberNrvAppear));
        mSelectAnimCtrl->appear();
    } else if (isNerve(GET_NERVE_ANON(FileSelectNumberNrvEnd))) {
        animFrame = MR::getAnimFrame(this, 0);

        MR::startAnim(this, "Appear", 0);
        MR::setAnimFrame(this, MR::getAnimCtrl(this, 0)->getEnd() - animFrame, 0);
        setNerve(GET_NERVE_ANON(FileSelectNumberNrvAppear));
    }
}

void FileSelectNumber::disappear() {
    f32 animFrame;

    if (isNerve(GET_NERVE_ANON(FileSelectNumberNrvAppear))) {
        animFrame = MR::getAnimFrame(this, 0);

        MR::startAnim(this, "End", 0);
        MR::setAnimFrame(this, MR::getAnimCtrl(this, 0)->getEnd() - animFrame, 0);
        setNerve(GET_NERVE_ANON(FileSelectNumberNrvEnd));
    } else if (isNerve(GET_NERVE_ANON(FileSelectNumberNrvWait))) {
        MR::startAnim(this, "End", 0);
        setNerve(GET_NERVE_ANON(FileSelectNumberNrvEnd));
    }
}

void FileSelectNumber::setNumber(s32 number) {
    mNumber = number;

    MR::setTextBoxNumberRecursive(this, "FileNumber", number);
}

void FileSelectNumber::onSelectIn() {
    mSelectAnimCtrl->selectIn();
}

void FileSelectNumber::onSelectOut() {
    mSelectAnimCtrl->selectOut();
}

void FileSelectNumber::control() {
    mSelectAnimCtrl->updateNerve();
}

void FileSelectNumber::exeAppear() {

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE_ANON(FileSelectNumberNrvWait));
    }
}

void FileSelectNumber::exeWait() {
}

void FileSelectNumber::exeEnd() {

    if (MR::isAnimStopped(this, 0)) {
        kill();
    }
}

namespace FileSelectNumberSub {
    NEW_NERVE(SelectAnimControllerNrvSelectInStart, SelectAnimController, SelectInStart);
    NEW_NERVE(SelectAnimControllerNrvSelectIn, SelectAnimController, SelectIn);
    NEW_NERVE(SelectAnimControllerNrvSelectOutStart, SelectAnimController, SelectOutStart);
    NEW_NERVE(SelectAnimControllerNrvSelectOut, SelectAnimController, SelectOut);

    SelectAnimController::SelectAnimController(LayoutActor* pHost) : NerveExecutor("セレクトアニメ制御"), mHost(pHost) {
        initNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOut));
    }

    void SelectAnimController::appear() {
        setNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOut));
        MR::startAnim(mHost, "SelectOut", 1);
        MR::setAnimFrame(mHost, MR::getAnimCtrl(mHost, 1)->getEnd() - 1.0f, 1);
    }

    void SelectAnimController::selectIn() {
        if (isNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOut))) {
            setNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectInStart));

            mQueuedNerve = nullptr;
        } else if (isNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOutStart))) {
            mQueuedNerve = GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectIn);
        }
    }

    void SelectAnimController::selectOut() {
        if (isNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectIn))) {
            setNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOutStart));

            mQueuedNerve = nullptr;
        } else if (isNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectInStart))) {
            mQueuedNerve = GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOut);
        }
    }

    void SelectAnimController::exeSelectInStart() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "SelectIn", 1);
        }

        if (MR::isAnimStopped(mHost, 0)) {
            if (mQueuedNerve == GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOut)) {
                setNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOutStart));
            } else {
                setNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectIn));
            }

            mQueuedNerve = nullptr;
        }
    }

    void SelectAnimController::exeSelectIn() {
    }

    void SelectAnimController::exeSelectOutStart() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "SelectOut", 1);
        }

        if (MR::isAnimStopped(mHost, 0)) {
            if (mQueuedNerve == GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectIn)) {
                setNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectInStart));
            } else {
                setNerve(GET_NERVE_GLOBAL(SelectAnimControllerNrvSelectOut));
            }

            mQueuedNerve = nullptr;
        }
    }

    void SelectAnimController::exeSelectOut() {
    }
};  // namespace FileSelectNumberSub
