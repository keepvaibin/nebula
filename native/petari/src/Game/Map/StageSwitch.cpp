#include "Game/Map/StageSwitch.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/System/LumaCore.hpp"

ZoneSwitch::ZoneSwitch() : mFlags(nullptr), mWords(0) {
}

bool ZoneSwitch::get(int index) const {
    const u32 word = static_cast< u32 >(index) / 32;
    return word < mWords && (mFlags[word] & (1u << (index % 32))) != 0;
}

void ZoneSwitch::set(int index, bool isOn) {
    // A switch number past a million bits (a placement with a wild number) is not stored.
    if (index < 0 || index >= (1 << 20)) {
        return;
    }

    const u32 word = static_cast< u32 >(index) / 32;
    if (word >= mWords) {
        if (!isOn) {
            return;
        }

        // In this switch's own arena: whichever is current now may go first.
        const u32 words = word + 4;
        u32* const pFlags = new (NebulaArena::findFromRoot(this), 0) u32[words];
        for (u32 i = 0; i < words; i++) {
            pFlags[i] = i < mWords ? mFlags[i] : 0;
        }
        delete[] mFlags;
        mFlags = pFlags;
        mWords = words;
    }

    if (isOn) {
        mFlags[word] |= 1u << (index % 32);
    } else {
        mFlags[word] &= ~(1u << (index % 32));
    }
}

SwitchIdInfo::SwitchIdInfo(s32 switchID, const TableIter& rIter) : mIDInfo(), mIsGlobal(switchID >= 1000) {
    mIDInfo = new PlacementId(switchID, rIter);
}

s32 SwitchIdInfo::getSwitchNo() const {
    if (mIsGlobal) {
        return mIDInfo->mId - 1000;
    }

    return mIDInfo->mId;
}

void StageSwitchContainer::createAndAddZone(const SwitchIdInfo& rInfo) {
    if (findZoneSwitchFromTable(rInfo) != nullptr) {
        return;
    }

    ZoneSwitch* pZoneSwitch = new ZoneSwitch();
    ContainerSwitch sw;
    sw.mZoneId = rInfo.mIDInfo->mZoneID;
    sw.mSwitch = pZoneSwitch;
    mSwitches.push_back(sw);
}

ZoneSwitch* StageSwitchContainer::getZoneSwitch(const SwitchIdInfo& rInfo) {
    if (rInfo.mIsGlobal) {
        return mGlobalSwitches;
    }

    return findZoneSwitchFromTable(rInfo);
}

ZoneSwitch* StageSwitchContainer::findZoneSwitchFromTable(const SwitchIdInfo& rInfo) {
    for (int i = 0; i < mSwitches.size(); i++) {
        ContainerSwitch* pContainer = &mSwitches[i];

        if (pContainer->mZoneId == rInfo.mIDInfo->mZoneID) {
            return pContainer->mSwitch;
        }
    }

    return nullptr;
}

namespace {
    StageSwitchContainer* getStageSwitchContainer() {
        return MR::getSceneObj< StageSwitchContainer >(SceneObj_StageSwitchContainer);
    }
};  // namespace

void StageSwitchCtrl::onSwitchA() {
    StageSwitchFunction::onSwitchBySwitchIdInfo(*mSwitchA);
}

void StageSwitchCtrl::offSwitchA() {
    StageSwitchFunction::offSwitchBySwitchIdInfo(*mSwitchA);
}

bool StageSwitchCtrl::isOnSwitchA() const {
    return StageSwitchFunction::isOnSwitchBySwitchIdInfo(*mSwitchA);
}

bool StageSwitchCtrl::isValidSwitchA() const {
    return mSwitchA != nullptr;
}

void StageSwitchCtrl::onSwitchB() {
    StageSwitchFunction::onSwitchBySwitchIdInfo(*mSwitchB);
}

void StageSwitchCtrl::offSwitchB() {
    StageSwitchFunction::offSwitchBySwitchIdInfo(*mSwitchB);
}

bool StageSwitchCtrl::isOnSwitchB() const {
    return StageSwitchFunction::isOnSwitchBySwitchIdInfo(*mSwitchB);
}

bool StageSwitchCtrl::isValidSwitchB() const {
    return mSwitchB != nullptr;
}

bool StageSwitchCtrl::isOnSwitchAppear() const {
    return StageSwitchFunction::isOnSwitchBySwitchIdInfo(*mSwitchAppear);
}

