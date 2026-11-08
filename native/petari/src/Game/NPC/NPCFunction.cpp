#include "Game/NPC/NPCFunction.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/NPC/NPCDirector.hpp"
#include "Game/NPC/NPCParameter.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/StringUtil.hpp"

void NPCFunction::createNPCData() {
}

void NPCFunction::deleteNPCData() {
}

bool NPCFunction::getNPCItemData(NPCActorItem* pItem, s32 idx) {
    NPCDirector* pDirector = MR::getSceneObj< NPCDirector >(SceneObj_NPCDirector);

    ResourceHolder* pHolder = pDirector->mDataResourceHolder;
    NPCItemParameterReader* pItemReader = pDirector->mItemParameterReader;

    MR::FormattedName resName("%sItem.bcsv", pItem->mActor);

    if (!pHolder->mFileInfoTable->isExistRes(resName.c_str())) {
        return false;
    }

    pItemReader->copy(pItem);

    Table mapInfo = Table();
    mapInfo.attach(pHolder->mFileInfoTable->getRes(resName.c_str()));

    pItemReader->read(&mapInfo, idx);
    *pItem = pItemReader->mItem;

    return true;
}
