// The game's own sounds for LumaCore's audio service (contract services-audio 8; plan 2.6 layer 1b): a mod starts a
// sound of the game's table by its name (SE_PM_JUMP_M), heard everywhere (no place) or at a place in the world, still
// or following the player or the camera, through the game's own sound engine (its distances, pan, reverb and limits).
// The host's audio provider (runtime/src/luma_audio.cpp) checks the mod and the thread and calls these; it also asks
// where the listener and the player are for the mods' own placed voices. Game thread only.
#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Sound/SoundStarter.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <luma_services.h>
#include <nebula_game_api.h>
#include <stdint.h>

namespace {
    const u32 cSlots = 64;         // every mod's together
    const u32 cOwnMost = 16;       // LUMA_AUDIO_GAME_SOUNDS_MOST: one mod's at once
    const u32 cNoPlace = 0xFFFFFFFFu;
    const u32 cFollowMost = 2;     // LumaAudioFollow: 0 a point, 1 the player, 2 the camera

    struct Slot {
        const void* mOwner;   // null: free; kept after its sound ended, so the handle answers OK until reused
        u64 mId;              // the handle the mod holds: (count << 6) | slot, never 0
        SoundHandle mHandle;  // the game's: its sound detaches it when it dies
        Vec3f mPos;           // where the game hears it (a still sound's place is copied at its start)
        Vec3f mOffset;        // a following sound's place from what it follows
        u32 mFollow;          // cNoPlace, or LumaAudioFollow
    };

    // File scope, not function-local statics (the game module cannot link a guarded static's initialisation).
    Slot sSlots[cSlots];
    u64 sCount = 0;

    bool cameraThere() {
        return MR::isExistSceneObj(SceneObj_CameraContext);
    }

    MarioActor* player() {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return nullptr;
        }
        MarioHolder* pHolder = MR::getMarioHolder();
        return pHolder != nullptr ? pHolder->getMarioActor() : nullptr;
    }

    // What a following sound is placed from: false when it is not there now (no scene, no camera).
    bool base(u32 follow, Vec3f* pOut) {
        if (follow == 0) {
            pOut->set(0.0f, 0.0f, 0.0f);
            return true;
        }
        if (follow == 1) {
            MarioActor* pPlayer = player();
            if (pPlayer == nullptr) {
                return false;
            }
            *pOut = pPlayer->mPosition;
            return true;
        }
        if (!cameraThere()) {
            return false;
        }
        *pOut = MR::getCamPos();
        return true;
    }

    // Its sound plays (the engine detaches the handle when the sound dies).
    bool live(const Slot& rSlot) {
        return rSlot.mOwner != nullptr && rSlot.mHandle.isSoundAttached();
    }

    bool following(const Slot& rSlot) {
        return rSlot.mFollow != cNoPlace && rSlot.mFollow != 0;
    }

    Slot* find(const void* pOwner, u64 id) {
        const u32 index = static_cast< u32 >(id & (cSlots - 1));
        Slot& rSlot = sSlots[index];
        if (id == 0 || rSlot.mOwner != pOwner || rSlot.mId != id) {
            return nullptr;
        }
        return &rSlot;
    }

    bool finite3(const float* pPos) {
        for (u32 i = 0; i < 3; i++) {
            const float value = pPos[i];
            if (!(value == value) || value > 1.0e7f || value < -1.0e7f) {
                return false;
            }
        }
        return true;
    }

    // ms to the game's frames (60 a second), rounded up; 0 stays 0 (at once).
    u32 framesOf(u32 ms) {
        return (ms * 60u + 999u) / 1000u;
    }
}  // namespace

