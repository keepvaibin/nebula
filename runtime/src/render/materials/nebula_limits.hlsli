// SPDX-License-Identifier: GPL-3.0-only
#ifndef NEBULA_SHARED_LIMITS_H
#define NEBULA_SHARED_LIMITS_H

#define NEBULA_NONE32 0xFFFFFFFFu
// No shader reads it: the cooker (crates/nebula-import/src/cook/limits.rs) does, until the model format is counted.
#define NEBULA_MATERIAL_SLOTS 16

#endif
