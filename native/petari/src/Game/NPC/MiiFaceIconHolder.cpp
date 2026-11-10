#include "Game/NPC/MiiFaceIconHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/MiiFaceIcon.hpp"
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

MiiFaceIconHolder::MiiFaceIconHolder(u32 iconNum, const char* pName)
    : NameObj(pName), mIconNumMax(iconNum), mIconNum(0), mIcon(new MiiFaceIcon*[iconNum]) {
    lumacore::setClass(this, lumacore::kClassId< MiiFaceIconHolder >);
    MR::connectToScene(MR::createDrawAdaptor("Miiアイコン生成", MR::Functor(this, &MiiFaceIconHolder::drawIcons)), MR::MovementType_None,
                       MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_MiiFaceIcon);
}

// Every icon asked for since the last draw, in the frame it was asked for.
void MiiFaceIconHolder::drawIcons() {
    for (u32 i = 0; i < mIconNum; i++) {
        if (mIcon[i]->mIsRequestMakeIcon) {
            mIcon[i]->drawIcon();
        }
    }
}

void MiiFaceIconHolder::registerIcon(MiiFaceIcon* pIcon) {
    if (mIconNum == mIconNumMax) {
        mIcon = MR::growArray(mIcon, static_cast< s32 >(mIconNumMax));
        mIconNumMax *= 2;
    }

    mIcon[mIconNum++] = pIcon;
}

namespace MR {
    MiiFaceIconHolder* getMiiFaceIconHolder() {
        return MR::getSceneObj< MiiFaceIconHolder >(SceneObj_MiiFaceIconHolder);
    }

    void registerMiiFaceIcon(MiiFaceIcon* pIcon) {
        MR::createSceneObj(SceneObj_MiiFaceIconHolder);
        getMiiFaceIconHolder()->registerIcon(pIcon);
    }
};  // namespace MR
