#pragma once

#include <nebula_geometry.hpp>

class SoundAudible;
class SoundID;
struct SoundParams;

class SoundAudience {
public:
    virtual ~SoundAudience();

    // TODO: Should all be pure virtual.
    virtual SoundAudible* newAudible(const Vec3f&, SoundID, const Vec3f*, u32) = 0;
    virtual int getMaxChannels() = 0;
    virtual void deleteAudible(SoundAudible*) = 0;
    virtual u32 calcPriority(SoundAudible*) = 0;
    virtual void mixChannelOut(const SoundParams&, SoundAudible*, int) = 0;
};
