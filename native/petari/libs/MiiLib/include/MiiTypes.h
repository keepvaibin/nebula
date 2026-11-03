#ifndef RVL_FACE_LIBRARY_TYPES_H
#define RVL_FACE_LIBRARY_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Common types used throughout many Mii files.
 *
 * To avoid circular depencies, common structures and all enums have been moved
 * here.
 *
 * Everything is organized in alphabetical order (when possible, otherwise
 * dependency order).
 */

/**
 * Common defines
 */

#define MII_NAME_LEN 10
#define MII_CREATOR_LEN 10
#define MII_CREATEID_LEN 8

#define MII_CTRL_CHAR_MAX 10
#define MII_DB_CHAR_MAX 100

/**
 * Common enums
 */

typedef enum { MiiAge_Child, MiiAge_Adult, MiiAge_Elder, MiiAge_All } MiiAge;

typedef enum {
    MiiDataSource_Official,
    MiiDataSource_Hidden,
    MiiDataSource_Controller1,
    MiiDataSource_Controller2,
    MiiDataSource_Controller3,
    MiiDataSource_Controller4,
    MiiDataSource_Default,
    MiiDataSource_Middle,

    MiiDataSource_Max
} MiiDataSource;

typedef enum {
    MiiErrcode_Success,
    MiiErrcode_NotAvailable,
    MiiErrcode_NANDCommandfail,
    MiiErrcode_Loadfail,
    MiiErrcode_Savefail,
    MiiErrcode_Fatal,
    MiiErrcode_Busy,
    MiiErrcode_Broken,
    MiiErrcode_Exist,
    MiiErrcode_DBFull,
    MiiErrcode_DBNodata,
    MiiErrcode_Controllerfail,
    MiiErrcode_NWC24Fail,
    MiiErrcode_MaxFiles,
    MiiErrcode_MaxBlocks,
    MiiErrcode_WrongParam,
    MiiErrcode_NoFriends,
    MiiErrcode_Isolation,

    MiiErrcode_Unknown = 0xFF
} MiiErrcode;

typedef enum {
    MiiExp_Normal,
    MiiExp_Smile,
    MiiExp_Anger,
    MiiExp_Sorrow,
    MiiExp_Surprise,
    MiiExp_Blink,
    MiiExp_OpenMouth,

    MiiExp_Max
} MiiExpression;

typedef enum {
    MiiFavoriteColor_Red,
    MiiFavoriteColor_Orange,
    MiiFavoriteColor_Yellow,
    MiiFavoriteColor_YellowGreen,
    MiiFavoriteColor_Green,
    MiiFavoriteColor_Blue,
    MiiFavoriteColor_SkyBlue,
    MiiFavoriteColor_Pink,
    MiiFavoriteColor_Purple,
    MiiFavoriteColor_Brown,
    MiiFavoriteColor_White,
    MiiFavoriteColor_Black,

    MiiFavoriteColor_Max
} MiiFavoriteColor;

typedef enum {
    MiiRace_Black,
    MiiRace_White,
    MiiRace_Asian,
    MiiRace_All
} MiiRace;

typedef enum {
    // No mipmap
    MiiResolution_64 = 64,
    MiiResolution_128 = 128,
    MiiResolution_256 = 256,

    // Mipmap
    MiiResolution_64M = 64 | 32,
    MiiResolution_128M = 128 | 64 | 32,
    MiiResolution_256M = 256 | 128 | 64 | 32
} MiiResolution;

typedef enum {
    MiiExpFlag_Normal = (1 << MiiExp_Normal),
    MiiExpFlag_Smile = (1 << MiiExp_Smile),
    MiiExpFlag_Anger = (1 << MiiExp_Anger),
    MiiExpFlag_Sorrow = (1 << MiiExp_Sorrow),
    MiiExpFlag_Surprise = (1 << MiiExp_Surprise),
    MiiExpFlag_Blink = (1 << MiiExp_Blink),
    MiiExpFlag_OpenMouth = (1 << MiiExp_OpenMouth),
} MiiExpFlag;

typedef enum { MiiSex_Male, MiiSex_Female, MiiSex_All } MiiSex;

/**
 * Common typedefs
 */

typedef void (*MiiCallback)(void);

/**
 * Common structs
 */
typedef struct MiiCreateID {
    u8 data[MII_CREATEID_LEN];
} MiiCreateID;

typedef struct MiiCharData {
    u8 dummy[0x4A];
} MiiCharData;

typedef struct MiiStoreData {
    u8 dummy[0x4C];
} MiiStoreData;

#ifdef __cplusplus
}
#endif
#endif
