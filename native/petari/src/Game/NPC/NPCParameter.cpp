#include "Game/NPC/NPCParameter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/NPCActor.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include <nebula_geometry.hpp>

NPCParameterReader::NPCParameterReader(const char* pName) : NPCParameterBase(pName), MR::InlineVector< NPCParameterBase*, 4 >() {
}

void NPCParameterReader::read(Table* pInfo, s32 entry) {
    if (entry <= -1 || entry >= pInfo->getNumEntries()) {
        return;
    }

    for (NPCParameterBase** pIter = begin(); pIter != end(); pIter++) {
        (*pIter)->read(pInfo, entry);
    }
}

void NPCItemParameterReader::copy(const NPCActorItem* pItem) {
    mItem.mActor = pItem->mActor;
    mItem.mGoods0 = pItem->mGoods0;
    mItem.mGoodsJoint0 = pItem->mGoodsJoint0;
    mItem.mGoods1 = pItem->mGoods1;
    mItem.mGoodsJoint1 = pItem->mGoodsJoint1;
}

NPCItemParameterReader::NPCItemParameterReader(const char* pName) : NPCParameterReader(pName), mItem("") {
    push_back(new NPCParameterValue< const char* >("mGoods0", &mItem.mGoods0));
    push_back(new NPCParameterValue< const char* >("mGoods1", &mItem.mGoods1));
    push_back(new NPCParameterValue< const char* >("mGoodsJoint0", &mItem.mGoodsJoint0));
    push_back(new NPCParameterValue< const char* >("mGoodsJoint1", &mItem.mGoodsJoint1));
}

void NPCParameterBase::read(Table*, s32) {
}
