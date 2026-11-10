#include "Game/MapObj/AstroOverlookObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/LumaCore.hpp"

AstroOverlookObj::AstroOverlookObj(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< AstroOverlookObj >);
    mIsLayoutHidden = 0;
    MR::connectToSceneMapObjMovement(this);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void AstroOverlookObj::control() {
    if (MR::getCurrentAstroOverlookAreaObj()) {
        MR::deactivateDefaultGameLayout();
        mIsLayoutHidden = 1;
    } else {
        if (mIsLayoutHidden == 1) {
            MR::activateDefaultGameLayout();
            mIsLayoutHidden = 0;
        }
    }
}
