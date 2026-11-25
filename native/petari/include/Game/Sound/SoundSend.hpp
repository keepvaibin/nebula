#pragma once

#include <nebula_game_api.h>
#include <nebula_geometry.hpp>
#include <nebula_types.h>

// A note's six sends as the cook types them (crates/nebula-import/src/cook/sound.rs send_record): each feeds one of the
// mixer's buses with a gain that follows up to two curves of the note's pan, effect send and rear amount. The bus is
// named as the surround output hears it; the mono and stereo outputs move some buses elsewhere (busFor).

// The value a curve follows: none, pan, effect send, rear; the last three reversed (1 - x).
enum SendCurve : u8 {
    SEND_CURVE_NONE = 0,
    SEND_CURVE_PAN = 1,
    SEND_CURVE_FX_MIX = 2,
    SEND_CURVE_DOLBY = 3,
    SEND_CURVE_PAN_REVERSED = 5,
    SEND_CURVE_FX_MIX_REVERSED = 6,
    SEND_CURVE_DOLBY_REVERSED = 7,
};

struct SoundSend {
    u8 mBus;        // NEBULA_AUDIO_BUS_*
    u8 mCurve[2];   // SendCurve
    // On the first send: the note plays on the surround panner instead of its sends.
    u8 mPanner;
};

