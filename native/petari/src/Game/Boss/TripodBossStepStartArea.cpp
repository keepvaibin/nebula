#include "Game/Boss/TripodBossStepStartArea.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

TripodBossStepStartArea::TripodBossStepStartArea(int formType, const char* pName) : AreaObj(formType, pName), mSequenceNo(-1) {
}

void TripodBossStepStartArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);
    mSequenceNo = mObjArg0;
}

void TripodBossStepStartArea::movement() {
    if (isInVolume(*MR::getPlayerPos())) {
        MR::requestStartTripodBossStepSequence(mSequenceNo);
    }
}

const char* TripodBossStepStartArea::getManagerName() const {
    return "TripodBossStepStart";
}
