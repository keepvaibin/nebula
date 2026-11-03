#ifndef RVL_FACE_LIBRARY_INTERNAL_CONFIG_H
#define RVL_FACE_LIBRARY_INTERNAL_CONFIG_H
#include "MiiInternalTypes.h"
#include <NebulaGpu.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Char data constraints
 */

#define MII_I_MAX_FACELINE_TYPE 7
#define MII_I_MAX_FACELINE_COLOR 5
#define MII_I_MAX_FACELINE_TEXTURE 11

#define MII_I_MAX_HAIR_TYPE 71
#define MII_I_MAX_HAIR_COLOR 7

#define MII_I_MAX_EYE_TYPE 47
#define MII_I_MAX_EYE_COLOR 5
#define MII_I_MAX_EYE_SCALE 7
#define MII_I_MAX_EYE_ROTATE 7
#define MII_I_MAX_EYE_X 12
#define MII_I_MAX_EYE_Y 18

#define MII_I_MAX_EYEBROW_TYPE 23
#define MII_I_MAX_EYEBROW_COLOR 7
#define MII_I_MAX_EYEBROW_SCALE 8
#define MII_I_MAX_EYEBROW_ROTATE 11
#define MII_I_MAX_EYEBROW_X 12
#define MII_I_MIN_EYEBROW_Y 3
#define MII_I_MAX_EYEBROW_Y 18

#define MII_I_MAX_NOSE_TYPE 11
#define MII_I_MAX_NOSE_SCALE 8
#define MII_I_MAX_NOSE_Y 18

#define MII_I_MAX_MOUTH_TYPE 23
#define MII_I_MAX_MOUTH_COLOR 2
#define MII_I_MAX_MOUTH_SCALE 8
#define MII_I_MAX_MOUTH_Y 18

#define MII_I_MAX_BEARD_MUSTACHE 3
#define MII_I_MAX_BEARD_TYPE 3
#define MII_I_MAX_BEARD_COLOR 7
#define MII_I_MAX_BEARD_SCALE 8
#define MII_I_MAX_BEARD_Y 16

#define MII_I_MAX_GLASS_TYPE 8
#define MII_I_MAX_GLASS_COLOR 5
#define MII_I_MAX_GLASS_SCALE 7
#define MII_I_MAX_GLASS_Y 20

#define MII_I_MAX_MOLE_TYPE 1
#define MII_I_MAX_MOLE_SCALE 8
#define MII_I_MAX_MOLE_X 16
#define MII_I_MAX_MOLE_Y 30

#define MII_I_MAX_BODY_HEIGHT 128
#define MII_I_MAX_BODY_BUILD 128

/**
 * Create ID
 */

#define MII_I_CREATE_ID_MASK_TEMPORARY 0x20000000
#define MII_I_CREATE_ID_MASK_NOT_SPECIAL (1u << 31)

/**
 * Face texture
 */

// TODO: A few values need to be converted to fractions/expressions
// Once MII_I_TEX_SCALE_Y becomes a fraction, replace the 1.1600001 with 1.16

#define MII_I_TEX_SCALE_X (0.88961464f)
#define MII_I_TEX_SCALE_Y (0.9276675f)

#define MII_I_TEX_SCALE2DIM(scale) (1.0 + 0.4f * (scale))
#define MII_I_TEX_ROTATE2ANG(rotate) ((360.0f / 32.0f) * ((rotate) % 32))
#define MII_I_TEX_UNIT(x) ((x) / 64.0f)

#define MII_I_TEX_EYE_BASE_X MII_I_TEX_UNIT(0)
#define MII_I_TEX_EYE_BASE_Y (18.451525f)
#define MII_I_TEX_EYE_BASE_W MII_I_TEX_UNIT(342)
#define MII_I_TEX_EYE_BASE_H MII_I_TEX_UNIT(288)