namespace SoundSends {
    // A note's sends until a program sets them: left and right by the pan, effect lines 0 and 1 by the pan and the
    // effect send.
    const SoundSend cDefault[6] = {
        {NEBULA_AUDIO_BUS_LEFT, {SEND_CURVE_PAN_REVERSED, SEND_CURVE_NONE}, 0},
        {NEBULA_AUDIO_BUS_RIGHT, {SEND_CURVE_PAN, SEND_CURVE_NONE}, 0},
        {NEBULA_AUDIO_BUS_EFFECT0, {SEND_CURVE_PAN_REVERSED, SEND_CURVE_FX_MIX}, 0},
        {NEBULA_AUDIO_BUS_EFFECT1, {SEND_CURVE_PAN, SEND_CURVE_FX_MIX}, 0},
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 0},
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 0},
    };
    // A stream's channels: the surround panner.
    const SoundSend cPanner[6] = {
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 1},
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 0},
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 0},
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 0},
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 0},
        {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 0},
    };

    // The bus each output mode (SoundEngine::outputMode: 0 mono, 1 stereo, 2 surround) plays a send's bus on: mono
    // puts surround left on the centre and the inverted right on the right, stereo surround left on the centre.
    const u8 cBusMap[3][NEBULA_AUDIO_BUSES] = {
        {NEBULA_AUDIO_BUS_NONE, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, NEBULA_AUDIO_BUS_EFFECT0, NEBULA_AUDIO_BUS_EFFECT1,
         NEBULA_AUDIO_BUS_EFFECT2, NEBULA_AUDIO_BUS_EFFECT3, NEBULA_AUDIO_BUS_SURROUND_RIGHT, NEBULA_AUDIO_BUS_CENTER,
         NEBULA_AUDIO_BUS_RIGHT, NEBULA_AUDIO_BUS_DISCARD, NEBULA_AUDIO_BUS_CENTER},
        {NEBULA_AUDIO_BUS_NONE, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, NEBULA_AUDIO_BUS_EFFECT0, NEBULA_AUDIO_BUS_EFFECT1,
         NEBULA_AUDIO_BUS_EFFECT2, NEBULA_AUDIO_BUS_EFFECT3, NEBULA_AUDIO_BUS_SURROUND_RIGHT, NEBULA_AUDIO_BUS_CENTER,
         NEBULA_AUDIO_BUS_RIGHT_INVERTED, NEBULA_AUDIO_BUS_DISCARD, NEBULA_AUDIO_BUS_CENTER},
        {NEBULA_AUDIO_BUS_NONE, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, NEBULA_AUDIO_BUS_EFFECT0, NEBULA_AUDIO_BUS_EFFECT1,
         NEBULA_AUDIO_BUS_EFFECT2, NEBULA_AUDIO_BUS_EFFECT3, NEBULA_AUDIO_BUS_SURROUND_RIGHT, NEBULA_AUDIO_BUS_SURROUND_LEFT,
         NEBULA_AUDIO_BUS_RIGHT_INVERTED, NEBULA_AUDIO_BUS_DISCARD, NEBULA_AUDIO_BUS_CENTER},
    };

    // The bus a send plays on in an output mode; a mode past surround plays every bus as surround does.
    inline u32 busFor(const SoundSend& rSend, u32 outputMode) {
        if (rSend.mBus >= NEBULA_AUDIO_BUSES) {
            return NEBULA_AUDIO_BUS_NONE;
        }
        return cBusMap[outputMode < 2 ? outputMode : 2][rSend.mBus];
    }

    inline f32 curveValue(u8 curve, f32 pan, f32 fxmix, f32 dolby) {
        switch (curve) {
        case SEND_CURVE_PAN:
            return pan;
        case SEND_CURVE_FX_MIX:
            return fxmix;
        case SEND_CURVE_DOLBY:
            return dolby;
        case SEND_CURVE_PAN_REVERSED:
            return 1.0f - pan;
        case SEND_CURVE_FX_MIX_REVERSED:
            return 1.0f - fxmix;
        case SEND_CURVE_DOLBY_REVERSED:
            return 1.0f - dolby;
        default:
            return 0.0f;
        }
    }

    inline bool isFxMix(u8 curve) {
        return curve == SEND_CURVE_FX_MIX || curve == SEND_CURVE_FX_MIX_REVERSED;
    }

    // The sends carry a rear amount: one feeds a surround bus or the inverted right (the two-channel matrix's rear), or
    // follows the rear curve. Where the output has speakers of their own such a note is placed by the panner's law instead
    // (NEBULA_VOICE_PLACED, the surround law's point 3).
    inline bool carriesRear(const SoundSend* pSends) {
        for (u32 i = 0; i < 6; i++) {
            const SoundSend& rSend = pSends[i];
            if (rSend.mBus == NEBULA_AUDIO_BUS_SURROUND_LEFT || rSend.mBus == NEBULA_AUDIO_BUS_SURROUND_RIGHT ||
                rSend.mBus == NEBULA_AUDIO_BUS_RIGHT_INVERTED) {
                return true;
            }
            for (u32 k = 0; k < 2; k++) {
                if (rSend.mCurve[k] == SEND_CURVE_DOLBY || rSend.mCurve[k] == SEND_CURVE_DOLBY_REVERSED) {
                    return true;
                }
            }
        }
        return false;
    }

    // A send's gain from the note's volume, pan, effect send and rear amount, before the note's gain: the effect send
    // linear, the rest by a quarter sine (linear in mono, `flat`); a rear amount as the second curve by the matrix's rear
    // curve. 0 for a send to no bus.
    inline f32 gain(const SoundSend& rSend, f32 volume, f32 pan, f32 fxmix, f32 dolby, bool flat) {
        if (rSend.mBus == NEBULA_AUDIO_BUS_NONE) {
            return 0.0f;
        }
        const u8 first = rSend.mCurve[0];
        if (first != SEND_CURVE_NONE) {
            const f32 scale = curveValue(first, pan, fxmix, dolby);
            if (isFxMix(first)) {
                volume *= scale;
            } else {
                volume *= flat ? scale : Angle::sinRadian(scale * FloatUtil::PI() * 0.5f);
            }
        }
        const u8 second = rSend.mCurve[1];
        if (second != SEND_CURVE_NONE) {
            const f32 scale = curveValue(second, pan, fxmix, dolby);
            if (second == SEND_CURVE_DOLBY || second == SEND_CURVE_DOLBY_REVERSED) {
                volume *= Angle::sinRadian((scale * 0.34776f + 0.32612f) * FloatUtil::PI() * 0.5f);
            } else if (isFxMix(second)) {
                volume *= scale;
            } else {
                volume *= flat ? scale : Angle::sinRadian(scale * FloatUtil::PI() * 0.5f);
            }
        }
        if (volume <= 0.0f) {
            return 0.0f;
        }
        if (volume >= 1.0f) {
            return 1.0f;
        }
        return volume;
    }
}  // namespace SoundSends
