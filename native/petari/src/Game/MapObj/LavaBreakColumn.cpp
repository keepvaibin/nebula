#include "Game/MapObj/LavaBreakColumn.hpp"
#include "Game/MapObj/AnmModelObj.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 1000.0f);

namespace {
    NEBULA_TUNABLE(f32, sEffectPosOffsetY, 5000.0f);
};  // namespace

LavaBreakColumn::LavaBreakColumn(const char* pName) : AnmModelSwitchMoveEndKill(pName), mEffectPos(gZeroVec), mIsPillarFallen() {
    lumacore::setClass(this, lumacore::kClassId< LavaBreakColumn >);
}

void LavaBreakColumn::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    mIsPillarFallen = false;
}

void LavaBreakColumn::init(const TableIter& rIter) {
    AnmModelSwitchMove::init(rIter);
    MR::onCalcGravity(this);
    MR::setEffectHostSRT(this, "LavaColumn", &mEffectPos, nullptr, nullptr);
}

void LavaBreakColumn::control() {
    if (mIsPillarFallen) {
        return;
    }

    Vec3f pos;
    MR::copyJointPos(this, "Move", &pos);
    MR::calcMapGround(pos, &mEffectPos, ::sEffectPosOffsetY);
    Pos3f mtx;
    mtx.set(getBaseMtx());
    Vec3f up;
    mtx.getYDir(up);
    MR::normalize(&up);
    Vec3f offset = pos + up * -cNum_control_f1;
    Vec3f diff = mEffectPos - offset;
    MR::normalize(&diff);

    if (diff.dot(mGravity) < 0.0f) {
        MR::emitEffect(this, "LavaColumn");
        MR::startSound(this, "SE_OJ_LAVA_COLUMN_FALL_ED");
        mIsPillarFallen = true;
    }
}

bool AnmModelSwitchMoveEndKill::isKilledAtMoveDone() const {
    return true;
}
