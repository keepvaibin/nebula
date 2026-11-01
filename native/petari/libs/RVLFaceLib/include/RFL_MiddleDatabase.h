#ifndef RVL_FACE_LIBRARY_MIDDLE_DATABASE_H
#define RVL_FACE_LIBRARY_MIDDLE_DATABASE_H
#include "RFL_Types.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MiiMiddleDB {
    // Room for Mii_MiddleDB, which the library keeps here (40 bytes natively: pointer members).
    union {
        u8 dummy[0x28];
        void* align;
    };
} MiiMiddleDB;

#ifdef __cplusplus
}
#endif
#endif
