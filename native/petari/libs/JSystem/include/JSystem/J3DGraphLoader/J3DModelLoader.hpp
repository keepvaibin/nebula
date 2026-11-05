#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DModelData;
class J3DMaterialTable;
struct J3DModelHierarchy;

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
    J3DMLF_StageCountShift = 0x00010000,
    J3DMLF_18 = 0x00020000,
    J3DMLF_UseSingleSharedSnapshot = 0x00040000,
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
    J3DMLF_Material_Color_AmbientOn = 1u << 31
};

// The file header, every block's header (kind, size, count, offsets) and the INF1/JNT1/EVP1/DRW1 contents are in
// host order as the importer cooks them (crates/nebula-import cook/native_data.rs j3d_model); the magic, type and
// block kinds read as their character constants.
struct J3DModelBlock {
    u32 mBlockType;
    u32 mBlockSize;
};

struct J3DModelFileData {
    u32 mMagic1;
    u32 mMagic2;
    u8 _8[4];
    u32 mBlockNum;
    u8 _10[0x10];
    J3DModelBlock mBlocks[1];
};

struct J3DModelInfoBlock : public J3DModelBlock {
    u16 mFlags;
    u32 mPacketNum;
    u32 mVtxNum;
    u32 mpHierarchy;
};

struct J3DVertexBlock : public J3DModelBlock {
    u32 mpVtxAttrFmtList;
    u32 mpVtxPosArray;
    u32 mpVtxNrmArray;
    u32 mpVtxNBTArray;
    u32 mpVtxColorArray[2];
    u32 mpVtxTexCoordArray[8];
    u32 mCounts[13];   // entries of each array above, position first
    u32 mNormalFloats;  // 3, or 9 where the model's normals are NBT normals
};

struct J3DEnvelopeBlock : public J3DModelBlock {
    u16 mWEvlpMtxNum;
    u32 mpWEvlpMixMtxNum;
    u32 mpWEvlpMixIndex;
    u32 mpWEvlpMixWeight;
    u32 mpInvJointMtx;
};

struct J3DDrawBlock : public J3DModelBlock {
    u16 mMtxNum;
    u32 mpDrawMtxFlag;
    u32 mpDrawMtxIndex;
};

struct J3DJointBlock : public J3DModelBlock {
    u16 mJointNum;
    u32 mpJointInitData;
    u32 mpIndexTable;
    u32 mpNameTable;
};

struct J3DMaterialBlock : public J3DModelBlock {
    u16 mMaterialNum;
    u32 mpMaterialInitData;
    u32 mpMaterialID;
    u32 mpNameTable;
    u32 mpIndInitData;
    u32 mpCullMode;
    u32 mpMatColor;
    u32 mpColorChanNum;
    u32 mpColorChanInfo;
    u32 mpAmbColor;
    u32 mpLightInfo;
    u32 mpTexGenNum;
    u32 mpTexCoordInfo;
    u32 mpTexCoord2Info;
    u32 mpTexMtxInfo;
    u32 field_0x44;
    u32 mpTexNo;
    u32 mpStageOrderInfo;
    u32 mpRegisterColor;
    u32 mpConstantColor;
    u32 mpStageCount;
    u32 mpStageStageInfo;
    u32 mpStageSwapModeInfo;
    u32 mpStageSwapModeTableInfo;
    u32 mpFogInfo;
    u32 mpAlphaCompInfo;
    u32 mpBlendInfo;
    u32 mpZModeInfo;
    u32 mpZCompLoc;
    u32 mpDither;
    u32 mpNBTScaleInfo;
};

struct J3DShapeBlock : public J3DModelBlock {
    u16 mShapeNum;
    u32 mpShapeInitData;
    u32 mpIndexTable;
    u32 mpNameTable;
    u32 mpVtxDescList;
    u32 mpMtxTable;
    u32 mpShapeDrawData;
    u32 mpMtxInitData;
    u32 mpDrawInitData;
};

struct J3DTextureBlock : public J3DModelBlock {
    u16 mTextureNum;
    u32 mpTextureRes;
    u32 mpNameTable;
};

class J3DModelLoader {
public:
    J3DModelLoader();

    virtual J3DModelData* load(const void*, u32);
    virtual J3DMaterialTable* loadMaterialTable(const void*);
    virtual J3DModelData* loadBinaryMaterials(const void*, u32);
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


    J3DModelData* mpModelData;
    J3DMaterialTable* mpMaterialTable;
    void finishMaterials();
    const J3DShapeBlock* mpShapeBlock;
    const J3DMaterialBlock* mpMaterialBlock;
    J3DModelHierarchy* mpModelHierarchy;
    u8 field_0x18;  // CLEANME: Change unknown JSystem members to the _XX format?
    u8 field_0x19;
    u16 mEnvelopeSize;
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
    static J3DModelData* loadBinaryMaterials(const void*, u32);
};

static inline u32 getMdlDataFlag_StageCount(u32 flags) {
    return (flags & 0x001f0000) >> 0x10;
}

static inline u32 getMdlDataFlag_TexGenFlag(u32 flags) {
    return flags & 0x0c000000;
}

static inline u32 getMdlDataFlag_ColorFlag(u32 flags) {
    return flags & (3u << 30);
}

static inline u32 getMdlDataFlag_OutputFlag(u32 flags) {
    return flags & 0x30000000;
}

static inline u32 getMdlDataFlag_MtxLoadType(u32 flags) {
    return flags & 0x10;
}
