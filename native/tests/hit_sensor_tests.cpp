// SPDX-License-Identifier: GPL-3.0-only
// HitSensor (Game/LiveActor/HitSensor.cpp, P-148): the list of sensors a sensor touches grows past the group size it was
// made with, in the order they were found; the console dropped any more. The sensor and the arenas are the game's; the
// group it joins and the mods' event table are the test's.
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/SensorHitChecker.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"

#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }
}  // namespace

// No group and no mod: the sensor is made and filled, nothing more.
namespace MR {
    void initHitSensorGroup(HitSensor*) {}
}  // namespace MR
void SensorGroup::add(HitSensor*) {}
void SensorGroup::remove(HitSensor*) {}
namespace NebulaMods {
    bool sWanted[NEBULA_MOD_EVENT_COUNT];
    void dispatch(NebulaModEvent, s32, void*, const char*) {}
}  // namespace NebulaMods

int main() {
    NebulaArena::create(nullptr);

    // A coin's sensor in a swarm: made for 2, touched by 9 this step.
    HitSensor* const pCoin = new HitSensor(0, 2, 50.0f, nullptr);
    HitSensor* touching[9];
    for (HitSensor*& pOther : touching) {
        pOther = new HitSensor(0, 2, 50.0f, nullptr);
        pCoin->addHitSensor(pOther);
    }
    expect(pCoin->mSensorCount == 9, "all 9 touching sensors are kept, not the first 2");
    expect(pCoin->mGroupSize >= 9, "the list's room grew");
    bool inOrder = true;
    for (u32 i = 0; i < 9; i++) {
        inOrder = inOrder && pCoin->mSensors[i] == touching[i];
    }
    expect(inOrder, "the sensors are kept in the order they were found");

    // The next step starts the list again and fills the grown room without growing it.
    pCoin->mSensorCount = 0;
    const u32 room = pCoin->mGroupSize;
    for (HitSensor* pOther : touching) {
        pCoin->addHitSensor(pOther);
    }
    expect(pCoin->mSensorCount == 9 && pCoin->mGroupSize == room && pCoin->mSensors[8] == touching[8],
           "the next step keeps 9 again in the same room");

    // A sensor in no group (group size 0) is in no pair, and keeps nothing.
    HitSensor* const pAlone = new HitSensor(0, 0, 50.0f, nullptr);
    pAlone->addHitSensor(pCoin);
    expect(pAlone->mSensorCount == 0 && pAlone->mSensors == nullptr, "a sensor in no group keeps nothing");

    if (sFailures != 0) {
        return 1;
    }
    printf("hit_sensor_tests: all passed\n");
    return 0;
}
