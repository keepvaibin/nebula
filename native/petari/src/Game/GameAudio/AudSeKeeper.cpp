#include "Game/GameAudio/AudSeKeeper.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/ModelData.hpp"

AudSeKeeperBox::AudSeKeeperBox() : mSoundObj(nullptr), mJoint(nullptr), mHash(0) {
}

AudSeKeeperBox::~AudSeKeeperBox() {
    if (mSoundObj != nullptr) {
        mSoundObj->dispose();
        delete mSoundObj;
    }
}

void AudSeKeeperBox::update() {
    updateJointPos();
    if (mSoundObj != nullptr) {
        mSoundObj->process();
    }
}

void AudSeKeeperBox::add(Joint* pJoint, u16 hash) {
    mJoint = pJoint;
    mHash = hash;
    updateJointPos();
    mSoundObj = new AudSoundObject(&mPos, 8, NebulaArena::current());
}

void AudSeKeeperBox::updateJointPos() {
    mPos.x = MR::getJointTransX(mJoint);
    mPos.y = MR::getJointTransY(mJoint);
    mPos.z = MR::getJointTransZ(mJoint);
}

AudSeKeeper::AudSeKeeper(LiveActor* pActor, int boxNumHint) : ArenaOwned(), mActor(pActor) {
    mBoxes.init(boxNumHint > 0 ? boxNumHint : 1);
}

AudSeKeeper::~AudSeKeeper() {
    for (int idx = 0; idx < mBoxes.size(); idx++) {
        delete mBoxes[idx];
    }
}

void AudSeKeeper::update() {
    for (int idx = 0; idx < mBoxes.size(); idx++) {
        mBoxes[idx]->update();
    }
}

// A box is named by pJointName2 when given, else by its joint.
void AudSeKeeper::makeBox(const char* pJointName, const char* pJointName2) {
    Joint* joint = MR::getJoint(mActor, pJointName);
    const u16 hash = MR::getHashCode(pJointName2 != nullptr ? pJointName2 : pJointName);

    for (int idx = 0; idx < mBoxes.size(); idx++) {
        if (hash == mBoxes[idx]->mHash) {
            return;
        }
    }

    AudSeKeeperBox* pBox = new AudSeKeeperBox();
    pBox->add(joint, hash);
    mBoxes.push_back(pBox);
}

bool AudSeKeeper::playLevel(const char* pJointName, const char* pSoundName) {
    const int idx = getBoxEqualName(pJointName);
    if (idx < 0) {
        return false;
    }
    MR::startSoundObjectLevel(mBoxes[idx]->mSoundObj, pSoundName);
    return true;
}

int AudSeKeeper::getBoxEqualName(const char* pJointName) {
    const u16 hash = MR::getHashCode(pJointName);
    for (int idx = 0; idx < mBoxes.size(); idx++) {
        if (hash == mBoxes[idx]->mHash) {
            return idx;
        }
    }
    return -1;
}
