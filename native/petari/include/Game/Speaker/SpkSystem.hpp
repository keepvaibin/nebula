#pragma once

#include "Game/Speaker/SpkData.hpp"
#include "Game/Speaker/SpkMixingBuffer.hpp"
#include "Game/Speaker/SpkSound.hpp"
#include <JSystem/JAudio2/JASGlobal.hpp>
#include <JSystem/JAudio2/JASHeapCtrl.hpp>

class SpkSystem : JASGlobalInstance< SpkSystem > {
public:
    SpkSystem(NebulaArena*);

    void setResource(AssetFolder*, u16, u16);
    void framework();
    void startSound(s32, s32, SpkSoundHandle*);
    static void newSoundMemPool(s32);
    static void connect(s32);
    static void disconnect(s32);
    static void reconnect(s32);
    static void extensionProcess(s32, s32);
    static f32 getDeviceVolume(s32);

    NebulaArena* mHeap;
    SpkData* mData;
    SpkMixingBuffer* mMixBuffer;
    SpkSoundHolder* mSoundHolder;
};