extern "C" {
// Starts the game's sound `pName` for `pOwner`. follow cNoPlace: heard everywhere (pPos unused); otherwise at pPos (a
// point in the world, or a place from the player or the camera). LUMA_OK and *pOut; LUMA_E_NOT_FOUND: no sound of that
// name in the game's table; LUMA_E_BUSY: the game does not start it now (its sound effects are held, as in the pause
// menu, its own limit, or no scene for a placed sound); LUMA_E_LIMIT: the owner holds cOwnMost; LUMA_E_ARGUMENT: a
// follow past cFollowMost or a place not finite.
NEBULA_GAME_API int32_t luma_game_sound_play(const void* pOwner, const char* pName, uint32_t follow, const float* pPos,
                                             uint64_t* pOut) {
    if (pOwner == nullptr || pName == nullptr || pOut == nullptr ||
        (follow != cNoPlace && (follow > cFollowMost || pPos == nullptr || !finite3(pPos)))) {
        return LUMA_E_ARGUMENT;
    }
    AudSoundNameConverter* pNames = AudSingletonHolder< AudSoundNameConverter >::get();
    AudSystem* pSystem = AudWrap::getSystem();
    SoundStarter* pStarter = SoundGlobal< SoundStarter >::getInstance();
    if (pNames == nullptr || pSystem == nullptr || pStarter == nullptr) {
        return LUMA_E_BUSY;
    }
    const SoundID id = pNames->getSoundID(pName);
    if (static_cast< u32 >(id) == 0xFFFFFFFFu) {
        return LUMA_E_NOT_FOUND;
    }
    if (!pSystem->isEnableStartSound(id) ||
        (pSystem->mIsTrigSeBlocked && id.getGroupID() != SE_HOME_BUTTON_MENU && id.getGroupID() != SE_SYSTEM)) {
        return LUMA_E_BUSY;
    }
    // A free slot first; else one whose sound ended (its handle then answers LUMA_E_HANDLE).
    u32 owned = 0;
    Slot* pFree = nullptr;
    Slot* pEnded = nullptr;
    for (u32 i = 0; i < cSlots; i++) {
        if (live(sSlots[i])) {
            if (sSlots[i].mOwner == pOwner) {
                owned++;
            }
        } else if (sSlots[i].mOwner == nullptr) {
            if (pFree == nullptr) {
                pFree = &sSlots[i];
            }
        } else if (pEnded == nullptr) {
            pEnded = &sSlots[i];
        }
    }
    if (pFree == nullptr) {
        pFree = pEnded;
    }
    if (owned >= cOwnMost) {
        return LUMA_E_LIMIT;
    }
    if (pFree == nullptr) {
        return LUMA_E_BUSY;  // every mod together holds cSlots: the game's own limit, as a full voice table
    }
    Vec3f from(0.0f, 0.0f, 0.0f);
    if (follow != cNoPlace) {
        if (!base(follow, &from)) {
            return LUMA_E_BUSY;
        }
        pFree->mOffset.set(pPos[0], pPos[1], pPos[2]);
        pFree->mPos.set(from.x + pPos[0], from.y + pPos[1], from.z + pPos[2]);
    }
    pFree->mHandle.releaseSound();
    if (!pStarter->startSound(id, &pFree->mHandle, follow != cNoPlace ? &pFree->mPos : nullptr) ||
        !pFree->mHandle.isSoundAttached()) {
        pFree->mHandle.releaseSound();
        return LUMA_E_BUSY;
    }
    sCount++;
    const u32 index = static_cast< u32 >(pFree - sSlots);
    pFree->mOwner = pOwner;
    pFree->mId = (sCount << 6) | index;
    pFree->mFollow = follow;
    *pOut = pFree->mId;
    return LUMA_OK;
}

// Moves a placed sound: follow and pPos as at its start (a sound started with no place cannot be placed:
// LUMA_E_ARGUMENT). LUMA_OK also when it has ended (nothing to move); LUMA_E_HANDLE: not the owner's.
NEBULA_GAME_API int32_t luma_game_sound_place(const void* pOwner, uint64_t id, uint32_t follow, const float* pPos) {
    if (pPos == nullptr || follow > cFollowMost || !finite3(pPos)) {
        return LUMA_E_ARGUMENT;
    }
    Slot* pSlot = find(pOwner, id);
    if (pSlot == nullptr) {
        return LUMA_E_HANDLE;
    }
    if (pSlot->mFollow == cNoPlace) {
        return LUMA_E_ARGUMENT;
    }
    if (!live(*pSlot)) {
        return LUMA_OK;
    }
    pSlot->mFollow = follow;
    pSlot->mOffset.set(pPos[0], pPos[1], pPos[2]);
    Vec3f from;
    if (base(follow, &from)) {
        pSlot->mPos.set(from.x + pPos[0], from.y + pPos[1], from.z + pPos[2]);
        pSlot->mHandle->setPos(pSlot->mPos);
    }
    return LUMA_OK;
}

// Stops a sound over fade_ms (0: at once). LUMA_OK also when it has ended; LUMA_E_HANDLE: not the owner's. The handle
// is the owner's no more.
NEBULA_GAME_API int32_t luma_game_sound_stop(const void* pOwner, uint64_t id, uint32_t fadeMs) {
    Slot* pSlot = find(pOwner, id);
    if (pSlot == nullptr) {
        return LUMA_E_HANDLE;
    }
    if (live(*pSlot)) {
        pSlot->mHandle->stop(framesOf(fadeMs));
        pSlot->mHandle.releaseSound();
    }
    pSlot->mOwner = nullptr;
    return LUMA_OK;
}

// The owner is gone: every sound of its stops at once (a mod's stop is silent the same frame).
NEBULA_GAME_API void luma_game_sound_gone(const void* pOwner) {
    for (u32 i = 0; i < cSlots; i++) {
        if (sSlots[i].mOwner != pOwner) {
            continue;
        }
        if (live(sSlots[i])) {
            sSlots[i].mHandle->stop();
            sSlots[i].mHandle.releaseSound();
        }
        sSlots[i].mOwner = nullptr;
    }
}

// Once a frame while the host has a following sound or a placed voice: every following sound moved to its place now.
// Returns the following sounds still playing (0: the host stops asking for them).
NEBULA_GAME_API uint32_t luma_game_sound_follow(void) {
    u32 count = 0;
    for (u32 i = 0; i < cSlots; i++) {
        if (following(sSlots[i]) && live(sSlots[i])) {
            count++;
        }
    }
    if (count == 0) {
        return 0;
    }
    Vec3f from[cFollowMost + 1];
    bool there[cFollowMost + 1] = {};
    for (u32 f = 1; f <= cFollowMost; f++) {
        there[f] = base(f, &from[f]);
    }
    for (u32 i = 0; i < cSlots; i++) {
        Slot& rSlot = sSlots[i];
        if (!following(rSlot) || !live(rSlot) || !there[rSlot.mFollow]) {
            continue;
        }
        const Vec3f& rFrom = from[rSlot.mFollow];
        rSlot.mPos.set(rFrom.x + rSlot.mOffset.x, rFrom.y + rSlot.mOffset.y, rFrom.z + rSlot.mOffset.z);
        rSlot.mHandle->setPos(rSlot.mPos);
    }
    return count;
}

// Where the mods' placed voices are heard from: pOut[0..2] the camera's place, [3..5] its front, [6..8] its up, [9..11]
// the player's place. Bit 1: the camera is there (else [0..8] are unchanged); bit 2: the player is.
NEBULA_GAME_API uint32_t luma_game_listener(float* pOut) {
    if (pOut == nullptr) {
        return 0;
    }
    u32 bits = 0;
    if (cameraThere()) {
        const Vec3f pos = MR::getCamPos();
        const Vec3f front = MR::getCamZdir();
        const Vec3f up = MR::getCamYdir();
        pOut[0] = pos.x;
        pOut[1] = pos.y;
        pOut[2] = pos.z;
        pOut[3] = front.x;
        pOut[4] = front.y;
        pOut[5] = front.z;
        pOut[6] = up.x;
        pOut[7] = up.y;
        pOut[8] = up.z;
        bits |= 1u;
    }
    if (MarioActor* pPlayer = player()) {
        pOut[9] = pPlayer->mPosition.x;
        pOut[10] = pPlayer->mPosition.y;
        pOut[11] = pPlayer->mPosition.z;
        bits |= 2u;
    }
    return bits;
}
}
