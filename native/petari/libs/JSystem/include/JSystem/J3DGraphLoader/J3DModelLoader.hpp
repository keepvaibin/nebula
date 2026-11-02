#pragma once

#include <NebulaGpu.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

class J3DModelData;
class J3DMaterialTable;
class J3DModelHierarchy;

enum J3DModelLoaderFlagTypes {
    J3DMLF_None = 0x00000000,

    J3DMLF_MtxSoftImageCalc = 0x00000001,
    J3DMLF_MtxMayaCalc = 0x00000002,
    J3DMLF_MtxBasicCalc = 0x00000004,
    J3DMLF_04 = 0x00000008,
    J3DMLF_MtxTypeMask =
        J3DMLF_MtxSoftImageCalc | J3DMLF_MtxMayaCalc | J3DMLF_MtxBasicCalc | J3DMLF_04,  // 0 - 2 (0 = Basic, 1 = SoftImage, 2 = Maya)

    J3DMLF_UseImmediateMtx = 0x00000010,
    J3DMLF_UsePostTexMtx = 0x00000020,
    J3DMLF_07 = 0x00000040,
    J3DMLF_08 = 0x00000080,
    J3DMLF_NoMatrixTransform = 0x00000100,
    J3DMLF_10 = 0x00000200,
    J3DMLF_11 = 0x00000400,
    J3DMLF_12 = 0x00000800,
    J3DMLF_13 = 0x00001000,
    J3DMLF_DoBdlMaterialCalc = 0x00002000,
    J3DMLF_15 = 0x00004000,
    J3DMLF_16 = 0x00008000,
    J3DMLF_TevNumShift = 0x00010000,
    J3DMLF_18 = 0x00020000,
    J3DMLF_UseSingleSharedDL = 0x00040000,
    J3DMLF_20 = 0x00080000,
    J3DMLF_21 = 0x00100000,
    J3DMLF_UseUniqueMaterials = 0x00200000,
    J3DMLF_23 = 0x00400000,
    J3DMLF_24 = 0x00800000,
    J3DMLF_Material_UseIndirect = 0x01000000,
    J3DMLF_26 = 0x02000000,
    J3DMLF_27 = 0x04000000,
    J3DMLF_Material_TexGen_Block4 = 0x08000000,
    J3DMLF_Material_PE_Full = 0x10000000,
    J3DMLF_Material_PE_FogOff = 0x20000000,
    J3DMLF_Material_Color_LightOn = 0x40000000,
    J3DMLF_Material_Color_AmbientOn = 0x80000000
};

// The file header, every block's header (kind, size, count, offsets) and the INF1/JNT1/EVP1/DRW1 contents are in
// host order as the importer cooks them (crates/nebula-import cook/native_data.rs j3d_model); the magic, type and
// block kinds read as their character constants.
struct J3DModelBlock {
    /* 0x0 */ u32 mBlockType;
    /* 0x4 */ u32 mBlockSize;
};

struct J3DModelFileData {
    /* 0x00 */ u32 mMagic1;
    /* 0x04 */ u32 mMagic2;
    /* 0x08 */ u8 _8[4];
    /* 0x0C */ u32 mBlockNum;
    /* 0x10 */ u8 _10[0x10];
    /* 0x20 */ J3DModelBlock mBlocks[1];
};

struct J3DModelInfoBlock : public J3DModelBlock {
    /* 0x08 */ u16 mFlags;
    /* 0x0C */ u32 mPacketNum;
    /* 0x10 */ u32 mVtxNum;
    /* 0x14 */ u32 mpHierarchy;
};

struct J3DVertexBlock : public J3DModelBlock {
    /* 0x08 */ u32 mpVtxAttrFmtList;
    /* 0x0C */ u32 mpVtxPosArray;
    /* 0x10 */ u32 mpVtxNrmArray;
    /* 0x14 */ u32 mpVtxNBTArray;
    /* 0x18 */ u32 mpVtxColorArray[2];
    /* 0x20 */ u32 mpVtxTexCoordArray[8];
};

struct J3DEnvelopeBlock : public J3DModelBlock {
    /* 0x08 */ u16 mWEvlpMtxNum;
    /* 0x0C */ u32 mpWEvlpMixMtxNum;
    /* 0x10 */ u32 mpWEvlpMixIndex;
    /* 0x14 */ u32 mpWEvlpMixWeight;
    /* 0x18 */ u32 mpInvJointMtx;
};

struct J3DDrawBlock : public J3DModelBlock {
    /* 0x08 */ u16 mMtxNum;
    /* 0x0C */ u32 mpDrawMtxFlag;
    /* 0x10 */ u32 mpDrawMtxIndex;
};

struct J3DJointBlock : public J3DModelBlock {
    /* 0x08 */ u16 mJointNum;
    /* 0x0C */ u32 mpJointInitData;
    /* 0x10 */ u32 mpIndexTable;
    /* 0x14 */ u32 mpNameTable;
};  // size 0x18

