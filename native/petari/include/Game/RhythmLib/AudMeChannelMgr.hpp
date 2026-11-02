#pragma once

#include <JSystem/JAudio2/JASChannel.hpp>
#include <JSystem/JAudio2/JASSoundParams.hpp>

class AudMeTrack;
class JASChannel;

class AudMeChannelMgr {
public:
    AudMeChannelMgr(AudMeTrack*);

    bool noteOff(u32, u16);
    void setPauseFlag(bool);

    const JASChannelParams& getChannelParams() const {
        return mChannelParams;
    }

    JASChannel* mChannels[8];
    JASChannelParams mChannelParams;
    u16 _38[8];
    JASSoundParams mParams;
    AudMeTrack* mTrack;
};
