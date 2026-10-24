#include "Game/NPC/NebulaAchievementLuma.hpp"

#ifdef TARGET_PC
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include <math.h>
#include <revolution/os.h>

namespace NrvNebulaAchievementLuma {
    NEW_NERVE(NebulaAchievementLumaNrvWait, NebulaAchievementLuma, NebulaWait);
    NEW_NERVE(NebulaAchievementLumaNrvOpen, NebulaAchievementLuma, NebulaOpen);
};  // namespace NrvNebulaAchievementLuma

namespace {
    // Where it waits, from the Observatory's first start (where Mario arrives with a file): ahead of him and to his
    // left, on the walkway, at a Luma's height above its floor.
    const s32 cStartNo = 0;
    const f32 cAhead = 600.0f;
    const f32 cLeft = 450.0f;
    const f32 cAboveFloor = 140.0f;
    const s32 cColor = 0;  // the Lumas' yellow (Tico's ColorChange frames)
    const char cMessageId[] = "Nebula_AchievementLuma";  // MessageData answers it from NebulaText
};  // namespace

NebulaAchievementLuma::NebulaAchievementLuma() : Tico("NebulaAchievementLuma") {
}

bool NebulaAchievementLuma::isHere() {
    return MR::isEqualStageName("AstroGalaxy") && GameDataFunction::isOnGameEventFlag("SpecialStarGrand1");
}

void NebulaAchievementLuma::tryCreate() {
    if (!isHere()) {
        return;
    }
    NebulaAchievementLuma* pLuma = new NebulaAchievementLuma();
    pLuma->initWithoutIter();
}

void NebulaAchievementLuma::init(const JMapInfoIter& rIter) {
    // Its place, before the actor is set up (with no placement entry it keeps the position it has).
    TVec3f start(0.0f, 0.0f, 0.0f);
    TVec3f startRotate(0.0f, 0.0f, 0.0f);
    MR::getMarioStartPose(cStartNo, &start, &startRotate);
    const f32 yaw = startRotate.y * (3.14159265f / 180.0f);
    const TVec3f ahead(static_cast< f32 >(sin(yaw)), 0.0f, static_cast< f32 >(cos(yaw)));
    const TVec3f left(ahead.z, 0.0f, -ahead.x);
    TVec3f place(start.x + ahead.x * cAhead + left.x * cLeft, start.y, start.z + ahead.z * cAhead + left.z * cLeft);
    // On the walkway's floor: the first ground below, looking down from above the start's height.
    TVec3f floor;
    const TVec3f from(place.x, start.y + 600.0f, place.z);
    if (MR::getFirstPolyOnLineToMap(&floor, nullptr, from, TVec3f(0.0f, -2000.0f, 0.0f))) {
        place.y = floor.y;
    }
    place.y += cAboveFloor;
    mPosition.set(place);
    // Facing where Mario arrives.
    mRotation.set(0.0f, static_cast< f32 >(atan2(start.x - place.x, start.z - place.z)) * (180.0f / 3.14159265f), 0.0f);
    OSReport("NebulaAchievementLuma: at %.0f %.0f %.0f (start %.0f %.0f %.0f)\n", place.x, place.y, place.z, start.x, start.y, start.z);

    initBase(cColor);
    initMessage(cMessageId);
    mWaitNerve = &NrvNebulaAchievementLuma::NebulaAchievementLumaNrvWait::sInstance;
    setNerve(&NrvNebulaAchievementLuma::NebulaAchievementLumaNrvWait::sInstance);
}

// As the Observatory's Lumas: it turns to Mario and talks when he asks.
void NebulaAchievementLuma::exeNebulaWait() {
    if (tryReaction()) {
        return;
    }
    if (MR::tryTalkNearPlayerAtEndAndStartTalkAction(this)) {
        setNerve(&NrvNebulaAchievementLuma::NebulaAchievementLumaNrvOpen::sInstance);
    }
}

// The talk's balloon closes first (the board would otherwise hold it half-closed until it returns); then the board,
// once. Back from it, the Luma waits to be asked again.
void NebulaAchievementLuma::exeNebulaOpen() {
    MR::tryStartTurnAction(this);
    if (MR::isTalkNone(mMsgCtrl)) {
        GameSceneFunction::requestShowNebulaBoard();
        setNerve(&NrvNebulaAchievementLuma::NebulaAchievementLumaNrvWait::sInstance);
    }
}
#endif
