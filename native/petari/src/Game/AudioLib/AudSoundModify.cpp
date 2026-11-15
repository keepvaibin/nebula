#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/AudioLib/AudUtil.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Util/MathUtil.hpp"

// Per-sound responses to the caller's level: one table for every sound that takes one.

namespace {
    const s32 cMagicPntGVolMinPrm = 100;
    const s32 cMagicPntGVolMaxPrm = 1500;
    const f32 cMagicPntGVolMin = 0.8f;
    const f32 cMagicPntGVolMax = 1.0f;
    const s32 cMagicPntGPitMinPrm = 100;
    const s32 cMagicPntGPitMaxPrm = 2000;
    const f32 cMagicPntGPitMin = 0.5f;
    const f32 cMagicPntGPitMax = 2.0f;
    const s32 cUnizoRollVolMinPrm = 200;
    const s32 cUnizoRollVolMaxPrm = 700;
    const f32 cUnizoRollVolMin = 0.1f;
    const f32 cUnizoRollVolMax = 1.0f;
    const s32 cUnizoRollPitMinPrm = 50;
    const s32 cUnizoRollPitMaxPrm = 1000;
    const f32 cUnizoRollPitMin = 0.8f;
    const f32 cUnizoRollPitMax = 1.0f;
    const s32 cUnizoRollGroundVolMinPrm = 200;
    const s32 cUnizoRollGroundVolMaxPrm = 700;
    const f32 cUnizoRollGroundPitMin = 0.9f;
    const f32 cUnizoRollGroundPitMax = 1.0f;
    const s32 cTorpedoPitMinPrm = 300;
    const s32 cTorpedoPitMaxPrm = 2000;
    const f32 cTorpedoPitMin = 1.27f;
    const f32 cTorpedoPitMax = 1.2f;
    const s32 cTorpedoWaitMinPrm = 200;
    const s32 cTorpedoWaitMaxPrm = 3000;
    const f32 cTorpedoWaitMin = 4.0f;
    const f32 cTorpedoWaitMax = 24.0f;
    const s32 cBombBoundVolMinPrm = 100;
    const s32 cBombBoundVolMaxPrm = 1000;

    inline s32 clampLevel(s32 level, s32 max) {
        return MR::min(MR::max(level, 0), max);
    }
};  // namespace

