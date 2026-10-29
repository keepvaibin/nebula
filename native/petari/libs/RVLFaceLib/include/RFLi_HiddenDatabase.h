#ifndef RVL_FACE_LIBRARY_INTERNAL_HIDDEN_DATABASE_H
#define RVL_FACE_LIBRARY_INTERNAL_HIDDEN_DATABASE_H
#include "RFLi_Format.h"
#include "RFLi_Types.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RFLi_HDB_DATA_MAX 10000

typedef struct RFLiHiddenDB {
    u32 identifier;                        // at 0x0
    s16 head;                              // at 0x4
    s16 tail;                              // at 0x6
    RFLiTableData data[RFLi_HDB_DATA_MAX]; // at 0x8
    u8 padding[22];                        // at 0x1D4C8
    u16 crc;                               // at 0x1D4DE
} RFLiHiddenDB;

#ifdef __cplusplus
}
#endif
#endif
