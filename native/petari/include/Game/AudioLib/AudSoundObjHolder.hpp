#pragma once


class AudSoundObject;
class NebulaArena;

class AudSoundObjHolder {
public:
    /// @brief Creates a new AudSoundObjHolder
    AudSoundObjHolder(NebulaArena* pHeap, s32 capacity);
    void update();
    void add(AudSoundObject* pSound);
    void remove(AudSoundObject* pSsound);
    /// @brief Shifts to the left sounds from initialIndex to finalIndex in mArray to replace AudSoundObject stored in initialIndex
    void moveOver(s32 initialIndex, s32 finalIndex);

    AudSoundObject** mArray;  ///< AudSoundObject storage
    s32 mCapacity;            ///< Physical capacity of mArray
    s32 mSize;                ///< Logical capacity of mArray
};
