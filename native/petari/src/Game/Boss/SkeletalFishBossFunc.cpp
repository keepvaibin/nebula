#include "Game/Boss/SkeletalFishBossFunc.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getNearestGuardPosID_f1, "getNearestGuardPosID.f1", 100000.0f);

namespace {
    static const char* sNamePosName = "ガード出現ポイント";
};  // namespace

s32 SkeletalFishBossFunc::getNearestGuardPosID(const Vec3f& rVec) {
    f32 closest = cNum_getNearestGuardPosID_f1;
    s32 posID = -1;

    for (int i = 0; i < 16; i++) {
        Vec3f pos;

        if (!copyGuardPos(&pos, i)) {
            continue;
        }

        f32 dist = rVec.distance(pos);

        if (dist < closest) {
            closest = dist;
            posID = i;
        }
    }

    return posID;
}

bool SkeletalFishBossFunc::copyGuardPos(Vec3f* pOut, s32 idx) {
    MR::FormattedName name("%s%d", ::sNamePosName, idx);

    return MR::tryFindNamePos(name.c_str(), pOut, nullptr);
}