// level: the caller's main value; subLevel: a second value some sounds read (-1: none).
void AudSoundObject::modifySe(SoundHandle* pHandle, s32 level, s32 subLevel) {
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    u32 steps = 0;

    switch (pHandle->getSound()->getID()) {
    case SE_OJ_COIN_BOUND:
    case SE_OJ_LV_SNOW_DIG: {
        volume = level / 100.0f;
        break;
    }
    case SE_OJ_KEY_SWITCH_BOUND: {
        volume = level / 100.0f;
        volume *= volume;
        break;
    }
    case SE_OJ_COCONUT_BOUND:
    case SE_OJ_COCONUT_BOUND_WATER: {
        volume = level / 100.0f;
        volume *= volume;
        volume *= 2.0f;
        if (volume > 1.0f) {
            volume = 1.0f;
        }
        break;
    }
    case SE_AT_LV_ASTRO_DOME_WIND_1: {
        f32 t = level / 100.0f;
        pitch = (t * t) * 0.5f + 1.0f;
        break;
    }
    case SE_OJ_LV_ROPE_SWING_WIND_1: {
        level = clampLevel(level, 100);
        volume = level / 100.0f;
        volume *= volume;
        pitch = volume * 0.5f + 0.9f;
        break;
    }
    case SE_OJ_LV_ROPE_SWING_WIND_2: {
        level = clampLevel(level, 100);
        if (level < 40) {
            pitch = 1.0f;
            volume = 0.0f;
        } else {
            volume = (level - 40) / 60.0f;
            volume *= volume;
            pitch = volume * 0.4f + 0.9f;
        }
        break;
    }
    case SE_SY_ACTION_COMBO: {
        writePort(pHandle, 11, clampLevel(level, 12));
        break;
    }
    case SE_SY_FLOWER_GET_COMBO: {
        level = MR::max(level, 0);
        while (level > 12 * 3) {
            level -= 12;
        }
        writePort(pHandle, 11, level);
        break;
    }
    case SE_PM_LV_PULL_BACK_FLY: {
        f32 t = clampLevel(level, 100) / 50.0f;
        pitch = t * t + 0.75f;
        break;
    }
    case SE_OJ_LV_COCONUT_ROLL: {
        f32 t = clampLevel(level, 100) / 100.0f;
        pitch = t / 2.0f + 0.5f;
        volume = t * t;
        break;
    }
    case SE_OJ_LV_COCONUT_ROLL_WATER: {
        f32 t = clampLevel(level, 100) / 100.0f;
        pitch = t / 4.0f + 0.75f;
        volume = t * t;
        break;
    }
    case SE_OJ_LV_SPACE_COCOON_DRAG: {
        f32 t = clampLevel(level, 100) / 100.0f;
        t *= t;
        pitch = t + 1.0f;
        volume = t * 0.3f + 0.7f;
        break;
    }
    case SE_PM_LV_TERESA_MARIO_FLY: {
        f32 t = clampLevel(level, 100) / 100.0f;
        volume = t * t;
        if (volume < 0.2f) {
            volume = 0.2f;
        }
        pitch = t / 2.0f + 0.5f;
        break;
    }
    case SE_BM_KOOPA_SPIN_WIND: {
        f32 t = level / 100.0f;
        pitch = t / 2.0f + 1.0f;
        volume = t * t + 0.2f;
        if (volume > 1.0f) {
            volume = 1.0f;
        }
        break;
    }
    case SE_BM_KOOPA_SPIN_HIT_2: {
        f32 t = level / 100.0f;
        pitch = t / 2.0f + 0.9f;
        break;
    }
    case SE_OJ_BIG_BUBBLE_DPD_HIT: {
        f32 t = level / 100.0f;
        pitch = t / 3.0f + 0.9f;
        break;
    }
    case SE_OJ_LV_SPIN_BOX_SLIDE:
    case SE_OJ_SPIN_BOX_SPIN:
    case SE_BM_LV_KOOPA_ROTATE_LOW: {
        f32 t = level / 100.0f;
        pitch = t / 2.0f + 0.75f;
        volume = t * t;
        break;
    }
    case SE_PM_LV_T_MARIO_RIDE_WIND: {
        f32 t = clampLevel(level, 100) / 100.0f;
        pitch = t / 2.0f + 0.75f;
        volume = t * t;
        break;
    }
    case SE_PM_LV_FOO_FLYING: {
        steps = 6;
        f32 t = (100 - clampLevel(level, 100)) / 100.0f;
        t *= t;
        pitch = t / 10.0f + 0.95f;
        volume = t / 4.0f + 0.75f;
        break;
    }
    case SE_BM_LV_KOOPA_SWAVE_MOVE: {
        f32 t = clampLevel(level, 100) / 100.0f;
        pitch = (t * t) / 2.0f + 1.0f;
        break;
    }
    case SE_SY_LV_BIG_BUBBLE_WIND: {
        f32 t = clampLevel(level, 100) / 100.0f;
        t *= t;
        pitch = t / 4.0f + 0.75f;
        volume = t / 2.0f + 0.5f;
        break;
    }
    case SE_BM_LV_SKL_GUARD_ALARM: {
        level = clampLevel(level, 100000);
        pitch = AudUtil::linearMinMax(level, 300, 2000, 2.0f, 1.85f);
        f32 wait = AudUtil::linearMinMax(level, 200, 3000, 4.0f, 24.0f);
        writePort(pHandle, 11, static_cast< s32 >(wait));
        break;
    }
    case SE_OJ_LV_FLEX_SPHERE_MOVE: {
        f32 t = clampLevel(level, 100) / 100.0f;
        t *= t;
        pitch = t + 0.25f;
        volume = t / 2.0f + 0.3f;
        break;
    }
    case SE_SM_LV_IRONSPH_ROLL1:
    case SE_SM_LV_IRONSPH_ROLL_RAIL: {
        volume = AudUtil::linearMax(clampLevel(level, 10000), 100, 1.0f);
        break;
    }
    case SE_SM_LV_BALLOONSPH_ROLL: {
        volume = AudUtil::linearMax(clampLevel(level, 1000000), 10000, 1.0f);
        break;
    }
    case SE_SM_IRONSPH_HIT_WALL:
    case SE_SM_BALLOONSPH_HIT_WALL: {
        volume = AudUtil::linearMax(clampLevel(level, 100), 20, 1.0f);
        break;
    }
    case SE_SM_LV_SLED_YOS_RUN1: {
        volume = AudUtil::linearMax(clampLevel(level, 10000), 2000, 1.0f);
        break;
    }
    case SE_SM_LV_SLED_MOVE: {
        level = clampLevel(level, 10000);
        volume = AudUtil::linearMinMax(level, 500, 3000, 0.4f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 1600, 3000, 0.85f, 1.5f);
        break;
    }
    case SE_SM_LV_SLED_YOS_WATER_RUN: {
        volume = AudUtil::linearMinMax(clampLevel(level, 10000), 500, 3000, 0.4f, 1.0f);
        break;
    }
    case SE_EM_LV_OTAJACK_PULL: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 2000, 40000, 0.5f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 2000, 40000, 0.8f, 1.1f);
        break;
    }
    case SE_SM_LV_FOLLOWKIKKI_MOVE: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMax(level, 2500, 1.0f);
        pitch = AudUtil::linearMinMax(level, 0, 2000, 1.0f, 1.5f);
        break;
    }
    case SE_SM_LV_KART_ENGINE_LOW: {
        level = clampLevel(level, 100000);
        if (level <= 1000) {
            volume = AudUtil::linearMinMax(level, 0, 1000, 0.3f, 1.0f);
        } else {
            volume = AudUtil::linearMinMax(level, 1500, 2500, 1.0f, 0.0f);
        }
        pitch = AudUtil::linearMinMax(level, 0, 2000, 1.0f, 2.0f);
        break;
    }
    case SE_SM_LV_KART_ENGINE_HIGH: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 1500, 2500, 0.0f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 1500, 4000, 1.0f, 2.0f);
        break;
    }
    case SE_SM_KART_LAND:
    case SE_SM_JETSWING_LAND: {
        volume = AudUtil::linearMinMax(clampLevel(level, 100000), 1000, 2500, 0.7f, 1.0f);
        break;
    }
    case SE_SM_LV_HELIBIRD_FLY: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 500, 3000, 0.4f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 500, 3000, 1.0f, 1.5f);
        break;
    }
    case SE_SM_LV_MOCINA_PULL2: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 10, 100, 0.5f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 10, 70, 1.2f, 1.8f);
        break;
    }
    case SE_SM_LV_JETSWING_ENGINE: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 500, 3000, 0.2f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 1600, 8000, 0.9f, 1.4f);
        break;
    }
    case SE_SM_LV_JETSWING_MOVE: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 500, 3000, 0.1f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 1600, 8000, 0.85f, 1.7f);
        break;
    }
    case SE_SM_LV_FOOFIGHTER_MOVE1: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 500, 2500, 0.5f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 500, 2500, 1.0f, 1.7f);
        break;
    }
    // level: speed in the high 16 bits, height in the low 16.
    case SE_SM_LV_FOOFIGHTER_MOVE2: {
        s32 speed = clampLevel(level >> 16, 100000);
        s32 height = clampLevel(level & 0xFFFF, 100000);
        volume = AudUtil::linearMinMax(speed, 500, 10000, 0.3f, 1.0f);
        pitch = AudUtil::linearMinMax(MR::max(height - speed / 10, 0), 5000, 11000, 1.0f, 0.5f);
        break;
    }
    case SE_OJ_LV_MAGIC_PNT_G_PULL: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, ::cMagicPntGVolMinPrm, ::cMagicPntGVolMaxPrm, ::cMagicPntGVolMax, ::cMagicPntGVolMin);
        if (level <= ::cMagicPntGPitMinPrm) {
            pitch = ::cMagicPntGPitMin;
        } else if (level >= ::cMagicPntGPitMaxPrm) {
            pitch = ::cMagicPntGPitMax;
        } else {
            f32 d = ::cMagicPntGPitMaxPrm - ::cMagicPntGPitMinPrm;
            f32 t = level - ::cMagicPntGPitMinPrm;
            pitch = (t * t) / (d * d) * (::cMagicPntGPitMax - ::cMagicPntGPitMin) + ::cMagicPntGPitMin;
        }
        break;
    }
    // level: pitch level in the high 16 bits, volume level in the low 16.
    case SE_OJ_LV_SPIDER_THREAD_PULL: {
        s32 pitchLvl = clampLevel(static_cast< s32 >(static_cast< u32 >(level) >> 16), 100000);
        s32 volLvl = clampLevel(level & 0xFFFF, 100000);
        volume = AudUtil::linearMinMax(volLvl, 10, 600, 0.0f, 1.0f);
        pitch = AudUtil::linearMinMax(pitchLvl, 10, 500, 1.0f, 1.5f);
        break;
    }
    case SE_SY_BLUECHIP_GET:
    case SE_SY_YELLOWCHIP_GET:
    case SE_OJ_BLUECHIP_COMPLETE:
    case SE_OJ_YELLOWCHIP_COMPLETE: {
        writePort(pHandle, 11, clampLevel(level, 12));
        break;
    }
    case SE_EM_LV_UNIZO_ROLL_GROUND: {
        level = clampLevel(level, 100000);
        if (level >= ::cUnizoRollGroundVolMinPrm) {
            volume = AudUtil::linearMinMax(level, ::cUnizoRollGroundVolMinPrm, ::cUnizoRollGroundVolMaxPrm, ::cUnizoRollVolMin, ::cUnizoRollVolMax);
        } else {
            volume = 0.0f;
        }
        pitch = AudUtil::linearMinMax(level, ::cUnizoRollPitMinPrm, ::cUnizoRollPitMaxPrm, ::cUnizoRollGroundPitMin, ::cUnizoRollGroundPitMax);
        break;
    }
    case SE_EM_LV_UNIZO_ROLL_SHALLOW:
    case SE_EM_LV_UNIZO_ROLL_WATER: {
        level = clampLevel(level, 100000);
        if (level >= ::cUnizoRollVolMinPrm) {
            volume = AudUtil::linearMinMax(level, ::cUnizoRollVolMinPrm, ::cUnizoRollVolMaxPrm, ::cUnizoRollVolMin, ::cUnizoRollVolMax);
        } else {
            volume = 0.0f;
        }
        pitch = AudUtil::linearMinMax(level, ::cUnizoRollPitMinPrm, ::cUnizoRollPitMaxPrm, ::cUnizoRollPitMin, ::cUnizoRollPitMax);
        break;
    }
    case SE_EM_LV_UNIZO_LAND_WATER:
    case SE_EM_LV_UNIZO_LAND: {
        level = clampLevel(level, 100000);
        if (level >= 50) {
            volume = AudUtil::linearMinMax(level, 50, 200, 0.3f, 1.0f);
        } else {
            volume = 0.0f;
        }
        pitch = AudUtil::linearMinMax(level, 5, 100, 0.9f, 1.0f);
        break;
    }
    case SE_EM_LV_UNIZO_COLLISION_WATER:
    case SE_EM_LV_UNIZO_COLLISION: {
        level = clampLevel(level, 100000);
        if (level >= 100) {
            volume = AudUtil::linearMinMax(level, 100, 800, 0.5f, 1.0f);
        } else {
            volume = 0.0f;
        }
        pitch = AudUtil::linearMinMax(level, 100, 800, 0.9f, 1.0f);
        break;
    }
    case SE_BM_LV_KAMECK_FLOAT: {
        level = clampLevel(level, 100000);
        volume = AudUtil::linearMinMax(level, 100, 2000, 0.3f, 1.0f);
        pitch = AudUtil::linearMinMax(level, 100, 2000, 0.6f, 1.0f);
        break;
    }
    case SE_EM_LV_TORPEDO_ALARM: {
        level = clampLevel(level, 100000);
        pitch = AudUtil::linearMinMax(level, ::cTorpedoPitMinPrm, ::cTorpedoPitMaxPrm, ::cTorpedoPitMin, ::cTorpedoPitMax);
        f32 wait = AudUtil::linearMinMax(level, ::cTorpedoWaitMinPrm, ::cTorpedoWaitMaxPrm, ::cTorpedoWaitMin, ::cTorpedoWaitMax);
        writePort(pHandle, 11, static_cast< s32 >(wait));
        break;
    }
    case SE_EM_BOMB_BOUND: {
        level = clampLevel(level, 10000);
        if (level >= 100) {
            volume = AudUtil::linearMinMax(level, ::cBombBoundVolMinPrm, ::cBombBoundVolMaxPrm, 0.6f, 1.0f);
        } else {
            volume = 0.0f;
        }
        break;
    }
    case SE_RS_LV_NOTE_TIMER_SLOW:
    case SE_RS_LV_NOTE_TIMER_MIDDLE:
    case SE_RS_LV_NOTE_TIMER_FAST:
    case SE_SM_LV_RABBIT_NEAR: {
        writePort(pHandle, 13, clampLevel(level, 127));
        break;
    }
    case SE_SM_LV_TICO_FLOAT:
    case SE_SM_LV_TICO_FLOAT_DEMO:
    case SE_EM_LV_STRSPIDER_SWING1:
    case SE_EM_LV_STRSPIDER_SWING2:
    case SE_BM_LV_BBEGO_ROT_NEEDLE:
    case SE_SM_LV_RABBIT_NEAR2:
    case SE_EM_STRSPIDER_COLLISION:
    case SE_EM_LV_WATERBAZ_STORM:
    case SE_BM_LV_ICEMERAKING_ROLL: {
        volume = clampLevel(level, 100) / 100.0f;
        break;
    }
    case SE_SM_STRAYTICO_GET:
    case SE_SM_LV_TICOFAT_EATING:
    case SE_SM_LV_TICOFAT_GLAD: {
        writePort(pHandle, 11, clampLevel(level, 127));
        break;
    }
    // subLevel: the effect send, percent.
    case SE_SV_LV_RABBIT_NEAR:
    case SE_SM_LV_RABBIT_RUS_LEAVES:
    case SE_SM_LV_RABBIT_RUS_HOLE:
    case SE_SV_LV_RABBIT_NEAR2: {
        volume = clampLevel(level, 100) / 100.0f;
        if (subLevel > 0) {
            pHandle->getSound()->getAuxiliary().moveFxMix(MR::min(subLevel, 100) / 100.0f, 0);
        }
        break;
    }
    case SE_EM_LV_STRSPIDER_SPIN: {
        volume = clampLevel(level, 100) / 100.0f;
        writePort(pHandle, 13, clampLevel(subLevel, 100));
        break;
    }
    case SE_SY_TRAMPLE_COMBO: {
        writePort(pHandle, 11, clampLevel(level, 6));
        break;
    }
    case SE_EM_LV_MAGKILLER_FLY: {
        pitch = clampLevel(level, 200) / 100.0f;
        break;
    }
    case SE_BM_BOSS_BUG_BOMB_GROUND: {
        volume = level / 100.0f;
        break;
    }
    case SE_BM_BOSS_BUG_BOMB_GROUND_WATER: {
        volume = level >= 50 ? level / 100.0f : 0.0f;
        break;
    }
    // subLevel: the wing beat written to port 10 (-1: 126).
    case SE_BM_LV_BOSS_BUG_FLY_BASE:
    case SE_BM_LV_BOSS_BUG_FLY_ADD:
    case SE_BM_LV_BOSS_BUG_FLY_ROTATE:
    case SE_BM_LV_BOSS_BUG_FLY_ROLL:
    case SE_BM_LV_BOSS_BUG_FLY_ALARM:
    case SE_BM_LV_BOSS_BUG_FLY_BASE2:
    case SE_BM_LV_BOSS_BUG_FLY_SHAKE:
    case SE_BM_LV_BOSS_BUG_FLY_SHAKE2: {
        volume = level / 1000.0f;
        writePort(pHandle, 10, subLevel == -1 ? 0x7F - 1 : subLevel);
        break;
    }
    case SE_SY_CTRL_GUIDE_CHANGE:
    case SE_SY_CTRL_GUIDE_CHANGE2:
    case SE_SM_LV_SURF_RAY_CURVE:
    case SE_SM_LV_SURF_RAY_MOVE0:
    case SE_SM_LV_SURF_RAY_MOVE1:
    case SE_SM_LV_SURF_RAY_MOVE2:
    case SE_SM_LV_SURF_RAY_MOVE3:
    case SE_SM_SURF_RAY_JUMPIN1:
    case SE_SM_SURF_RAY_JUMPOUT1:
    case SE_SM_SURF_RAY_MOVE_WIND:
    case SE_SM_SURF_RAY_JUMPIN0: {
        volume = level / 1000.0f;
        break;
    }
    case SE_SY_SURF_TUTORIAL_TIMER:
    case SE_SY_SURF_TUTORIAL_TIMER2:
    case SE_SY_SURF_TUTORIAL_TIMER3:
    case SE_SY_SURF_TUTORIAL_TIMER4:
    case SE_SY_SURF_TUTORIAL_GONEXT:
    case SE_SY_SURF_TUTORIAL_GONEXT2: {
        writePort(pHandle, 11, level);
        break;
    }
    default: {
        return;
    }
    }

    pHandle->getSound()->getAuxiliary().moveVolume(volume, steps);
    pHandle->getSound()->getAuxiliary().movePitch(pitch, steps);
}

