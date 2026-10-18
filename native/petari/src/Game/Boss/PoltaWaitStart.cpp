#include "Game/Boss/PoltaWaitStart.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaActionBase.hpp"
#include "Game/Boss/PoltaFunction.hpp"
#include "Game/Util/LiveActorUtil.hpp"

PoltaWaitStart::PoltaWaitStart(Polta* pPolta) : PoltaActionBase("\203\174\203\213\203\136\212\112\216\156\221\322\202\277" /* ポルタ開始待ち */, pPolta) {
}

void PoltaWaitStart::appear() {
    mIsDead = false;
    PoltaFunction::killLeftArm(getHost());
    PoltaFunction::killRightArm(getHost());
    MR::hideModel(getHost());
}

PoltaWaitStart::~PoltaWaitStart() {
}
