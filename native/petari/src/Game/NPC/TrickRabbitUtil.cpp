#include "Game/NPC/TrickRabbitUtil.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createRabbitFootPrint_f1, "createRabbitFootPrint.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createRabbitFootPrint_f2, "createRabbitFootPrint.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createRabbitFootPrint_f3, "createRabbitFootPrint.f3", 30.0f);

namespace TrickRabbitUtil {
    FootPrint* createRabbitFootPrint(LiveActor* pActor) {
        FootPrint* footPrint;

        footPrint = new FootPrint("ウサギ足跡", 64);
        footPrint->mMinPrintDistance = cNum_createRabbitFootPrint_f1;
        footPrint->setTexture(MR::getTexFromArc("RabbitFootprint.bti", pActor));
        footPrint->mSideOffset = 0.0f;
        footPrint->mHeight = cNum_createRabbitFootPrint_f2;
        footPrint->mWidth = cNum_createRabbitFootPrint_f3;

        return footPrint;
    }
};  // namespace TrickRabbitUtil