#define MII_I_TEX_EYEBROW_BASE_X MII_I_TEX_UNIT(0)
#define MII_I_TEX_EYEBROW_BASE_Y (16.549807f)
#define MII_I_TEX_EYEBROW_BASE_W MII_I_TEX_UNIT(324)
#define MII_I_TEX_EYEBROW_BASE_H MII_I_TEX_UNIT(288)

#define MII_I_TEX_MOUTH_BASE_Y (29.25885f)
#define MII_I_TEX_MOUTH_BASE_W MII_I_TEX_UNIT(396)
#define MII_I_TEX_MOUTH_BASE_H MII_I_TEX_UNIT(288)

#define MII_I_TEX_MUSTACHE_BASE_Y (31.763554f)
#define MII_I_TEX_MUSTACHE_BASE_W MII_I_TEX_UNIT(288)
#define MII_I_TEX_MUSTACHE_BASE_H MII_I_TEX_UNIT(576)

#define MII_I_TEX_MOLE_BASE_X (17.766165f)
#define MII_I_TEX_MOLE_BASE_Y (17.95986f)
#define MII_I_TEX_MOLE_BASE_W MII_I_TEX_UNIT(0)
#define MII_I_TEX_MOLE_BASE_H MII_I_TEX_UNIT(0)

/**
 * Tables
 */

// Two unused eye types?
static const u8 MII_I_EYE_ROT_OFFSET[MII_I_MAX_EYE_TYPE + 2 + 1] = {
    29, 28, 28, 28, 29, 28, 28, 28, 29, 28, 28, 28, 28, 29, 29, 28, 28,
    28, 29, 29, 28, 29, 28, 29, 29, 28, 29, 28, 28, 29, 28, 28, 28, 29,
    29, 29, 28, 28, 29, 29, 29, 28, 28, 29, 29, 29, 29, 29, 28, 28};

static const u8 MII_I_EYEBROW_ROT_OFFSET[MII_I_MAX_EYEBROW_TYPE + 1] = {
    26, 26, 27, 25, 26, 25, 26, 25, 28, 25, 26, 24,
    27, 27, 26, 26, 25, 25, 26, 26, 27, 26, 25, 27};

/**
 * Colors for the stages
 */
static const NebulaColor8 MII_I_MOLE_COLOR0 = {18, 15, 15, 255};

static const NebulaColor8 MII_I_EYE_COLOR1[MII_I_MAX_EYE_COLOR + 1] = {
    {0, 0, 0, 255},      {124, 128, 128, 255}, {112, 80, 64, 255},
    {112, 110, 64, 255}, {88, 104, 184, 255},  {72, 128, 104, 255}};

static const NebulaColor8 MII_I_EYEBROW_COLOR0[MII_I_MAX_EYEBROW_COLOR + 1] = {
    {30, 26, 24, 255},  {56, 32, 21, 255},    {85, 38, 23, 255},
    {112, 64, 36, 255}, {114, 114, 120, 255}, {73, 54, 26, 255},
    {122, 89, 40, 255}, {193, 159, 100, 255}};

static const NebulaColor8 MII_I_MOUTH_COLOR0[MII_I_MAX_MOUTH_COLOR + 1] = {
    {190, 78, 38, 255}, {216, 48, 40, 255}, {207, 68, 71, 255}};
static const NebulaColor8 MII_I_MOUTH_COLOR1[MII_I_MAX_MOUTH_COLOR + 1] = {
    {113, 42, 4, 255}, {120, 21, 16, 255}, {126, 37, 40, 255}};

static const NebulaColor8 MII_I_BEARD_COLOR0[MII_I_MAX_BEARD_COLOR + 1] = {
    {30, 26, 24, 255},  {56, 32, 21, 255},    {85, 38, 23, 255},
    {112, 64, 36, 255}, {114, 114, 120, 255}, {73, 54, 26, 255},
    {122, 89, 40, 255}, {193, 159, 100, 255}};

#ifdef __cplusplus
}
#endif
#endif
