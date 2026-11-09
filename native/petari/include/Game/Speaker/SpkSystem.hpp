#pragma once

#include "Game/Speaker/SpkData.hpp"
#include "Game/Speaker/SpkSound.hpp"
#include "Game/Sound/SoundGlobal.hpp"

class SpkSystem : SoundGlobal< SpkSystem > {
public:
    SpkSystem(NebulaArena*);

    void setResource(AssetFolder*, u16, u16);
    void framework();
    void startSound(s32, s32, SpkSoundHandle*);
    // The controller's last sound AudSpeakerWrap started.
    SpkSoundHandle* padHandle(s32 padChannel) {
        return &mSoundHolder->mPadHandles[padChannel];
    }
    static void connect(s32);
    static void disconnect(s32);
    static f32 getDeviceVolume(s32);

    NebulaArena* mArena;
    SpkData* mData;
    SpkSoundHolder* mSoundHolder;
};