bool StageSwitchCtrl::isValidSwitchAppear() const {
    return mSwitchAppear != nullptr;
}

void StageSwitchCtrl::onSwitchDead() {
    StageSwitchFunction::onSwitchBySwitchIdInfo(*mSwitchDead);
}

void StageSwitchCtrl::offSwitchDead() {
    StageSwitchFunction::offSwitchBySwitchIdInfo(*mSwitchDead);
}

bool StageSwitchCtrl::isValidSwitchDead() const {
    return mSwitchDead != nullptr;
}

bool StageSwitchCtrl::isOnAllSwitchAfterB(int index) const {
    SwitchIdInfo* pSwitch;
    s32 switchNo;

    for (s32 i = 0; i < index; i++) {
        switchNo = i + mSwitchB->getSwitchNo();
        pSwitch = mSwitchB;

        if (!::getStageSwitchContainer()->getZoneSwitch(*pSwitch)->get(switchNo)) {
            return false;
        }
    }

    return true;
}

bool StageSwitchCtrl::isOnAnyOneSwitchAfterB(int index) const {
    SwitchIdInfo* pSwitch;
    s32 switchNo;

    for (s32 i = 0; i < index; i++) {
        switchNo = i + mSwitchB->getSwitchNo();
        pSwitch = mSwitchB;

        if (::getStageSwitchContainer()->getZoneSwitch(*pSwitch)->get(switchNo)) {
            return true;
        }
    }

    return false;
}

SwitchIdInfo* StageSwitchFunction::createSwitchIdInfo(const char* pSwitchName, const TableIter& rIter, bool) {
    s32 switchNo;
    bool hasSwitch = rIter.getValue< s32 >(pSwitchName, &switchNo);

    if (!hasSwitch) {
        return nullptr;
    }

    if (switchNo < 0) {
        return nullptr;
    }

    SwitchIdInfo* idInfo = new SwitchIdInfo(switchNo, rIter);

    if (switchNo < 1000) {
        ::getStageSwitchContainer()->createAndAddZone(*idInfo);
    }

    return idInfo;
}

void StageSwitchFunction::onSwitchBySwitchIdInfo(const SwitchIdInfo& rIdInfo) {
    s32 switchNo = rIdInfo.getSwitchNo();
    ZoneSwitch* pZoneSwitch = ::getStageSwitchContainer()->getZoneSwitch(rIdInfo);

    if (!pZoneSwitch->get(switchNo)) {
        pZoneSwitch->set(switchNo, true);
    }
}

void StageSwitchFunction::offSwitchBySwitchIdInfo(const SwitchIdInfo& rIdInfo) {
    s32 switchNo = rIdInfo.getSwitchNo();
    ZoneSwitch* pZoneSwitch = ::getStageSwitchContainer()->getZoneSwitch(rIdInfo);

    if (pZoneSwitch->get(switchNo)) {
        pZoneSwitch->set(switchNo, false);
    }
}

bool StageSwitchFunction::isOnSwitchBySwitchIdInfo(const SwitchIdInfo& rIdInfo) {
    s32 switchNo = rIdInfo.getSwitchNo();
    StageSwitchContainer* pContainer = ::getStageSwitchContainer();

    return pContainer->getZoneSwitch(rIdInfo)->get(switchNo);
}

StageSwitchCtrl* MR::createStageSwitchCtrl(NameObj*, const TableIter& rIter) {
    return new StageSwitchCtrl(rIter);
}

StageSwitchContainer::StageSwitchContainer() : NameObj("ステージスイッチ"), mSwitches(), mGlobalSwitches() {
    lumacore::setClass(this, lumacore::kClassId< StageSwitchContainer >);
    mGlobalSwitches = new ZoneSwitch();
}

StageSwitchCtrl::StageSwitchCtrl(const TableIter& rIter) : mSwitchA(), mSwitchB(), mSwitchAppear(), mSwitchDead() {
    mSwitchA = StageSwitchFunction::createSwitchIdInfo("SW_A", rIter, true);
    mSwitchB = StageSwitchFunction::createSwitchIdInfo("SW_B", rIter, true);
    mSwitchAppear = StageSwitchFunction::createSwitchIdInfo("SW_APPEAR", rIter, false);
    mSwitchDead = StageSwitchFunction::createSwitchIdInfo("SW_DEAD", rIter, false);
}
