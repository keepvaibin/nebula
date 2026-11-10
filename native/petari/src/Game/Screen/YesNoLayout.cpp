#include "Game/Screen/YesNoLayout.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/YesNoController.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvYesNoLayout {
    NEW_NERVE(YesNoLayoutNrvSelecting, YesNoLayout, Selecting);
};  // namespace NrvYesNoLayout

YesNoLayout::YesNoLayout() : SimpleLayout("Yes/Noレイアウト", "SelectButton", 1, MR::DrawType_TalkPartsLayout), mController(nullptr), mHasInformationMessage(false) {
    lumacore::setClass(this, lumacore::kClassId< YesNoLayout >);
}

void YesNoLayout::init(const TableIter& /*rIter*/) {
    MR::createAndAddPaneCtrl(this, "Left", 1);
    MR::createAndAddPaneCtrl(this, "Right", 1);
    initEffectKeeper(0, nullptr, nullptr);

    mController = new YesNoController(this);

    MR::registerDemoSimpleCastAll(this);
    initNerve(GET_NERVE(YesNoLayout, YesNoLayoutNrvSelecting));
}

void YesNoLayout::appear() {
    LayoutActor::appear();
    mController->appear();

    mHasInformationMessage = false;

    MR::startStarPointerModeChooseYesNo(this);
    setNerve(GET_NERVE(YesNoLayout, YesNoLayoutNrvSelecting));
}

void YesNoLayout::kill() {
    MR::endStarPointerMode(this);
    LayoutActor::kill();
}

void YesNoLayout::setInformationMessage(const char* pMessage) {
    mHasInformationMessage = true;

    MR::appearInformationMessage(pMessage, false);
}

void YesNoLayout::setSelectMessage(const char* pRightSelectMessage, const char* pLeftSelectMessage) {
    MR::setTextBoxGameMessageRecursive(this, "Right", pRightSelectMessage);
    MR::setTextBoxGameMessageRecursive(this, "Left", pLeftSelectMessage);
}

void YesNoLayout::control() {
    mController->update();
}

void YesNoLayout::exeSelecting() {
    if (mHasInformationMessage && mController->isDisappearStart()) {
        MR::disappearInformationMessage();
    }

    if (!mController->mIsActive) {
        kill();
    }
}
