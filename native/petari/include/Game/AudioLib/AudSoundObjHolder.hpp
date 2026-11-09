#pragma once

class AudSoundObject;
class NebulaArena;

/// @brief The sound objects whose map code is cleared each frame. The list grows in the arena it was made in.
class AudSoundObjHolder {
public:
    AudSoundObjHolder(NebulaArena* pArena, s32 capacity);

    void update();
    void add(AudSoundObject* pSound);
    void remove(AudSoundObject* pSound);

private:
    AudSoundObject** mArray;
    s32 mCapacity;
    s32 mSize;
};
