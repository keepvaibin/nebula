/* CodeWarrior's runtime conversions the game calls by name. The game module is built with
   -fno-strict-float-cast-overflow, so these casts saturate: negative and NaN to 0, past the range to the maximum,
   as __cvt_fp2unsigned does on the console. */
#ifndef NEBULA_MSL_RUNTIME_H
#define NEBULA_MSL_RUNTIME_H
#include <stdint.h>
static inline uint32_t __cvt_fp2unsigned(double x) {
    return (uint32_t)x;
}
static inline uint64_t __cvt_dbl_usll(double x) {
    return (uint64_t)x;
}
#endif
