#include "Game/NPC/NPCParameter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/NPCActor.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>

template MR::Vector< MR::FixedArray< NPCParameterBase*, 48 > >::~Vector();
template MR::Vector< MR::FixedArray< NPCParameterBase*, 48 > >::Vector();
template void MR::Vector< MR::FixedArray< NPCParameterBase*, 48 > >::push_back(NPCParameterBase* const&);

NPCParameterV3f::NPCParameterV3f(const char* pName, Vec3f* pVec) : NPCParameterBase(pName), mValue(pVec) {
}

void NPCParameterV3f::read(Table* pInfo, s32 entry) {
    MR::FormattedName key("%sX", mName);
    pInfo->getValue(entry, key.c_str(), &mValue->x);
    key.format("%sY", mName);
    pInfo->getValue(entry, key.c_str(), &mValue->y);
    key.format("%sZ", mName);
    pInfo->getValue(entry, key.c_str(), &mValue->z);
}

NPCParameterReader::NPCParameterReader(const char* pName) : NPCParameterBase(pName), MR::Vector< MR::FixedArray< NPCParameterBase*, 48 > >() {
}

void NPCParameterReader::read(Table* pInfo, s32 entry) {
    if (entry <= -1 || entry >= pInfo->getNumEntries()) {
        return;
    }

    for (NPCParameterBase** pIter = begin(); pIter != end(); pIter++) {
        (*pIter)->read(pInfo, entry);
    }
}

NPCCapsParameterReader::NPCCapsParameterReader(const char* pName) : NPCParameterReader(pName), mActorCaps("") {
    mActorCaps.setDefault();
    mActorCaps.mObjectName = pName;
    mActorCaps.mMessageId = pName;
    init();
}

void NPCItemParameterReader::copy(const NPCActorItem* pItem) {
    mItem.mActor = pItem->mActor;
    mItem.mGoods0 = pItem->mGoods0;
    mItem.mGoodsJoint0 = pItem->mGoodsJoint0;
    mItem.mGoods1 = pItem->mGoods1;
    mItem.mGoodsJoint1 = pItem->mGoodsJoint1;
}

void NPCCapsParameterReader::init() {
    push_back(new NPCParameterValue< bool >("mModel", &mActorCaps.mModel));
    push_back(new NPCParameterValue< bool >("mMakeActor", &mActorCaps.mMakeActor));
    push_back(new NPCParameterValue< bool >("mHostIO", &mActorCaps.mHostIO));
    push_back(new NPCParameterValue< bool >("mMessage", &mActorCaps.mMessage));
    push_back(new NPCParameterV3f("mMessageOffset", &mActorCaps.mMessageOffset));
    push_back(new NPCParameterValue< bool >("mInterpole", &mActorCaps.mInterpole));
    push_back(new NPCParameterValue< bool >("mConnectTo", &mActorCaps.mConnectTo));
    push_back(new NPCParameterValue< bool >("mLightCtrl", &mActorCaps.mLightCtrl));
    push_back(new NPCParameterValue< bool >("mEffect", &mActorCaps.mEffect));
    push_back(new NPCParameterValue< bool >("mSound", &mActorCaps.mSound));
    push_back(new NPCParameterValue< s32 >("mSoundSize", &mActorCaps.mSoundSize));
    push_back(new NPCParameterValue< bool >("mAttribute", &mActorCaps.mAttribute));
    push_back(new NPCParameterValue< bool >("mPosition", &mActorCaps.mPosition));
    push_back(new NPCParameterValue< bool >("mLodCtrl", &mActorCaps.mLodCtrl));
    push_back(new NPCParameterValue< bool >("mNerve", &mActorCaps.mNerve));
    push_back(new NPCParameterValue< bool >("mBinder", &mActorCaps.mBinder));
    push_back(new NPCParameterValue< f32 >("mBinderSize", &mActorCaps.mBinderSize));
    push_back(new NPCParameterValue< bool >("mSensor", &mActorCaps.mSensor));
    push_back(new NPCParameterValue< const char* >("mSensorJoint", &mActorCaps.mSensorJoint));
    push_back(new NPCParameterValue< f32 >("mSensorSize", &mActorCaps.mSensorSize));
    push_back(new NPCParameterValue< s32 >("mSensorMax", &mActorCaps.mSensorMax));
    push_back(new NPCParameterValue< bool >("mShadow", &mActorCaps.mShadow));
    push_back(new NPCParameterValue< f32 >("mShadowSize", &mActorCaps.mShadowSize));
    push_back(new NPCParameterValue< bool >("mRailRider", &mActorCaps.mRailRider));
    push_back(new NPCParameterValue< bool >("mSwitchDead", &mActorCaps.mSwitchDead));
    push_back(new NPCParameterValue< bool >("mSwitchAppear", &mActorCaps.mSwitchAppear));
    push_back(new NPCParameterValue< bool >("mPointer", &mActorCaps.mPointer));
    push_back(new NPCParameterValue< f32 >("mPointerSize", &mActorCaps.mPointerSize));
}

NPCItemParameterReader::NPCItemParameterReader(const char* pName) : NPCParameterReader(pName), mItem("") {
    push_back(new NPCParameterValue< const char* >("mGoods0", &mItem.mGoods0));
    push_back(new NPCParameterValue< const char* >("mGoods1", &mItem.mGoods1));
    push_back(new NPCParameterValue< const char* >("mGoodsJoint0", &mItem.mGoodsJoint0));
    push_back(new NPCParameterValue< const char* >("mGoodsJoint1", &mItem.mGoodsJoint1));
}

void NPCParameterBase::read(Table*, s32) {
}
