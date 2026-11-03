#ifndef RVL_FACE_LIBRARY_DATABASE_H
#define RVL_FACE_LIBRARY_DATABASE_H
#include "MiiTypes.h"
#ifdef __cplusplus
extern "C" {
#endif

BOOL MiiIsAvailableOfficialData(u16 index);
BOOL MiiSearchOfficialData(const MiiCreateID* id, u16* index);

#ifdef __cplusplus
}
#endif
#endif
