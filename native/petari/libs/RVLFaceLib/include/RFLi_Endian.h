#ifndef RVL_FACE_LIBRARY_INTERNAL_ENDIAN_H
#define RVL_FACE_LIBRARY_INTERNAL_ENDIAN_H
#include <revolution/types.h>

/**
 * Mii records (RFLiCharData, RFLiHiddenCharData, RFLiTableData and the database and controller headers around them)
 * keep the console's bytes in memory: NAND, the Wii Remote and the CRCs see them as they are. Their bitfields and
 * 16/32-bit fields are read and written through these. For CodeWarrior they are the plain member accesses.
 */
#ifdef TARGET_PC
/* Bit pos counts from the struct's first bit, MSB first in each big-endian 16-bit unit (the console's bitfield
 * order); no field crosses a unit. */
static inline u32 RFLiGetBits(const void* p, u32 pos, u32 len) {
    const u8* unit = (const u8*)p + (pos >> 4) * 2;
    const u32 word = (u32)unit[0] << 8 | unit[1];
    return (word >> (16 - (pos & 15) - len)) & ((1u << len) - 1);
}

static inline s32 RFLiGetSignedBits(const void* p, u32 pos, u32 len) {
    return (s32)(RFLiGetBits(p, pos, len) << (32 - len)) >> (32 - len);
}

static inline void RFLiSetBits(void* p, u32 pos, u32 len, u32 value) {
    u8* unit = (u8*)p + (pos >> 4) * 2;
    const u32 shift = 16 - (pos & 15) - len;
    const u32 mask = ((1u << len) - 1) << shift;
    const u32 word = (((u32)unit[0] << 8 | unit[1]) & ~mask) | ((value << shift) & mask);
    unit[0] = (u8)(word >> 8);
    unit[1] = (u8)word;
}

/* 32-bit words (floats in resources) to host order, in place. */
static inline void RFLiSwapWords(void* p, u32 num) {
    u32* words = (u32*)p;
    u32 i;
    for (i = 0; i < num; i++) {
        words[i] = __builtin_bswap32(words[i]);
    }
}

/* UTF-16 names: big-endian in records, host order in RFLiCharInfo (the swap is its own inverse). */
static inline void RFLiCopyName(wchar_t* dst, const wchar_t* src, u32 len) {
    u32 i;
    for (i = 0; i < len; i++) {
        dst[i] = (wchar_t)__builtin_bswap16((u16)src[i]);
    }
}

#define RFLI_GET(S, p, f) RFLiGetBits((p), RFLI_BITPOS_##S##_##f, RFLI_BITLEN_##S##_##f)
#define RFLI_GETS(S, p, f) RFLiGetSignedBits((p), RFLI_BITPOS_##S##_##f, RFLI_BITLEN_##S##_##f)
#define RFLI_SET(S, p, f, v) RFLiSetBits((p), RFLI_BITPOS_##S##_##f, RFLI_BITLEN_##S##_##f, (u32)(v))
#define RFLI_LD16(x) nebula_load_be16(&(x))
#define RFLI_LDS16(x) ((s16)nebula_load_be16(&(x)))
#define RFLI_LD32(x) nebula_load_be32(&(x))
#define RFLI_ST16(x, v) nebula_store_be16(&(x), (u16)(v))
#define RFLI_ST32(x, v) nebula_store_be32(&(x), (u32)(v))
#define RFLI_COPY_NAME(dst, src, len) RFLiCopyName((dst), (src), (len))

