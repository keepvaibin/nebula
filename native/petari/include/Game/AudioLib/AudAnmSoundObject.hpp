#pragma once

#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/Sound/SoundAnimator.hpp"

struct SoundStarter;
struct SoundParams;
class SoundAnimationSound;

class AudAnmSoundObject : public AudSoundObject, public SoundAnimator {
public:
    AudAnmSoundObject(Vec3f*, u8, NebulaArena*);

    void update(f32);
    bool playsSound(const SoundAnimationSound*, const Vec3f&, f32);
    u32 getSoundID(const SoundAnimationSound*, const Vec3f&, f32);
    SoundHandle* getFreeHandle(const SoundAnimationSound*);
    void skip(f32);
    void setStartPos(f32);
    bool hasAnim() const {
        return mSoundAnimation != nullptr;
    }
    void updateAnimSound(f32, const Vec3f&, SoundStarter*);
    void startAnimSound(const Vec3f&, f32, SoundStarter*);
    bool releaseHandleIfNecessary(SoundHandle*, u32);

    SoundHandles* getAnimatorHandles() {
        return SoundAnimator::mHandles;
    }
};
