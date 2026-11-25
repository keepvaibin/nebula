// SPDX-License-Identifier: GPL-3.0-only
// A note's sends as typed records (Game/Sound/SoundSend.hpp, P-221): for every mix word the cook types, the record gives
// the same bus in every output mode and the same gain, to the bit, as the packed word gave when the game decoded it at
// every update. The word's record is the cook's rule (crates/nebula-import/src/cook/sound.rs send_record), which
// sound_tests.rs every_mix_word_is_typed_or_refused proves for every word.
#include "Game/Sound/SoundSend.hpp"
#include "Game/Sound/SeqTrack.hpp"

#include <stdio.h>
#include <string.h>

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat, u32 word) {
        if (!ok) {
            printf("FAIL %s (word 0x%04x)\n", pWhat, static_cast< unsigned >(word));
            sFailures++;
        }
    }

    bool sameBits(f32 a, f32 b) {
        return memcmp(&a, &b, sizeof(a)) == 0;
    }

    // The cook's record of a word, or false for a word it refuses.
    bool record(u32 word, SoundSend* pSend) {
        if (word == 0xffff) {
            *pSend = {NEBULA_AUDIO_BUS_NONE, {SEND_CURVE_NONE, SEND_CURVE_NONE}, 1};
            return true;
        }
        const u32 bus = word >> 8;
        const u32 first = (word >> 4) & 0xf;
        const u32 second = word & 0xf;
        const auto known = [](u32 curve) { return curve <= 3 || (curve >= 5 && curve <= 7); };
        if (bus >= 12 || !known(first) || !known(second)) {
            return false;
        }
        *pSend = {static_cast< u8 >(bus), {static_cast< u8 >(first), static_cast< u8 >(second)}, 0};
        return true;
    }

    // The game's bus of a word before P-221 (SoundVoices.cpp initialUpdate).
    u32 oldBus(u32 word, u32 outputMode) {
        u8 bus = static_cast< u8 >(word >> 8);
        if (outputMode == 0) {
            switch (bus) {
            case 8:
                bus = 11;
                break;
            case 9:
                bus = 2;
                break;
            }
        } else if (outputMode == 1 && bus == 8) {
            bus = 11;
        }
        return bus < NEBULA_AUDIO_BUSES ? bus : static_cast< u32 >(NEBULA_AUDIO_BUS_NONE);
    }

    f32 clamp01(f32 x) {
        if (x <= 0.0f) {
            return 0.0f;
        }
        if (x >= 1.0f) {
            return 1.0f;
        }
        return x;
    }

    // The game's gain of a word before P-221 (SoundVoices.cpp mixerVolumes, before the note's gain).
    f32 oldGain(u32 config, f32 volumeIn, f32 pan, f32 fxmix, f32 dolby, bool flat) {
        f32 volume = volumeIn;
        const u8 bus = static_cast< u8 >(config >> 8);
        const u8 mode0 = (config >> 4) & 0xf;
        const u8 mode1 = config & 0xf;
        if (bus == 0) {
            return 0.0f;
        }
        f32 scale = 0.0f;
        if (mode0 != 0) {
            switch (mode0) {
            case 1:
                scale = pan;
                break;
            case 2:
                scale = fxmix;
                break;
            case 3:
                scale = dolby;
                break;
            case 5:
                scale = 1.0f - pan;
                break;
            case 6:
                scale = 1.0f - fxmix;
                break;
            case 7:
                scale = 1.0f - dolby;
                break;
            }
            switch (mode0) {
            case 2:
            case 6:
                volume *= scale;
                break;
            default:
                volume *= flat ? scale : Angle::sinRadian(scale * FloatUtil::PI() * 0.5f);
                break;
            }
        }
        if (mode1 != 0) {
            switch (mode1) {
            case 1:
                scale = pan;
                break;
            case 2:
                scale = fxmix;
                break;
            case 3:
                scale = dolby;
                break;
            case 5:
                scale = 1.0f - pan;
                break;
            case 6:
                scale = 1.0f - fxmix;
                break;
            case 7:
                scale = 1.0f - dolby;
                break;
            }
            switch (mode1) {
            case 3:
            case 7:
                volume *= Angle::sinRadian((scale * 0.34776f + 0.32612f) * FloatUtil::PI() * 0.5f);
                break;
            case 2:
            case 6:
                volume *= scale;
                break;
            default:
                volume *= flat ? scale : Angle::sinRadian(scale * FloatUtil::PI() * 0.5f);
                break;
            }
        }
        return clamp01(volume);
    }

    bool sameSend(const SoundSend& a, const SoundSend& b) {
        return a.mBus == b.mBus && a.mCurve[0] == b.mCurve[0] && a.mCurve[1] == b.mCurve[1] && a.mPanner == b.mPanner;
    }

    void testEveryWord() {
        const f32 volumes[] = {0.0f, 0.25f, 0.5f, 0.8f, 1.0f, 1.7f};
        const f32 values[] = {0.0f, 0.1f, 0.25f, 0.5f, 0.7071f, 0.9f, 1.0f};
        u32 typed = 0;
        for (u32 word = 0; word <= 0xffff; word++) {
            SoundSend send;
            if (!record(word, &send)) {
                continue;
            }
            typed++;
            for (u32 mode = 0; mode < 4; mode++) {
                expect(SoundSends::busFor(send, mode) == oldBus(word, mode), "the bus of every output mode", word);
            }
            if (send.mPanner != 0) {
                // A note on the panner has no send gains (effectorParams); its send plays on no bus.
                expect(SoundSends::busFor(send, 2) == NEBULA_AUDIO_BUS_NONE, "the panner's send on no bus", word);
                continue;
            }
            bool same = true;
            for (int flat = 0; flat < 2 && same; flat++) {
                for (f32 volume : volumes) {
                    for (f32 pan : values) {
                        for (f32 fxmix : values) {
                            for (f32 dolby : values) {
                                const f32 now = SoundSends::gain(send, volume, pan, fxmix, dolby, flat != 0);
                                same = same && sameBits(now, oldGain(word, volume, pan, fxmix, dolby, flat != 0));
                            }
                        }
                    }
                }
            }
            expect(same, "the gain of every volume, pan, effect send and rear amount", word);
        }
        expect(typed == 12 * 7 * 7 + 1, "every bus and known curve, and the panner word", typed);
    }

    void testDefaults() {
        const u32 notes[6] = {0x150, 0x210, 0x352, 0x412, 0, 0};
        const u32 streams[6] = {0xffff, 0, 0, 0, 0, 0};
        for (u32 i = 0; i < 6; i++) {
            SoundSend send;
            expect(record(notes[i], &send) && sameSend(send, SoundSends::cDefault[i]), "a note's default send", notes[i]);
            expect(record(streams[i], &send) && sameSend(send, SoundSends::cPanner[i]), "a stream's send", streams[i]);
        }
        SoundNote note;
        expect(!note.isPannerMode() && sameSend(note.mSends[2], SoundSends::cDefault[2]), "a new note's sends", 0x352);
    }

    // The filter values as floats are the DSP's 16-bit fractions, to the bit.
    void testFilterValues() {
        expect(sameBits(VoiceFilter::cPass, 32767.0f / 32768.0f), "the pass weight", 0x7fff);
        const s16 rows[3][4] = {{0x0F5C, 0x0A3D, 0x4665, 0x1E73}, {0x7FFF, 0, 0, 0}, {0x4330, 0, 0, 0}};
        const u32 at[3] = {0, 127, 128};
        for (u32 r = 0; r < 3; r++) {
            for (u32 i = 0; i < 4; i++) {
                expect(sameBits(cIIRCutoff[at[r]][i], rows[r][i] / 32768.0f), "an IIR cutoff row", at[r]);
            }
        }
    }
}  // namespace

int main() {
    testEveryWord();
    testDefaults();
    testFilterValues();
    if (sFailures != 0) {
        printf("%d failure(s)\n", sFailures);
        return 1;
    }
    printf("sound send tests passed\n");
    return 0;
}
