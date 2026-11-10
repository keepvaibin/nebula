#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <algorithm>
#include <cstring>
#include "Game/System/LumaCore.hpp"

namespace {
    struct HasActor {
        bool operator()(const MsgSharedGroup* pGroup, const LiveActor* pActor) const {
            return pGroup->hasActor(pActor);
        }
    };

    struct HasGroupId {
        bool operator()(const MsgSharedGroup* pGroup, const PlacementId& rIdInfo) const {
            return *pGroup->mIdInfo == rIdInfo;
        }
    };
};  // namespace

namespace {
    // The group keeps its own copy: callers pass names they format on the stack.
    const char* copyGroupName(const char* pName) {
        const size_t size = strlen(pName) + 1;
        char* const pCopy = new char[size];
        memcpy(pCopy, pName, size);
        return pCopy;
    }
}  // namespace

MsgSharedGroup::MsgSharedGroup(const char* pName, s32 numMax, const TableIter& rIter)
    : LiveActorGroup(copyGroupName(pName), numMax), mIdInfo(), mMsg(-1), mSensor(), mSensorName() {
    lumacore::setClass(this, lumacore::kClassId< MsgSharedGroup >);
    PlacementId* pIdInfo = new PlacementId();

    if (pIdInfo != nullptr) {
        PlacementId newInfo = MR::createPlacementIdFromGroupId(rIter);
        pIdInfo->mId = newInfo.mId;
        pIdInfo->mZoneID = newInfo.mZoneID;
    }

    mIdInfo = pIdInfo;
}

void MsgSharedGroup::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_MsgSharedGroup, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void MsgSharedGroup::movement() {
    NameObj::movement();

    if (mMsg == -1) {
        return;
    }

    for (s32 i = 0; i < getObjNum(); i++) {
        // A member without a sensor of that name gets no message.
        HitSensor* const pReceiver = getActor(i)->getSensor(mSensorName);
        if (pReceiver != nullptr) {
            pReceiver->receiveMessage(mMsg, mSensor);
        }
    }

    mMsg = -1;
}

void MsgSharedGroup::sendMsgToGroupMember(u32 msg, HitSensor* pSensor, const char* pSensorName) {
    mMsg = msg;
    mSensor = pSensor;
    mSensorName = pSensorName;
}

LiveActorGroupArray::LiveActorGroupArray(const char* pName) : NameObj(pName), mGroups() {
    lumacore::setClass(this, lumacore::kClassId< LiveActorGroupArray >);
}

void LiveActorGroupArray::init(const TableIter& /*rIter*/) {
}

LiveActorGroup* LiveActorGroupArray::getLiveActorGroup(const LiveActor* pActor) const {
    return findGroup(pActor);
}

LiveActorGroup* LiveActorGroupArray::createGroup(const TableIter& rIter, const char* pName, s32 numMax) {
    MsgSharedGroup* pGroup;

    pGroup = new MsgSharedGroup(pName, numMax, rIter);
    pGroup->initWithoutIter();

    mGroups.push_back(pGroup);

    return pGroup;
}

LiveActorGroup* LiveActorGroupArray::findGroup(const TableIter& rIter) const {
    const PlacementId idInfo = MR::createPlacementIdFromGroupId(rIter);
    MsgSharedGroup* const* it =
        std::find_if(mGroups.begin(), mGroups.end(), [&](const auto& item) { return HasGroupId()(item, idInfo); });

    if (it != mGroups.end()) {
        return *it;
    }

    return nullptr;
}

LiveActorGroup* LiveActorGroupArray::findGroup(const LiveActor* pActor) const {
    MsgSharedGroup* const* it = std::find_if(mGroups.begin(), mGroups.end(), [&](const auto& item) { return HasActor()(item, pActor); });

    if (it != mGroups.end()) {
        return *it;
    }

    return nullptr;
}

LiveActorGroup* LiveActorGroupArray::entry(LiveActor* pActor, const TableIter& rIter, const char* pName, s32 numMax) {
    s32 groupID = -1;
    MR::getPlacementGroupID(rIter, &groupID);

    // Function scope: pName points here past the if block .
    MR::FormattedName defaultName;
    if (pName == nullptr) {
        defaultName.format("group%02d", groupID);

        pName = defaultName.c_str();
    }

    LiveActorGroup* pGroup = findGroup(rIter);

    if (pGroup == nullptr) {
        pGroup = createGroup(rIter, pName, numMax);
    }

    pGroup->registerActor(pActor);

    return pGroup;
}
