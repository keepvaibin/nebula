#pragma once

#include <NebulaGpu.h>

// Cooked layouts and layout animations (crates/nebula-import cook/ui_layout.rs): flat records in host order, read in
// place. Offsets count from the file's start; 0 is none. Names are offsets into the file's string pool. Panes are in
// tree order (a pane's descendants follow it), so a subtree is the index range [pane, mSubtreeEnd).

enum { cUiLayoutMagic = 0x54594C4E /* NLYT */, cUiAnimationMagic = 0x4E414C4E /* NLAN */, cUiDataVersion = 3 };
enum { cUiNone16 = 0xFFFF };

enum {
    UiLayoutFlag_Centered = 1,        // the origin is the layout's centre, not its top left
    UiLayoutFlag_GroupContainer = 2,  // the source had a root group: a container, even with no groups in it
};

enum UiPaneKind : u8 { UiPaneKind_Pane, UiPaneKind_Picture, UiPaneKind_TextBox, UiPaneKind_Window, UiPaneKind_Bounding };

struct UiLayoutHeader {
    u32 mMagic;
    u32 mVersion;
    u32 mFlags;  // UiLayoutFlag_*
    f32 mWidth;
    f32 mHeight;
    u32 mPaneCount;
    u32 mPanes;  // UiPaneData[mPaneCount]
    u32 mMaterialCount;
    u32 mMaterials;  // UiMaterialData[mMaterialCount]
    u32 mTextureCount;
    u32 mTextures;  // u32[mTextureCount]: names
    u32 mFontCount;
    u32 mFonts;  // u32[mFontCount]: names
    u32 mGroupCount;
    u32 mGroups;  // UiGroupData[mGroupCount]
    u32 mStrings;
    u32 mStringBytes;
    u32 mPaneSlots;  // a power of two
    u32 mPaneTable;  // u32[mPaneSlots]: first pane with a name, plus one; 0 empty; linear probing over uiNameHash
    u32 mMaterialSlots;
    u32 mMaterialTable;
    u32 mGroupSlots;
    u32 mGroupTable;
};
static_assert(sizeof(UiLayoutHeader) == 92);

struct UiPaneData {
    u16 mParent;       // cUiNone16 for the root
    u16 mFirstChild;   // cUiNone16 none
    u16 mNextSibling;  // cUiNone16 none
    u16 mSubtreeEnd;   // one past the last descendant
    u16 mNextSameName; // the next pane with this name, cUiNone16 none
    UiPaneKind mKind;
    u8 mBasePosition;  // horizontal + 3 * vertical (left/centre/right, top/centre/bottom)
    u8 mAlpha;
    u8 mVisible;
    u8 mInfluencedAlpha;
    u8 mLocationAdjust;
    u32 mName;
    u8 mUserData[8];
    f32 mTranslate[3];
    f32 mRotate[3];
    f32 mScale[2];
    f32 mSize[2];
    u32 mPayload;  // UiPictureData, UiTextBoxData or UiWindowData by mKind; 0 for a plain pane or a bounding
};
static_assert(sizeof(UiPaneData) == 72);

// A picture, and a window's content.
struct UiPictureData {
    u8 mVertexColors[4][4];  // RGBA: left top, right top, left bottom, right bottom
    u16 mMaterial;
    u8 mCoordCount;
    u8 mReserved;
    u32 mCoords;  // f32[mCoordCount][4][2]: each set's corners (s, t)
};
static_assert(sizeof(UiPictureData) == 24);

struct UiTextBoxData {
    u16 mBufferBytes;
    u16 mTextBytes;
    u16 mMaterial;
    u16 mFont;
    u8 mPosition;   // as mBasePosition
    u8 mAlignment;  // as the file holds it (TextBox::GetTextAlignment)
    u16 mReserved;
    u32 mText;  // u16[mTextUnits]: UTF-16 with the message tags kept
    u8 mColors[2][4];  // top, bottom
    f32 mFontSize[2];
    f32 mCharSpace;
    f32 mLineSpace;
    u16 mTextUnits;
    u16 mReserved2;
};
static_assert(sizeof(UiTextBoxData) == 44);

struct UiWindowFrameData {
    u16 mMaterial;
    u8 mFlip;
    u8 mReserved;
};

struct UiWindowData {
    f32 mInflation[4];  // left, right, top, bottom
    u8 mFrameCount;
    u8 mReserved[3];
    u32 mContent;  // UiPictureData
    u32 mFrames;   // UiWindowFrameData[mFrameCount]
};
static_assert(sizeof(UiWindowData) == 28);

// A counted table in the file: `mCount` entries of the list's record at `mOffset` (0 when there are none).
struct UiDataList {
    u32 mCount;
    u32 mOffset;
};
static_assert(sizeof(UiDataList) == 8);

struct UiMapData {
    u16 mTexture;  // into the layout's texture names
    u8 mWrapS;
    u8 mWrapT;
    u8 mMinFilter;
    u8 mMagFilter;
};
static_assert(sizeof(UiMapData) == 6);

// A texture matrix: the translation, the rotation in degrees, the scale.
struct UiSrtData {
    f32 mTranslate[2];
    f32 mRotate;
    f32 mScale[2];
};
static_assert(sizeof(UiSrtData) == 20);

struct UiGeneratorData {
    u8 mType;
    u8 mSource;
    u8 mMatrix;
};
static_assert(sizeof(UiGeneratorData) == 3);