struct J3DMaterialBlock : public J3DModelBlock {
    /* 0x08 */ u16 mMaterialNum;
    /* 0x0C */ u32 mpMaterialInitData;
    /* 0x10 */ u32 mpMaterialID;
    /* 0x14 */ u32 mpNameTable;
    /* 0x18 */ u32 mpIndInitData;
    /* 0x1C */ u32 mpCullMode;
    /* 0x20 */ u32 mpMatColor;
    /* 0x24 */ u32 mpColorChanNum;
    /* 0x28 */ u32 mpColorChanInfo;
    /* 0x2C */ u32 mpAmbColor;
    /* 0x30 */ u32 mpLightInfo;
    /* 0x34 */ u32 mpTexGenNum;
    /* 0x38 */ u32 mpTexCoordInfo;
    /* 0x3C */ u32 mpTexCoord2Info;
    /* 0x40 */ u32 mpTexMtxInfo;
    /* 0x44 */ u32 field_0x44;
    /* 0x48 */ u32 mpTexNo;
    /* 0x4C */ u32 mpTevOrderInfo;
    /* 0x50 */ u32 mpTevColor;
    /* 0x54 */ u32 mpTevKColor;
    /* 0x58 */ u32 mpTevStageNum;
    /* 0x5C */ u32 mpTevStageInfo;
    /* 0x60 */ u32 mpTevSwapModeInfo;
    /* 0x64 */ u32 mpTevSwapModeTableInfo;
    /* 0x68 */ u32 mpFogInfo;
    /* 0x6C */ u32 mpAlphaCompInfo;
    /* 0x70 */ u32 mpBlendInfo;
    /* 0x74 */ u32 mpZModeInfo;
    /* 0x78 */ u32 mpZCompLoc;
    /* 0x7C */ u32 mpDither;
    /* 0x80 */ u32 mpNBTScaleInfo;
};

struct J3DShapeBlock : public J3DModelBlock {
    /* 0x08 */ u16 mShapeNum;
    /* 0x0C */ u32 mpShapeInitData;
    /* 0x10 */ u32 mpIndexTable;
    /* 0x14 */ u32 mpNameTable;
    /* 0x18 */ u32 mpVtxDescList;
    /* 0x1C */ u32 mpMtxTable;
    /* 0x20 */ u32 mpDisplayListData;
    /* 0x24 */ u32 mpMtxInitData;
    /* 0x28 */ u32 mpDrawInitData;
};

struct J3DTextureBlock : public J3DModelBlock {
    /* 0x08 */ u16 mTextureNum;
    /* 0x0C */ u32 mpTextureRes;
    /* 0x10 */ u32 mpNameTable;
};

class J3DModelLoader {
public:
    J3DModelLoader();

    virtual J3DModelData* load(const void*, u32);
    virtual J3DMaterialTable* loadMaterialTable(const void*);
    virtual J3DModelData* loadBinaryDisplayList(const void*, u32);
    virtual void setupBBoardInfo();
    virtual ~J3DModelLoader() {
    }

    virtual void readMaterial(J3DMaterialBlock const*, u32) {
    }

    virtual void readMaterialTable(J3DMaterialBlock const*, u32) {
    }

    void readInformation(J3DModelInfoBlock const*, u32);
    void readVertex(J3DVertexBlock const*);
    void readEnvelop(J3DEnvelopeBlock const*);
    void readDraw(J3DDrawBlock const*);
    void readJoint(J3DJointBlock const*);
    void readShape(J3DShapeBlock const*, u32);
    void readTexture(J3DTextureBlock const*);
    void readTextureTable(J3DTextureBlock const*);
    void readPatchedMaterial(J3DMaterialBlock const*, u32);
    void modifyMaterial(u32);


    /* 0x04 */ J3DModelData* mpModelData;
    /* 0x08 */ J3DMaterialTable* mpMaterialTable;
    /* 0x0C */ const J3DShapeBlock* mpShapeBlock;
    /* 0x10 */ const J3DMaterialBlock* mpMaterialBlock;
    /* 0x14 */ J3DModelHierarchy* mpModelHierarchy;
    /* 0x18 */ u8 field_0x18;  // CLEANME: Change unknown JSystem members to the _XX format?
    /* 0x1C */ u8 field_0x19;
    /* 0x1A */ u16 mEnvelopeSize;
};

class J3DModelLoader_v26 : public J3DModelLoader {
public:
    J3DModelLoader_v26() {
    }

    ~J3DModelLoader_v26() {
    }

    void readMaterial(J3DMaterialBlock const*, u32);
    void readMaterialTable(J3DMaterialBlock const*, u32);
};

class J3DModelLoaderDataBase {
public:
    static J3DModelData* load(void const*, u32);
    static J3DMaterialTable* loadMaterialTable(const void*);
    static J3DModelData* loadBinaryDisplayList(const void*, u32);
};

static inline u32 getMdlDataFlag_TevStageNum(u32 flags) {
    return (flags & 0x001f0000) >> 0x10;
}

static inline u32 getMdlDataFlag_TexGenFlag(u32 flags) {
    return flags & 0x0c000000;
}

static inline u32 getMdlDataFlag_ColorFlag(u32 flags) {
    return flags & 0xc0000000;
}

static inline u32 getMdlDataFlag_PEFlag(u32 flags) {
    return flags & 0x30000000;
}

static inline u32 getMdlDataFlag_MtxLoadType(u32 flags) {
    return flags & 0x10;
}
