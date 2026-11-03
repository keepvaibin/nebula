#ifndef RVL_FACE_LIBRARY_INTERNAL_DATA_UTILITY_H
#define RVL_FACE_LIBRARY_INTERNAL_DATA_UTILITY_H
#include "MiiInternalTypes.h"
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct MiiMiddleDB;

BOOL Mii_CheckValidInfo(const Mii_CharInfo* info);
BOOL Mii_IsValidOnNAND(const Mii_CharInfo* info);
BOOL Mii_IsSameFaceCore(const Mii_CharInfo* lhs, const Mii_CharInfo* rhs);
MiiErrcode Mii_PickupCharInfo(Mii_CharInfo* info, MiiDataSource source,
                              struct MiiMiddleDB* db, u16 index);
BOOL Mii_CheckBirthday(u8 month, u8 day);

#ifdef __cplusplus
}
#endif
#endif