// A material's form as the cook wrote it: its shader pair, and the rows it reads, its texture slots and its inputs.
struct UiFormData {
    u64 mVertex;
    u64 mPixel;
    UiDataList mRows;      // u32[mRows.mCount]: the vertex program's first (mVertexRows of them), then the pixel program's
    UiDataList mTextures;  // u8[mTextures.mCount]: the material's map for each slot
    UiDataList mInputs;    // u8[mInputs.mCount]: the input codes
    u32 mVertexRows;
    u8 mFlags;
    u8 mReserved[3];  // 8-aligned for the hashes
};
static_assert(sizeof(UiFormData) == 48);

enum {
    UiMaterialFlag_Channel = 1,
    UiMaterialFlag_MaterialColor = 2,
    UiMaterialFlag_AlphaCompare = 4,
    UiMaterialFlag_Blend = 8,
    UiMaterialFlag_Stages = 16,  // the shader combines more than the default of a material without its own
};

struct UiMaterialData {
    u32 mName;
    u16 mNextSameName;
    u8 mFlags;  // UiMaterialFlag_*
    u8 mReserved;
    s16 mRegisters[3][4];  // RGBA
    u8 mConstants[4][4];   // RGBA
    u8 mChannel[2];        // colour source, alpha source
    u8 mMaterialColor[4];  // RGBA
    u8 mAlphaCompare[4];   // comp0 | comp1 << 4, op, ref0, ref1
    u8 mBlend[4];          // type, source factor, destination factor, op
    u16 mReserved2;
    UiDataList mMaps;          // UiMapData[]
    UiDataList mSrts;          // UiSrtData[]
    UiDataList mGenerators;    // UiGeneratorData[]
    UiDataList mIndirectSrts;  // UiSrtData[]
    UiDataList mCoordMap;      // u8[]: each coordinate's map, 255 none
    UiFormData mForms[4];  // by raster stage (1) and remap (2)
};
static_assert(sizeof(UiMaterialData) == 296);

struct UiGroupData {
    u32 mName;
    u32 mPaneCount;
    u32 mPanes;  // u16[mPaneCount]: pane indices
};
static_assert(sizeof(UiGroupData) == 12);

struct UiAnimationHeader {
    u32 mMagic;
    u32 mVersion;
    u32 mFrameSize;
    u32 mLoop;
    u32 mFileCount;
    u32 mFiles;  // u32[mFileCount]: the texture-pattern images' names
    u32 mContentCount;
    u32 mContents;  // UiAnimContentData[mContentCount]
    u32 mStrings;
    u32 mStringBytes;
};
static_assert(sizeof(UiAnimationHeader) == 40);

struct UiAnimContentData {
    u32 mName;
    u32 mMaterial;  // 0 a pane's, 1 a material's
    u32 mInfoCount;
    u32 mInfos;  // UiAnimInfoData[mInfoCount]
};

// An animation's kind: its source tag's four characters, the first highest.
enum UiAnimKind : u32 {
    UiAnimKind_PaneSrt = 0x524C5041,          // RLPA
    UiAnimKind_Visibility = 0x524C5649,       // RLVI
    UiAnimKind_VertexColor = 0x524C5643,      // RLVC
    UiAnimKind_MaterialColor = 0x524C4D43,    // RLMC
    UiAnimKind_TextureSrt = 0x524C5453,       // RLTS
    UiAnimKind_TexturePattern = 0x524C5450,   // RLTP
    UiAnimKind_IndirectSrt = 0x524C494D,      // RLIM
};

struct UiAnimInfoData {
    u32 mKind;  // UiAnimKind
    u32 mTargetCount;
    u32 mTargets;  // UiAnimTargetData[mTargetCount]
};

struct UiAnimTargetData {
    u8 mId;
    u8 mTarget;
    u8 mStep;  // 1: UiStepKey, 0: UiHermiteKey
    u8 mReserved;
    u32 mKeyCount;
    u32 mKeys;
};
static_assert(sizeof(UiAnimTargetData) == 12);

struct UiHermiteKey {
    f32 mFrame;
    f32 mValue;
    f32 mSlope;
};

struct UiStepKey {
    f32 mFrame;
    u16 mValue;
    u16 mReserved;
};

// A record in a cooked file, by its offset from the file's start.
template < typename T >
inline const T* uiAt(const void* pFile, u32 offset) {
    return reinterpret_cast< const T* >(static_cast< const u8* >(pFile) + offset);
}

// FNV-1a over a name's bytes, as the cook built the tables.
inline u32 uiNameHash(const char* pName) {
    u32 hash = 0x811C9DC5u;
    for (const char* p = pName; *p != '\0'; ++p) {
        hash = (hash ^ static_cast< u8 >(*p)) * 0x01000193u;
    }
    return hash;
}

inline bool uiSameName(const char* pA, const char* pB) {
    while (*pA != '\0' && *pA == *pB) {
        ++pA;
        ++pB;
    }
    return *pA == *pB;
}

// The first pane with this name in tree order, or cUiNone16.
inline u32 uiFindPane(const UiLayoutHeader* pLayout, const char* pName) {
    const u32* const pTable = uiAt< u32 >(pLayout, pLayout->mPaneTable);
    const UiPaneData* const pPanes = uiAt< UiPaneData >(pLayout, pLayout->mPanes);
    const u32 mask = pLayout->mPaneSlots - 1;
    for (u32 slot = uiNameHash(pName) & mask;; slot = (slot + 1) & mask) {
        const u32 entry = pTable[slot];
        if (entry == 0) {
            return cUiNone16;
        }
        if (uiSameName(uiAt< char >(pLayout, pLayout->mStrings + pPanes[entry - 1].mName), pName)) {
            return entry - 1;
        }
    }
}
