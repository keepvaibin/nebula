#include "Game/Boss/DodoryuHill.hpp"
#include "Game/Boss/Dodoryu.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    inline void setHillTable(MogucchiHill* pHill, const char* const* pModelNames, u32 count) {
        pHill->mModelNum = count;
        pHill->mModelNameTable = pModelNames;
    }
    inline void setHillNames(MogucchiHill* pHill, const char* pJointName, const char* pEffectName) {
        pHill->mJointName = pJointName;
        pHill->mAutoEffectName = pEffectName;
    }

    const char* const sDodoryuHillModel[] = {
        "DodoryuHillA",
        "DodoryuHillB",
        "DodoryuHillC",
    };
};  // namespace

DodoryuHill::DodoryuHill(Dodoryu* pHost) : MogucchiHill(pHost, 50, "ドドリュウ塚"), mHost(pHost) {
    lumacore::setClass(this, lumacore::kClassId< DodoryuHill >);
    setHillTable(this, ::sDodoryuHillModel, ARRAY_SIZE(::sDodoryuHillModel));
    const char* pJoint = "MogucchiHill";
    const char* pEffect = "DodoryuHill";
    setHillNames(this, pJoint, pEffect);
    mHasLightCtrl = true;

    initWithoutIter();
}

bool DodoryuHill::notifyAppearAttackToPlayer(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}
