#include "Game/NPC/RosettaDemoEpilogue.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/Rosetta.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace NrvRosettaDemoEpilogue {
    NEW_NERVE(RosettaDemoEpilogueNrvDemo, RosettaDemoEpilogue, Demo);
};  // namespace NrvRosettaDemoEpilogue

RosettaDemoEpilogue::RosettaDemoEpilogue(Rosetta* pRosetta, const JMapInfoIter& rIter)
    : NerveExecutor("\203\215\203\133\203\142\203\136\203\146\203\202\216\300\215\163\216\322" /* ロゼッタデモ実行者 */), mRosetta(pRosetta), mIsFadeOut() {
    DemoFunction::tryCreateDemoTalkAnimCtrlForScene(pRosetta, rIter, "DemoEpilogueB", nullptr, 0, 0);
    DemoFunction::registerDemoTalkMessageCtrl(mRosetta, mRosetta->mMsgCtrl);
    MR::registerDemoActionFunctor(mRosetta, MR::Functor(this, &RosettaDemoEpilogue::startDemo), "\203\107\203\163\203\215\201\133\203\117[\212\112\216\156]" /* エピローグ[開始] */);
    mRosetta->mLodCtrl->invalidate();
    initNerve(GET_NERVE(RosettaDemoEpilogue, RosettaDemoEpilogueNrvDemo));
}

void RosettaDemoEpilogue::startDemo() {
    mIsFadeOut = false;

    mRosetta->startDemo(this);
}

void RosettaDemoEpilogue::exeDemo() {
    if (MR::isDemoPartActive("\203\107\203\163\203\215\201\133\203\117[\203\164\203\106\201\133\203\150\203\101\203\105\203\147]" /* エピローグ[フェードアウト] */)) {
        mIsFadeOut = true;
    }

    if (!mIsFadeOut) {
        MR::startAtmosphereLevelSE("SE_DM_LV_EPILOGUE_BABY_CRY");
    }

    if (MR::isDemoLastStep()) {
        mRosetta->endDemo();
    }
}
