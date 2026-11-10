#include "Game/AreaObj/BgmProhibitArea.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 10000.0f);

struct BgmMuteSet {
    const char* mGalaxyName;
    u32 mMuteState;
    u32 mUnmuteState;
};

namespace {
    const BgmMuteSet sBgmMuteSet[] = {{"OceanPhantomCaveGalaxy", 7, 1}, {"SoundMapCodeTest", 7, 1}};

    const BgmMuteSet* findMuteSet(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(::sBgmMuteSet); i++) {
            if (MR::isEqualString(pName, ::sBgmMuteSet[i].mGalaxyName)) {
                return &::sBgmMuteSet[i];
            }
        }

        return nullptr;
    }
};  // namespace

BgmProhibitArea::BgmProhibitArea(int formType, const char* pName)
    : AreaObj(formType, pName), mMuteSet(), mIsMuteSet(), mIsUnmuteSet(), mPrevPlayerPos(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< BgmProhibitArea >);
}

void BgmProhibitArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);
    mMuteSet = ::findMuteSet(MR::getCurrentStageName());
}

void BgmProhibitArea::movement() {
    if (MR::isStageStateScenarioOpeningCamera()) {
        return;
    }

    Vec3f playerPos = *MR::getPlayerPos();
    f32 distToPlayer = playerPos.distance(mPrevPlayerPos);

    if (isInVolume(*MR::getPlayerPos())) {
        mIsUnmuteSet = false;

        if (!mIsMuteSet && MR::isPlayingStageBgm()) {
            if (mMuteSet != nullptr) {
                // A jump of 100 m in one step (a warp) mutes at once.
                MR::setStageBGMState(mMuteSet->mMuteState, distToPlayer >= cNum_movement_f1 ? 0 : 120);
            }

            mIsMuteSet = true;
        }
    } else {
        mIsMuteSet = false;

        if (!mIsUnmuteSet && MR::isPlayingStageBgm()) {
            if (mMuteSet != nullptr) {
                MR::setStageBGMState(mMuteSet->mUnmuteState, 120);
            }

            mIsUnmuteSet = true;
        }
    }

    mPrevPlayerPos = playerPos;
}
