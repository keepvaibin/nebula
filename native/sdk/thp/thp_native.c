/* THP movies natively (in place of the SDK's THPDec.c, paired-single assembly, and THPAudio.c).
   - Video: each frame's JPEG data goes to Nebula's native decoder in the host (thp_video_decode), which writes the
     Y, U and V planes into the game's buffers before the renderer reads them.
   - Audio: THPAudio.c's ADPCM decoder, reading the frame's big-endian record header. */
#include <string.h>

#include <revolution/thp.h>

#include <nebula_endian.h>
#include <nebula_game_api.h>

const NebulaHostServices* nebula_host(void);

BOOL THPInit(void) {
    return TRUE;
}

/* The frame's video component and its size (from the frame header, as the game's player reads it). */
s32 __NebulaTHPVideoDecode(void* file, u32 size, void* tileY, void* tileU, void* tileV) {
    return nebula_host()->thp_video_decode(nebula_host()->user, file, size, tileY, tileU, tileV);
}

/* Without the size the frame cannot be bounded (see __NebulaTHPVideoDecode). */
s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work) {
    (void)file;
    (void)tileY;
    (void)tileU;
    (void)tileV;
    (void)work;
    return -1;
}

typedef struct {
    const u8* data;
    u32 nibbles;
    u8 predictor;
    u8 scale;
} Decoder;

static void begin(Decoder* d, const u8* p) {
    d->data = p;
    d->nibbles = 2;
    d->predictor = (u8)((*d->data & 0x70) >> 4);
    d->scale = (u8)(*d->data & 0xF);
    d->data++;
}

static s32 next_sample(Decoder* d) {
    s32 sample;
    if (!(d->nibbles & 0x0f)) {
        d->predictor = (u8)((*d->data & 0x70) >> 4);
        d->scale = (u8)(*d->data & 0xF);
        d->data++;
        d->nibbles += 2;
    }
    if (d->nibbles & 0x1) {
        sample = (s32)((u32)(*d->data & 0xF) << 28) >> 28;
        d->data++;
    } else {
        sample = (s32)((u32)(*d->data & 0xF0) << 24) >> 28;
    }
    d->nibbles++;
    return sample;
}

/* One channel's samples into dst every `step` samples (and into dst2 too for a mono frame). */
static void decode_channel(const u8* data, const s16 coef[8][2], s16 yn1, s16 yn2, u32 count, s16* dst, s16* dst2,
                           s32 step) {
    Decoder d;
    begin(&d, data);
    for (u32 i = 0; i < count; i++) {
        const s32 sample = next_sample(&d);
        s64 yn = (s64)coef[d.predictor][1] * yn2;
        yn += (s64)coef[d.predictor][0] * yn1;
        yn += (s64)(s32)((u32)sample << d.scale << 11);
        yn <<= 5;
        yn += 0x8000;
        if (yn > 2147483647LL) {
            yn = 2147483647LL;
        }
        if (yn < -2147483648LL) {
            yn = -2147483648LL;
        }
        const s16 out = (s16)(yn >> 16);
        *dst = out;
        dst += step;
        if (dst2 != NULL) {
            *dst2 = out;
            dst2 += step;
        }
        yn2 = yn1;
        yn1 = out;
    }
}

/* The record header: offsetNextChannel, sampleSize, then the left and right coefficients and histories, all
   big-endian (offsets 0, 4, 8, 0x28, 0x48..0x4E); the encoded data follows at 0x50. */
u32 THPAudioDecode(s16* audioBuffer, u8* audioFrame, s32 flag) {
    if (audioBuffer == NULL || audioFrame == NULL) {
        return 0;
    }
    const u32 offsetNextChannel = nebula_load_be32(audioFrame + 0);
    const u32 sampleSize = nebula_load_be32(audioFrame + 4);
    s16 lCoef[8][2], rCoef[8][2];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 2; j++) {
            lCoef[i][j] = (s16)nebula_load_be16(audioFrame + 8 + (i * 2 + j) * 2);
            rCoef[i][j] = (s16)nebula_load_be16(audioFrame + 0x28 + (i * 2 + j) * 2);
        }
    }
    const s16 lYn1 = (s16)nebula_load_be16(audioFrame + 0x48), lYn2 = (s16)nebula_load_be16(audioFrame + 0x4A);
    const s16 rYn1 = (s16)nebula_load_be16(audioFrame + 0x4C), rYn2 = (s16)nebula_load_be16(audioFrame + 0x4E);
    const u8* left = audioFrame + sizeof(THPAudioRecordHeader);
    const u8* right = left + offsetNextChannel;

    s16 *decLeft, *decRight;
    s32 step;
    if (flag == 1) {
        decRight = audioBuffer;
        decLeft = audioBuffer + sampleSize;
        step = 1;
    } else {
        decRight = audioBuffer;
        decLeft = audioBuffer + 1;
        step = 2;
    }

    if (offsetNextChannel == 0) {
        decode_channel(left, (const s16(*)[2])lCoef, lYn1, lYn2, sampleSize, decLeft, decRight, step);
    } else {
        decode_channel(left, (const s16(*)[2])lCoef, lYn1, lYn2, sampleSize, decLeft, NULL, step);
        decode_channel(right, (const s16(*)[2])rCoef, rYn1, rYn2, sampleSize, decRight, NULL, step);
    }
    return sampleSize;
}
