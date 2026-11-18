#include "Game/Player/MarioMessenger.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace {
    const u32 cFirstRoom = 32;  // the console's whole room
}  // namespace

MarioMessenger::MarioMessenger(HitSensor* pSender) : NameObj("マリオメッセンジャー"), mSender(pSender) {
    mReceiverArray = new HitSensor*[cFirstRoom];
    mMsgArray = new u32[cFirstRoom];
    mArraySize = 0;
    mArrayRoom = cFirstRoom;

    MR::connectToScene(this, MR::MovementType_PlayerMessenger, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void MarioMessenger::movement() {
    for (int i = 0; i < mArraySize; i++) {
        MR::sendArbitraryMsg(mMsgArray[i], mReceiverArray[i], mSender);
    }

    mArraySize = 0;
}

// Every message Mario sends this step goes out at the next movement; past the room it grows (N-3006). The console
// dropped any message past 32 without a word.
void MarioMessenger::addRequest(HitSensor* pReceiver, u32 msg) {
    if (mArraySize == mArrayRoom) {
        mReceiverArray = MR::growArray(mReceiverArray, static_cast< s32 >(mArrayRoom));
        mMsgArray = MR::growArray(mMsgArray, static_cast< s32 >(mArrayRoom));
        mArrayRoom *= 2;
    }

    mReceiverArray[mArraySize] = pReceiver;
    mMsgArray[mArraySize] = msg;
    mArraySize++;
}

MarioMessenger::~MarioMessenger() {
}