// True: the start is dropped because a sound it would clash with is playing.
bool AudSoundObject::modifyLimitedSound(SoundID soundID) {
    f32 nearRange;

    switch (soundID) {
    case SE_SY_GCAPTURE_APPEAR: {
        if (isPlayingID(SE_SY_SPIN_DRIVER_APPEAR)) {
            stopSound(SE_SY_SPIN_DRIVER_APPEAR, 0);
        }
        return false;
    }
    case SE_SY_SPIN_DRIVER_APPEAR: {
        return isPlayingID(SE_SY_GCAPTURE_APPEAR);
    }
    case SE_SY_READ_RIDDLE_S: {
        return isPlayingID(SE_SY_QUESTION_COIN);
    }
    case SE_RS_LV_NOTE_TIMER_SLOW:
    case SE_RS_LV_NOTE_TIMER_MIDDLE:
    case SE_RS_LV_NOTE_TIMER_FAST: {
        return !isPlayingID(soundID) &&
               (isPlayingID(SE_RS_LV_NOTE_TIMER_SLOW) || isPlayingID(SE_RS_LV_NOTE_TIMER_MIDDLE) || isPlayingID(SE_RS_LV_NOTE_TIMER_FAST));
    }
    case SE_OJ_LV_HOPPER_CONVEYER_MV:
        nearRange = 600.0f;
        break;
    case SE_OJ_LV_M_KOOPA_CATER_S_MV:
        nearRange = 500.0f;
        break;
    case SE_OJ_LV_FIRE_BAR_CENTER:
    case SE_OJ_LV_MARINE_STEAM_OUT_S:
    case SE_OJ_LV_LAVA_GEYSER_SIGN:
    case SE_OJ_LAVA_GEYSER_SHOOT:
    case SE_OJ_LV_LAVA_GEYSER_KEEP:
        nearRange = 400.0f;
        break;
    case SE_OJ_LV_STAIR_BREAK:
    case SE_OJ_LV_LAVA_STEAM_OUT:
        nearRange = 350.0f;
        break;
    default:
        return false;
    }

    // One copy of these loops within nearRange is enough.
    return AudWrap::getSystem()->isAlreadyPlayingSoundNear(soundID, getPos(), nearRange);
}
