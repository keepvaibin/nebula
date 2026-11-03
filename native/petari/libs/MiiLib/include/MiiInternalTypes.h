#ifndef RVL_FACE_LIBRARY_INTERNAL_TYPES_H
#define RVL_FACE_LIBRARY_INTERNAL_TYPES_H
#include "MiiTypes.h"
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
 * Common enums
 */

typedef enum {
    Mii_ArcID_Beard,
    Mii_ArcID_Eye,
    Mii_ArcID_Eyebrow,
    Mii_ArcID_Faceline,
    Mii_ArcID_FaceTex,
    Mii_ArcID_ForeHead,
    Mii_ArcID_Glass,
    Mii_ArcID_GlassTex,
    Mii_ArcID_Hair,
    Mii_ArcID_Mask,
    Mii_ArcID_Mole,
    Mii_ArcID_Mouth,
    Mii_ArcID_Mustache,
    Mii_ArcID_Nose,
    Mii_ArcID_Nline,
    Mii_ArcID_NlineTex,
    Mii_ArcID_Cap,
    Mii_ArcID_CapTex,
    Mii_ArcID_Max
} Mii_ArcID;

typedef enum {
    Mii_AsyncTag_WriteFile,
    Mii_AsyncTag_WriteCloseFile,
    Mii_AsyncTag_ReadFile,
    Mii_AsyncTag_ReadCloseFile,
    Mii_AsyncTag_ReadCache,
    Mii_AsyncTag_ReadCacheClose,
    Mii_AsyncTag_WriteHDBFile,
    Mii_AsyncTag_WriteHDBFileClose,
    Mii_AsyncTag_ReadHDBFile,
    Mii_AsyncTag_ReadHDBFileClose,
    Mii_AsyncTag_CreateAsync,
    Mii_AsyncTag_OpenAsync,
    Mii_AsyncTag_SeekAsync,
    Mii_AsyncTag_ReadAsync,
    Mii_AsyncTag_WriteAsync,
    Mii_AsyncTag_CloseAsync,
    Mii_AsyncTag_GetLengthAsync,
    Mii_AsyncTag_DeleteAsync,
    Mii_AsyncTag_CreateDirAsync,

    Mii_AsyncTag_Max
} Mii_AsyncTag;

typedef enum {
    Mii_FileBrokenType_DBNotFound,
    Mii_FileBrokenType_DBBroken,
    Mii_FileBrokenType_ResBroken,
    Mii_FileBrokenType_Corrupt,

    Mii_FileBrokenType_Max
} Mii_FileBrokenType;

typedef enum {
    Mii_FileType_Database,
    Mii_FileType_Resource,

    Mii_FileType_Max
} Mii_FileType;

typedef enum {
    Mii_HiddenType_Any,
    Mii_HiddenType_No,
    Mii_HiddenType_Yes,
} Mii_HiddenType;

typedef enum {
    Mii_OpenType_Multi,
    Mii_OpenType_Single,
    Mii_OpenType_Opened
} Mii_OpenType;

typedef enum {
    Mii_PartsShp_Nose,
    Mii_PartsShp_Forehead,
    Mii_PartsShp_Faceline,
    Mii_PartsShp_Hair,
    Mii_PartsShp_Cap,
    Mii_PartsShp_Beard,
    Mii_PartsShp_Noseline,
    Mii_PartsShp_Mask,
    Mii_PartsShp_Glass,

    Mii_PartsShp_Max
} Mii_PartsShp;

typedef enum {
    Mii_PartsShpTex_Face,
    Mii_PartsShpTex_Cap,
    Mii_PartsShpTex_Noseline,
    Mii_PartsShpTex_Glass,

    Mii_PartsShpTex_Max
} Mii_PartsShpTex;

typedef enum {
    Mii_PartsTex_Eye,
    Mii_PartsTex_Eyebrow,
    Mii_PartsTex_Mouth,
    Mii_PartsTex_Mustache,
    Mii_PartsTex_Mole,

    Mii_PartsTex_Max
} Mii_PartsTex;

/**
 * Common typedefs
 */

typedef void (*Mii_Callback)(void);
typedef void (*Mii_ExCallback)(uintptr_t arg);

/**
 * Common structs
 */
typedef struct Mii_CharInfo {
    union {
        struct {
            u16 type : 3;
            u16 color : 3;
            u16 texture : 4;
            u16 padding : 6;
        };
        u16 rawdata;
    } faceline;

    union {
        struct {
            u16 type : 7;
            u16 color : 3;
            u16 flip : 1;
            u16 padding : 5;
        };
        u16 rawdata;
    } hair;

    union {
        struct {
            u32 type : 6;
            u32 color : 3;
            u32 scale : 4;
            u32 rotate : 5;
            u32 x : 4;
            u32 y : 5;
            u32 padding : 5;
        };
        u32 rawdata;
    } eye;

    union {
        struct {
            u32 type : 5;
            u32 color : 3;
            u32 scale : 4;
            u32 rotate : 5;
            u32 x : 4;
            u32 y : 5;
            u32 padding : 6;
        };
        u32 rawdata;
    } eyebrow;

    union {
        struct {
            u16 type : 4;
            u16 scale : 4;
            u16 y : 5;
            u16 padding : 3;
        };
        u16 rawdata;
    } nose;

    union {
        struct {
            u16 type : 5;
            u16 color : 2;
            u16 scale : 4;
            u16 y : 5;
        };
        u16 rawdata;
    } mouth;

    union {
        struct {
            u16 mustache : 2;
            u16 type : 2;
            u16 color : 3;
            u16 scale : 4;
            u16 y : 5;
        };
        u16 rawdata;
    } beard;

    union {
        struct {
            u16 type : 4;
            u16 color : 3;
            u16 scale : 4;
            u16 y : 5;
        };
        u16 rawdata;
    } glass;

    union {
        struct {
            u16 type : 1;
            u16 scale : 4;
            u16 x : 5;
            u16 y : 5;
            u16 padding : 1;
        };
        u16 rawdata;
    } mole;

    struct {
        u8 height;
        u8 build;
    } body;

    struct {
        char16_t name[MII_NAME_LEN + 1];
        char16_t creator[MII_CREATOR_LEN + 1];


        u16 sex : 1;
        u16 bmonth : 4;
        u16 bday : 5;
        u16 color : 4;
        u16 favorite : 1;
        u16 localOnly : 1;
    } personal;

    MiiCreateID createID;
} Mii_CharInfo;

#ifdef __cplusplus
}
#endif
#endif
