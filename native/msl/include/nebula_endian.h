/* Big-endian loads and stores for data in the console's byte order: disc files and the GX command stream. The
   native port reads such data through these (or BE<T>) rather than through host-order pointers. */
#ifndef NEBULA_ENDIAN_H
#define NEBULA_ENDIAN_H

#include <stdint.h>
#include <string.h>

static inline uint16_t nebula_load_be16(const void* p) {
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap16(v);
}

static inline uint32_t nebula_load_be32(const void* p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap32(v);
}

static inline float nebula_load_bef32(const void* p) {
    uint32_t v = nebula_load_be32(p);
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
}

static inline void nebula_store_be16(void* p, uint16_t v) {
    v = __builtin_bswap16(v);
    memcpy(p, &v, sizeof(v));
}

static inline void nebula_store_be32(void* p, uint32_t v) {
    v = __builtin_bswap32(v);
    memcpy(p, &v, sizeof(v));
}

#endif
