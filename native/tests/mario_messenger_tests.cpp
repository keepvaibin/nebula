// SPDX-License-Identifier: GPL-3.0-only
// MarioMessenger (Game/Player/MarioMessenger.cpp, N-3006): every message Mario asks to send in a step goes out at the
// next movement, in order, however many; the console dropped any past 32. The messenger and the arenas are the game's;
// its base class's scene calls and the message delivery are the test's.
#include "Game/NameObj/NameObj.hpp"
#include "Game/Player/MarioMessenger.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

#include <stdint.h>
#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    // What was delivered, in order.
    struct Sent {
        u32 msg;
        HitSensor* pReceiver;
        HitSensor* pSender;
    };
    Sent sSent[256];
    u32 sSentCount = 0;

    // Receivers the messages name; the messenger only passes them on.
    HitSensor* receiver(u32 i) {
        static char sTargets[256];
        return reinterpret_cast< HitSensor* >(&sTargets[i]);
    }
}  // namespace

// The base class and the scene, as far as the messenger uses them.
NameObj::NameObj(const char* pName)
    : mName(pName), mMovementFlag(0), mLumaClass(0), mExecutorIdx(-1), mDrawSlot(0), mMotionEpoch(0), mListPlace{-1, -1, -1},
      mDrawBufferPlace(-1), mNameHolder(nullptr), mHolderPlace(-1) {}
NameObj::~NameObj() {}
void NameObj::init(const TableIter&) {}
void NameObj::initAfterPlacement() {}
void NameObj::movement() {}
void NameObj::draw() const {}
void NameObj::calcAnim() {}
void NameObj::calcViewAndEntry() {}
namespace MR {
    void connectToScene(NameObj*, int, int, int, int) {}
    bool sendArbitraryMsg(u32 msg, HitSensor* pReceiver, HitSensor* pSender) {
        if (sSentCount < 256) {
            sSent[sSentCount++] = {msg, pReceiver, pSender};
        }
        return true;
    }
}  // namespace MR

int main() {
    NebulaArena::create(nullptr);
    HitSensor* const pMario = receiver(255);
    MarioMessenger* const pMessenger = new MarioMessenger(pMario);

    // A step that asks for 100 messages (a spin through a crowd): all go out, in order.
    for (u32 i = 0; i < 100; i++) {
        pMessenger->addRequest(receiver(i), 1000 + i);
    }
    pMessenger->movement();
    expect(sSentCount == 100, "all 100 messages of one step are sent, not the first 32");
    bool inOrder = true;
    for (u32 i = 0; i < sSentCount; i++) {
        inOrder = inOrder && sSent[i].msg == 1000 + i && sSent[i].pReceiver == receiver(i) && sSent[i].pSender == pMario;
    }
    expect(inOrder, "each goes to its receiver with its message, from Mario, in the order asked");

    // The next step starts empty.
    sSentCount = 0;
    pMessenger->addRequest(receiver(7), 42);
    pMessenger->movement();
    expect(sSentCount == 1 && sSent[0].msg == 42 && sSent[0].pReceiver == receiver(7), "the next step sends only its own");

    if (sFailures != 0) {
        return 1;
    }
    printf("mario_messenger_tests: all passed\n");
    return 0;
}
