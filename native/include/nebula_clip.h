/* SPDX-License-Identifier: GPL-3.0-only */
/* An animation as the store keeps it: one flat block in host order, used in place. Every *_at is a byte offset from the
   block's start. crates/nebula-import/src/cook/clip.rs writes this layout. */
#pragma once

#include <stdint.h>
#include "nebula_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEBULA_CLIP_MAGIC 0x504C434Eu /* "NCLP" */
#define NEBULA_CLIP_VERSION 2u

/* Tracks per entry: joints 9 (x scale, rotation, translation; then y; then z), texture matrices 5 (scale x, y, rotation,
   translation x, y), colours and registers 4 (r, g, b, a), pattern and visibility 1. */
enum {
    NEBULA_CLIP_JOINTS = 0,
    NEBULA_CLIP_TEXTURE_MATRICES = 1,
    NEBULA_CLIP_COLORS = 2,
    NEBULA_CLIP_REGISTERS = 3, /* first_count colour registers, then second_count constant registers */
    NEBULA_CLIP_PATTERN = 4,
    NEBULA_CLIP_VISIBILITY = 5,
    NEBULA_CLIP_UNPLAYED = 6, /* cluster and vertex colour files: the game has no player for them */
};

typedef struct NebulaClipHeader {
    uint32_t magic, version, size;
    uint8_t kind, loop_mode, reserved, coord_mode;
    int32_t frames;
    uint32_t first_count, second_count;
    uint32_t tracks_at, targets_at, values_at, value_count;
} NebulaClipHeader;

/* count 0: the kind's rest value. count 1: values[at]. Otherwise by stride: 1 steps at the frame's whole part, 2 steps at
   the nearest frame, 3 and 4 hermite keys (time, value, tangent; time, value, in tangent, out tangent). integer_keys selects
   the authored Hermite arithmetic order; values and tangents remain floats, including converted radians. */
enum {
    NEBULA_TRACK_STEP = 1,
    NEBULA_TRACK_STEP_NEAREST = 2,
    NEBULA_TRACK_KEYS = 3,
    NEBULA_TRACK_KEYS_SPLIT = 4,
};

typedef struct NebulaClipTrack {
    uint32_t count;
    uint8_t stride, integer_keys;
    uint8_t reserved[2];
    uint32_t at;
} NebulaClipTrack;

/* Pattern pools contain uint32_t indices; other clip pools contain floats.
   Pattern tracks step between indices, preserving every bit of a texture ID. */
static inline uint32_t nebula_clip_pattern_value(const NebulaClipHeader* clip, const NebulaClipTrack* track, float frame) {
    if (track->count == 0u) return 0u;
    const uint32_t* indices = (const uint32_t*)((const uint8_t*)clip + clip->values_at) + track->at;
    if (!(frame >= 0.0f) || track->count == 1u) return indices[0];
    if (track->stride == NEBULA_TRACK_STEP_NEAREST) frame += 0.5f;
    return frame >= (float)track->count ? indices[track->count - 1u] : indices[(uint32_t)frame];
}

/* What one entry of a material clip changes. slot: texture matrix, register or texture map number. */
typedef struct NebulaClipTarget {
    uint32_t name_at, name_hash;
    uint32_t material; /* the file's number; a model binds by name */
    uint8_t slot, reserved[3];
    float center[3];
} NebulaClipTarget;

#ifdef __cplusplus
}
static_assert(sizeof(NebulaClipHeader) == 11 * 4);
static_assert(sizeof(NebulaClipTrack) == 12);
static_assert(sizeof(NebulaClipTarget) == 28);
#endif
