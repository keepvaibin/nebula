#ifndef RVL_FACE_LIBRARY_INTERNAL_DATABASE_H
#define RVL_FACE_LIBRARY_INTERNAL_DATABASE_H
#include "RFLi_Types.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

// The official database: the host's Mii records by slot (RFL_Database.c).
RFLErrcode RFLiGetCharInfo(RFLiCharInfo* out, u16 index);
BOOL RFLiIsSameID(const RFLCreateID* id1, const RFLCreateID* id2);
BOOL RFLiIsValidID(const RFLCreateID* id);
BOOL RFLiIsSpecialID(const RFLCreateID* id);
BOOL RFLiIsTemporaryID(const RFLCreateID* id);
BOOL RFLiIsValidName2(const RFLiCharInfo* info);

#ifdef __cplusplus
}
#endif
#endif
