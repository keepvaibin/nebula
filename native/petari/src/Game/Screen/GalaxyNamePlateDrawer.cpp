#include "Game/Util/SortUtil.hpp"
#include "Game/Screen/GalaxyNamePlateDrawer.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <algorithm>
#include "Game/System/LumaCore.hpp"

namespace {
    struct PlatePriorityLt {
        bool operator()(const GalaxyNamePlatePrioritied* pEntry1, const GalaxyNamePlatePrioritied* pEntry2) {
            return pEntry1->mZ < pEntry2->mZ;
        }
    };
};  // namespace

GalaxyNamePlateDrawer::GalaxyNamePlateDrawer() : NameObj("ギャラクシー名プレート描画") {
    lumacore::setClass(this, lumacore::kClassId< GalaxyNamePlateDrawer >);
}

void GalaxyNamePlateDrawer::init(const TableIter&) {
    MR::connectToScene(this, -1, -1, -1, MR::DrawType_GalaxyNamePlate);
}

void GalaxyNamePlateDrawer::draw() const {
    mDrawOrder.clear();
    for (s32 i = 0; i < mPlateArray.size(); i++) {
        mDrawOrder.push_back(mPlateArray[i]);
    }
    if (mDrawOrder.size() == 0) {
        return;
    }
    MR::sort(&mDrawOrder[0], &mDrawOrder[0] + mDrawOrder.size(), ::PlatePriorityLt());

    for (s32 i = 0; i < mDrawOrder.size(); i++) {
        mDrawOrder[i]->mHostActor->draw();
    }
}

GalaxyNamePlatePrioritied* GalaxyNamePlateDrawer::registerPlate(const LayoutActor* pPlate) {
    GalaxyNamePlatePrioritied* newEntry = new GalaxyNamePlatePrioritied(pPlate);
    mPlateArray.push_back(newEntry);
    return newEntry;
}

namespace MR {
    GalaxyNamePlatePrioritied* registerToGalaxyNamePlateDrawer(const LayoutActor* pPlate) {
        createSceneObj(SceneObj_GalaxyNamePlateDrawer);
        return getSceneObj< GalaxyNamePlateDrawer >(SceneObj_GalaxyNamePlateDrawer)->registerPlate(pPlate);
    }
};  // namespace MR
