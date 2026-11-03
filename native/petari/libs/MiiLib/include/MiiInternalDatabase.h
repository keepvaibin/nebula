#ifndef RVL_FACE_LIBRARY_INTERNAL_DATABASE_H
#define RVL_FACE_LIBRARY_INTERNAL_DATABASE_H
#include "MiiInternalTypes.h"
#ifdef __cplusplus
extern "C" {
#endif

// The official database: the host's Mii records by slot (MiiDatabase.c).
MiiErrcode Mii_GetCharInfo(Mii_CharInfo* out, u16 index);
BOOL Mii_IsSameID(const MiiCreateID* id1, const MiiCreateID* id2);
BOOL Mii_IsValidID(const MiiCreateID* id);
BOOL Mii_IsSpecialID(const MiiCreateID* id);
BOOL Mii_IsTemporaryID(const MiiCreateID* id);
BOOL Mii_IsValidName2(const Mii_CharInfo* info);

#ifdef __cplusplus
}
#endif
#endif