/* The console layouts' bit positions. */
#define RFLI_BITPOS_RFLiCharData_padding0 0
#define RFLI_BITLEN_RFLiCharData_padding0 1
#define RFLI_BITPOS_RFLiCharData_sex 1
#define RFLI_BITLEN_RFLiCharData_sex 1
#define RFLI_BITPOS_RFLiCharData_birthMonth 2
#define RFLI_BITLEN_RFLiCharData_birthMonth 4
#define RFLI_BITPOS_RFLiCharData_birthDay 6
#define RFLI_BITLEN_RFLiCharData_birthDay 5
#define RFLI_BITPOS_RFLiCharData_favoriteColor 11
#define RFLI_BITLEN_RFLiCharData_favoriteColor 4
#define RFLI_BITPOS_RFLiCharData_favorite 15
#define RFLI_BITLEN_RFLiCharData_favorite 1
#define RFLI_BITPOS_RFLiCharData_faceType 256
#define RFLI_BITLEN_RFLiCharData_faceType 3
#define RFLI_BITPOS_RFLiCharData_faceColor 259
#define RFLI_BITLEN_RFLiCharData_faceColor 3
#define RFLI_BITPOS_RFLiCharData_faceTex 262
#define RFLI_BITLEN_RFLiCharData_faceTex 4
#define RFLI_BITPOS_RFLiCharData_padding2 266
#define RFLI_BITLEN_RFLiCharData_padding2 3
#define RFLI_BITPOS_RFLiCharData_localonly 269
#define RFLI_BITLEN_RFLiCharData_localonly 1
#define RFLI_BITPOS_RFLiCharData_type 270
#define RFLI_BITLEN_RFLiCharData_type 2
#define RFLI_BITPOS_RFLiCharData_hairType 272
#define RFLI_BITLEN_RFLiCharData_hairType 7
#define RFLI_BITPOS_RFLiCharData_hairColor 279
#define RFLI_BITLEN_RFLiCharData_hairColor 3
#define RFLI_BITPOS_RFLiCharData_hairFlip 282
#define RFLI_BITLEN_RFLiCharData_hairFlip 1
#define RFLI_BITPOS_RFLiCharData_padding3 283
#define RFLI_BITLEN_RFLiCharData_padding3 5
#define RFLI_BITPOS_RFLiCharData_eyebrowType 288
#define RFLI_BITLEN_RFLiCharData_eyebrowType 5
#define RFLI_BITPOS_RFLiCharData_eyebrowRotate 293
#define RFLI_BITLEN_RFLiCharData_eyebrowRotate 5
#define RFLI_BITPOS_RFLiCharData_padding4 298
#define RFLI_BITLEN_RFLiCharData_padding4 6
#define RFLI_BITPOS_RFLiCharData_eyebrowColor 304
#define RFLI_BITLEN_RFLiCharData_eyebrowColor 3
#define RFLI_BITPOS_RFLiCharData_eyebrowScale 307
#define RFLI_BITLEN_RFLiCharData_eyebrowScale 4
#define RFLI_BITPOS_RFLiCharData_eyebrowY 311
#define RFLI_BITLEN_RFLiCharData_eyebrowY 5
#define RFLI_BITPOS_RFLiCharData_eyebrowX 316
#define RFLI_BITLEN_RFLiCharData_eyebrowX 4
#define RFLI_BITPOS_RFLiCharData_eyeType 320
#define RFLI_BITLEN_RFLiCharData_eyeType 6
#define RFLI_BITPOS_RFLiCharData_eyeRotate 326
#define RFLI_BITLEN_RFLiCharData_eyeRotate 5
#define RFLI_BITPOS_RFLiCharData_eyeY 331
#define RFLI_BITLEN_RFLiCharData_eyeY 5
#define RFLI_BITPOS_RFLiCharData_eyeColor 336
#define RFLI_BITLEN_RFLiCharData_eyeColor 3
#define RFLI_BITPOS_RFLiCharData_eyeScale 339
#define RFLI_BITLEN_RFLiCharData_eyeScale 4
#define RFLI_BITPOS_RFLiCharData_eyeX 343
#define RFLI_BITLEN_RFLiCharData_eyeX 4
#define RFLI_BITPOS_RFLiCharData_padding5 347
#define RFLI_BITLEN_RFLiCharData_padding5 5
#define RFLI_BITPOS_RFLiCharData_noseType 352
#define RFLI_BITLEN_RFLiCharData_noseType 4
#define RFLI_BITPOS_RFLiCharData_noseScale 356
#define RFLI_BITLEN_RFLiCharData_noseScale 4
#define RFLI_BITPOS_RFLiCharData_noseY 360
#define RFLI_BITLEN_RFLiCharData_noseY 5
#define RFLI_BITPOS_RFLiCharData_padding6 365
#define RFLI_BITLEN_RFLiCharData_padding6 3
#define RFLI_BITPOS_RFLiCharData_mouthType 368
#define RFLI_BITLEN_RFLiCharData_mouthType 5
#define RFLI_BITPOS_RFLiCharData_mouthColor 373
#define RFLI_BITLEN_RFLiCharData_mouthColor 2
#define RFLI_BITPOS_RFLiCharData_mouthScale 375
#define RFLI_BITLEN_RFLiCharData_mouthScale 4
#define RFLI_BITPOS_RFLiCharData_mouthY 379
#define RFLI_BITLEN_RFLiCharData_mouthY 5
#define RFLI_BITPOS_RFLiCharData_glassType 384
#define RFLI_BITLEN_RFLiCharData_glassType 4
#define RFLI_BITPOS_RFLiCharData_glassColor 388
#define RFLI_BITLEN_RFLiCharData_glassColor 3
#define RFLI_BITPOS_RFLiCharData_glassScale 391
#define RFLI_BITLEN_RFLiCharData_glassScale 4
#define RFLI_BITPOS_RFLiCharData_glassY 395
#define RFLI_BITLEN_RFLiCharData_glassY 5
#define RFLI_BITPOS_RFLiCharData_mustacheType 400
#define RFLI_BITLEN_RFLiCharData_mustacheType 2
#define RFLI_BITPOS_RFLiCharData_beardType 402
#define RFLI_BITLEN_RFLiCharData_beardType 2
#define RFLI_BITPOS_RFLiCharData_beardColor 404
#define RFLI_BITLEN_RFLiCharData_beardColor 3
#define RFLI_BITPOS_RFLiCharData_beardScale 407
#define RFLI_BITLEN_RFLiCharData_beardScale 4
#define RFLI_BITPOS_RFLiCharData_beardY 411
#define RFLI_BITLEN_RFLiCharData_beardY 5
#define RFLI_BITPOS_RFLiCharData_moleType 416
#define RFLI_BITLEN_RFLiCharData_moleType 1
#define RFLI_BITPOS_RFLiCharData_moleScale 417
#define RFLI_BITLEN_RFLiCharData_moleScale 4
#define RFLI_BITPOS_RFLiCharData_moleY 421
#define RFLI_BITLEN_RFLiCharData_moleY 5
#define RFLI_BITPOS_RFLiCharData_moleX 426
#define RFLI_BITLEN_RFLiCharData_moleX 5
#define RFLI_BITPOS_RFLiCharData_padding8 431
#define RFLI_BITLEN_RFLiCharData_padding8 1
#define RFLI_BITPOS_RFLiHiddenCharData_padding0 0
#define RFLI_BITLEN_RFLiHiddenCharData_padding0 1
#define RFLI_BITPOS_RFLiHiddenCharData_sex 1
#define RFLI_BITLEN_RFLiHiddenCharData_sex 1
#define RFLI_BITPOS_RFLiHiddenCharData_birthPadding 2
#define RFLI_BITLEN_RFLiHiddenCharData_birthPadding 9
#define RFLI_BITPOS_RFLiHiddenCharData_favoriteColor 11
#define RFLI_BITLEN_RFLiHiddenCharData_favoriteColor 4
#define RFLI_BITPOS_RFLiHiddenCharData_favorite 15
#define RFLI_BITLEN_RFLiHiddenCharData_favorite 1
#define RFLI_BITPOS_RFLiHiddenCharData_faceType 256
#define RFLI_BITLEN_RFLiHiddenCharData_faceType 3
#define RFLI_BITPOS_RFLiHiddenCharData_faceColor 259
#define RFLI_BITLEN_RFLiHiddenCharData_faceColor 3
#define RFLI_BITPOS_RFLiHiddenCharData_faceTex 262
#define RFLI_BITLEN_RFLiHiddenCharData_faceTex 4
#define RFLI_BITPOS_RFLiHiddenCharData_padding2 266
#define RFLI_BITLEN_RFLiHiddenCharData_padding2 3
#define RFLI_BITPOS_RFLiHiddenCharData_localonly 269
#define RFLI_BITLEN_RFLiHiddenCharData_localonly 1
#define RFLI_BITPOS_RFLiHiddenCharData_type 270
#define RFLI_BITLEN_RFLiHiddenCharData_type 2
#define RFLI_BITPOS_RFLiHiddenCharData_hairType 272
#define RFLI_BITLEN_RFLiHiddenCharData_hairType 7
#define RFLI_BITPOS_RFLiHiddenCharData_hairColor 279
#define RFLI_BITLEN_RFLiHiddenCharData_hairColor 3
#define RFLI_BITPOS_RFLiHiddenCharData_hairFlip 282
#define RFLI_BITLEN_RFLiHiddenCharData_hairFlip 1
#define RFLI_BITPOS_RFLiHiddenCharData_padding3 283
#define RFLI_BITLEN_RFLiHiddenCharData_padding3 5
#define RFLI_BITPOS_RFLiHiddenCharData_eyebrowType 288
#define RFLI_BITLEN_RFLiHiddenCharData_eyebrowType 5
#define RFLI_BITPOS_RFLiHiddenCharData_eyebrowRotate 293
#define RFLI_BITLEN_RFLiHiddenCharData_eyebrowRotate 5
#define RFLI_BITPOS_RFLiHiddenCharData_padding4 298
#define RFLI_BITLEN_RFLiHiddenCharData_padding4 6
#define RFLI_BITPOS_RFLiHiddenCharData_eyebrowColor 304
#define RFLI_BITLEN_RFLiHiddenCharData_eyebrowColor 3
#define RFLI_BITPOS_RFLiHiddenCharData_eyebrowScale 307
#define RFLI_BITLEN_RFLiHiddenCharData_eyebrowScale 4
#define RFLI_BITPOS_RFLiHiddenCharData_eyebrowY 311
#define RFLI_BITLEN_RFLiHiddenCharData_eyebrowY 5
#define RFLI_BITPOS_RFLiHiddenCharData_eyebrowX 316
#define RFLI_BITLEN_RFLiHiddenCharData_eyebrowX 4
#define RFLI_BITPOS_RFLiHiddenCharData_eyeType 320
#define RFLI_BITLEN_RFLiHiddenCharData_eyeType 6
#define RFLI_BITPOS_RFLiHiddenCharData_eyeRotate 326
#define RFLI_BITLEN_RFLiHiddenCharData_eyeRotate 5
#define RFLI_BITPOS_RFLiHiddenCharData_eyeY 331
#define RFLI_BITLEN_RFLiHiddenCharData_eyeY 5
#define RFLI_BITPOS_RFLiHiddenCharData_eyeColor 336
#define RFLI_BITLEN_RFLiHiddenCharData_eyeColor 3
#define RFLI_BITPOS_RFLiHiddenCharData_eyeScale 339
#define RFLI_BITLEN_RFLiHiddenCharData_eyeScale 4
#define RFLI_BITPOS_RFLiHiddenCharData_eyeX 343
#define RFLI_BITLEN_RFLiHiddenCharData_eyeX 4
#define RFLI_BITPOS_RFLiHiddenCharData_padding5 347
#define RFLI_BITLEN_RFLiHiddenCharData_padding5 5
#define RFLI_BITPOS_RFLiHiddenCharData_noseType 352
#define RFLI_BITLEN_RFLiHiddenCharData_noseType 4
#define RFLI_BITPOS_RFLiHiddenCharData_noseScale 356
#define RFLI_BITLEN_RFLiHiddenCharData_noseScale 4
#define RFLI_BITPOS_RFLiHiddenCharData_noseY 360
#define RFLI_BITLEN_RFLiHiddenCharData_noseY 5
#define RFLI_BITPOS_RFLiHiddenCharData_padding6 365
#define RFLI_BITLEN_RFLiHiddenCharData_padding6 3
#define RFLI_BITPOS_RFLiHiddenCharData_mouthType 368
#define RFLI_BITLEN_RFLiHiddenCharData_mouthType 5
#define RFLI_BITPOS_RFLiHiddenCharData_mouthColor 373
#define RFLI_BITLEN_RFLiHiddenCharData_mouthColor 2
#define RFLI_BITPOS_RFLiHiddenCharData_mouthScale 375
#define RFLI_BITLEN_RFLiHiddenCharData_mouthScale 4
#define RFLI_BITPOS_RFLiHiddenCharData_mouthY 379
#define RFLI_BITLEN_RFLiHiddenCharData_mouthY 5
#define RFLI_BITPOS_RFLiHiddenCharData_glassType 384
#define RFLI_BITLEN_RFLiHiddenCharData_glassType 4
#define RFLI_BITPOS_RFLiHiddenCharData_glassColor 388
#define RFLI_BITLEN_RFLiHiddenCharData_glassColor 3
#define RFLI_BITPOS_RFLiHiddenCharData_glassScale 391
#define RFLI_BITLEN_RFLiHiddenCharData_glassScale 4
#define RFLI_BITPOS_RFLiHiddenCharData_glassY 395
#define RFLI_BITLEN_RFLiHiddenCharData_glassY 5
#define RFLI_BITPOS_RFLiHiddenCharData_mustacheType 400
#define RFLI_BITLEN_RFLiHiddenCharData_mustacheType 2
#define RFLI_BITPOS_RFLiHiddenCharData_beardType 402
#define RFLI_BITLEN_RFLiHiddenCharData_beardType 2
#define RFLI_BITPOS_RFLiHiddenCharData_beardColor 404
#define RFLI_BITLEN_RFLiHiddenCharData_beardColor 3
#define RFLI_BITPOS_RFLiHiddenCharData_beardScale 407
#define RFLI_BITLEN_RFLiHiddenCharData_beardScale 4
#define RFLI_BITPOS_RFLiHiddenCharData_beardY 411
#define RFLI_BITLEN_RFLiHiddenCharData_beardY 5
#define RFLI_BITPOS_RFLiHiddenCharData_moleType 416
#define RFLI_BITLEN_RFLiHiddenCharData_moleType 1
#define RFLI_BITPOS_RFLiHiddenCharData_moleScale 417
#define RFLI_BITLEN_RFLiHiddenCharData_moleScale 4
#define RFLI_BITPOS_RFLiHiddenCharData_moleY 421
#define RFLI_BITLEN_RFLiHiddenCharData_moleY 5
#define RFLI_BITPOS_RFLiHiddenCharData_moleX 426
#define RFLI_BITLEN_RFLiHiddenCharData_moleX 5
#define RFLI_BITPOS_RFLiHiddenCharData_padding8 431
#define RFLI_BITLEN_RFLiHiddenCharData_padding8 1
#define RFLI_BITPOS_RFLiTableData_sex 64
#define RFLI_BITLEN_RFLiTableData_sex 1
#define RFLI_BITPOS_RFLiTableData_next 65
#define RFLI_BITLEN_RFLiTableData_next 15
#define RFLI_BITPOS_RFLiTableData_padding2 80
#define RFLI_BITLEN_RFLiTableData_padding2 1
#define RFLI_BITPOS_RFLiTableData_prev 81
#define RFLI_BITLEN_RFLiTableData_prev 15
#define RFLI_BITPOS_RFLiDatabase_isolation 59232
#define RFLI_BITLEN_RFLiDatabase_isolation 1
#define RFLI_BITPOS_RFLiDatabase_padding1 59233
#define RFLI_BITLEN_RFLiDatabase_padding1 31

#else
#define RFLI_GET(S, p, f) ((p)->f)
#define RFLI_GETS(S, p, f) ((p)->f)
#define RFLI_SET(S, p, f, v) ((p)->f = (v))
#define RFLI_LD16(x) (x)
#define RFLI_LDS16(x) (x)
#define RFLI_LD32(x) (x)
#define RFLI_ST16(x, v) ((x) = (v))
#define RFLI_ST32(x, v) ((x) = (v))
#define RFLI_COPY_NAME(dst, src, len) memcpy((dst), (src), (len) * sizeof(wchar_t))
#endif

#endif
