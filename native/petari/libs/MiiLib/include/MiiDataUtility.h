#ifndef RVL_FACE_LIBRARY_DATA_UTILITY_H
#define RVL_FACE_LIBRARY_DATA_UTILITY_H
#include "MiiTypes.h"
#include <NebulaGpu.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct MiiMiddleDB;

typedef struct MiiAdditionalInfo {
    char16_t name[MII_NAME_LEN + 1];
    char16_t creator[MII_CREATOR_LEN + 1];
    MiiCreateID createID;


    u32 sex : 1;
    u32 bmonth : 4;
    u32 bday : 5;
    u32 color : 4;
    u32 favorite : 1;
    u32 height : 7;
    u32 build : 7;
    u32 reserved : 3;

    NebulaColor8 skinColor;
} MiiAdditionalInfo;

MiiErrcode MiiGetAdditionalInfo(MiiAdditionalInfo* info, MiiDataSource source,
                                struct MiiMiddleDB* db, u16 index);

#ifdef __cplusplus
}
#endif
#endif
