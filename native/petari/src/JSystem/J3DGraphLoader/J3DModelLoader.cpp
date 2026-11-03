#include "JSystem/J3DGraphAnimator/J3DJoint.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include "Game/System/NebulaCurvedSurfaces.hpp"
#include "JSystem/J3DGraphLoader/J3DModelLoader.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphLoader/J3DJointFactory.hpp"
#include "JSystem/J3DGraphLoader/J3DMaterialFactory.hpp"
#include "JSystem/J3DGraphLoader/J3DShapeFactory.hpp"
#include "Game/System/NebulaArena.hpp"
#include "JSystem/JSupport/JSupport.hpp"
#include "JSystem/JUtility/JUTNameTab.hpp"
#include <stdint.h>
#include <cstring>
#include "Game/Util/ByteView.hpp"

namespace {
    // A material's ID (mDiffFlag): packets with the same ID share their material state (J3DMatPacket::isSame). Each
    // material table takes a fresh range, so IDs are unique
    // across models, never 0 (a material no loader named), and below bit 30 (J3DDiffFlag_Unk40000000,
    // J3DDiffFlag_Changed).
    u32 sNextMaterialID = 1;

    u32 allocMaterialIDs(u32 count) {
        if (sNextMaterialID + count >= 0x40000000) {
            sNextMaterialID = 1;
        }
        const u32 base = sNextMaterialID;
        sNextMaterialID += count;
        return base;
    }
}  // namespace

J3DModelLoader::J3DModelLoader()
    : mpModelData(NULL), mpMaterialTable(NULL), mpShapeBlock(NULL), mpMaterialBlock(NULL), mpModelHierarchy(NULL), field_0x18(0), mEnvelopeSize(0) {
    /* empty function */
}

// Nebula: each model the renderer may round (NebulaCurvedSurfaces).
static J3DModelData* withCurvedSurfaces(J3DModelData* pModelData) {
    NebulaCurvedSurfaces::registerModel(pModelData);
    return pModelData;
}

// A model as the disc has it ("J3D2" read in host order): the importer cooks every one.
static void panicIfNotCooked(const void* pData) {
    if (readBytes< u32 >(pData) == '2D3J') {
        NEBULA_PANIC("J3DModelLoader: a model not in host order (the importer cooks it)");
    }
}

J3DModelData* J3DModelLoaderDataBase::load(void const* i_data, u32 i_flags) {
    if (i_data == NULL) {
        return NULL;
    }
    panicIfNotCooked(i_data);
    if (readBytes< u32 >(i_data) == 'J3D1' && readBytes< u32 >(static_cast< const u8* >(i_data) + 4) == 'bmd1') {
        return NULL;
    }
    if (readBytes< u32 >(i_data) == 'J3D2' && readBytes< u32 >(static_cast< const u8* >(i_data) + 4) == 'bmd3') {
        J3DModelLoader_v26 loader;
        return withCurvedSurfaces(loader.load(i_data, i_flags));
    }
    return NULL;
}

J3DMaterialTable* J3DModelLoaderDataBase::loadMaterialTable(const void* data) {
    if (data == NULL) {
        return NULL;
    }
    panicIfNotCooked(data);
    if (reinterpret_cast< const u32* >(data)[0] == 'J3D2' && reinterpret_cast< const u32* >(data)[1] == 'bmt3') {
        J3DModelLoader_v26 loader;
        return loader.loadMaterialTable(data);
    }
    return NULL;
}

J3DModelData* J3DModelLoaderDataBase::loadBinaryMaterials(const void* data, u32 flags) {
    if (data == NULL) {
        return NULL;
    }
    panicIfNotCooked(data);
    if (reinterpret_cast< const u32* >(data)[0] == 'J3D2' &&
        (reinterpret_cast< const u32* >(data)[1] == 'bdl3' || reinterpret_cast< const u32* >(data)[1] == 'bdl4')) {
        J3DModelLoader_v26 loader;
        return withCurvedSurfaces(loader.loadBinaryMaterials(data, flags));
    }
    return NULL;
}

J3DModelData* J3DModelLoader::load(void const* i_data, u32 i_flags) {
    mpModelData = new J3DModelData();
    mpModelData->clear();
    mpModelData->mpRawData = i_data;
    mpModelData->setModelDataType(0);
    mpMaterialTable = &mpModelData->mMaterialTable;
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        switch (block->mBlockType) {
        case 'INF1':
            readInformation((J3DModelInfoBlock*)block, (s32)i_flags);
            break;
        case 'VTX1':
            readVertex((J3DVertexBlock*)block);
            break;
        case 'EVP1':
            readEnvelop((J3DEnvelopeBlock*)block);
            break;
        case 'DRW1':
            readDraw((J3DDrawBlock*)block);
            break;
        case 'JNT1':
            readJoint((J3DJointBlock*)block);
            break;
        case 'MAT3':
            readMaterial((J3DMaterialBlock*)block, (s32)i_flags);
            break;
        case 'SHP1':
            readShape((J3DShapeBlock*)block, (s32)i_flags);
            break;
        case 'TEX1':
            readTexture((J3DTextureBlock*)block);
            break;
        default:
            break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    J3DModelHierarchy const* hierarchy = mpModelData->getHierarchy();
    mpModelData->makeHierarchy(NULL, &hierarchy);
    mpModelData->getJointTree().findImportantMtxIndex();
    setupBBoardInfo();
    if (mpModelData->getFlag() & 0x100) {
        for (u16 shape_no = 0; shape_no < mpModelData->getShapeNum(); shape_no++) {
            mpModelData->getShapeNodePointer(shape_no)->onFlag(0x200);
        }
    }
    return mpModelData;
}

J3DMaterialTable* J3DModelLoader::loadMaterialTable(void const* i_data) {
    mpMaterialTable = new J3DMaterialTable();
    mpMaterialTable->clear();
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        switch (block->mBlockType) {
        case 'MAT3':
            readMaterialTable((J3DMaterialBlock*)block, 0x51100000);
            break;
        case 'TEX1':
            readTextureTable((J3DTextureBlock*)block);
            break;
        default:
            break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    if (mpMaterialTable->mTexture == NULL) {
        mpMaterialTable->mTexture = new J3DTexture(0, NULL);
    }
    return mpMaterialTable;
}

J3DModelData* J3DModelLoader::loadBinaryMaterials(void const* i_data, u32 i_flags) {
    mpModelData = new J3DModelData();
    mpModelData->clear();
    mpModelData->mpRawData = i_data;
    // Its materials are full ones made from MAT3 (no precompiled lists): the model is drawn as a .bmd one.
    mpModelData->setModelDataType(0);
    mpMaterialTable = &mpModelData->mMaterialTable;
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        s32 flags;
        switch (block->mBlockType) {
        case 'INF1':
            flags = i_flags;
            readInformation((J3DModelInfoBlock*)block, flags);
            break;
        case 'VTX1':
            readVertex((J3DVertexBlock*)block);
            break;
        case 'EVP1':
            readEnvelop((J3DEnvelopeBlock*)block);
            break;
        case 'DRW1':
            readDraw((J3DDrawBlock*)block);
            break;
        case 'JNT1':
            readJoint((J3DJointBlock*)block);
            break;
        case 'SHP1':
            readShape((J3DShapeBlock*)block, i_flags);
            break;
        case 'TEX1':
            readTexture((J3DTextureBlock*)block);
            break;
        case 'MDL3':
            // The file's precompiled material lists: its materials come from MAT3, their lists
            // from J3DModel::makeMesh as a .bmd model's.
            break;
        case 'MAT3':
            flags = 0x50100000;
            flags |= (i_flags & 0x3000000);
            mpMaterialBlock = (J3DMaterialBlock*)block;
            readMaterial((J3DMaterialBlock*)block, flags);
            break;
        default:
            break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    J3DModelHierarchy const* hierarchy = mpModelData->getHierarchy();
    mpModelData->makeHierarchy(NULL, &hierarchy);
    mpModelData->getJointTree().findImportantMtxIndex();
    setupBBoardInfo();
    return mpModelData;
}

void J3DModelLoader::setupBBoardInfo() {
    for (u16 i = 0; i < mpModelData->getJointNum(); i++) {
        J3DMaterial* mesh = mpModelData->getJointNodePointer(i)->getMesh();
        if (mesh != NULL) {
            u32 shape_index = mesh->getShape()->getIndex();
            u16* index_table = JSUConvertOffsetToPtr< u16 >(mpShapeBlock, mpShapeBlock->mpIndexTable);
            J3DShapeInitData* shape_init_data = JSUConvertOffsetToPtr< J3DShapeInitData >(mpShapeBlock, mpShapeBlock->mpShapeInitData);
            J3DJoint* joint;
            switch (shape_init_data[index_table[shape_index]].mShapeMtxType) {
            case 0:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(0);
                break;
            case 1:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(1);
                mpModelData->mbHasBillboard = true;
                break;
            case 2:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(2);
                mpModelData->mbHasBillboard = true;
                break;
            case 3:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(0);
                break;
            default:
                break;
            }
        }
    }
}

void J3DModelLoader::readInformation(J3DModelInfoBlock const* i_block, u32 i_flags) {
    mpModelData->mFlags = i_flags | i_block->mFlags;
    mpModelData->getJointTree().setFlag(mpModelData->mFlags);
    J3DMtxCalc* mtx_calc = NULL;
    switch (mpModelData->mFlags & 0xf) {
    case 0:
        mtx_calc = new J3DMtxCalcNoAnm< J3DMtxCalcCalcTransformBasic, J3DMtxCalcJ3DSysInitBasic >();
        break;
    case 1:
        mtx_calc = new J3DMtxCalcNoAnm< J3DMtxCalcCalcTransformSoftimage, J3DMtxCalcJ3DSysInitSoftimage >();
        break;
    case 2:
        mtx_calc = new J3DMtxCalcNoAnm< J3DMtxCalcCalcTransformMaya, J3DMtxCalcJ3DSysInitMaya >();
        break;
    }
    mpModelData->setBasicMtxCalc(mtx_calc);
    mpModelData->getVertexData().mPacketNum = i_block->mPacketNum;
    mpModelData->getVertexData().mVtxNum = i_block->mVtxNum;
    mpModelData->setHierarchy(JSUConvertOffsetToPtr< J3DModelHierarchy >(i_block, i_block->mpHierarchy));
}

inline J3DMtxCalcNoAnmBase::J3DMtxCalcNoAnmBase() {
}

void J3DModelLoader::readVertex(J3DVertexBlock const* i_block) {
    J3DVertexData& vertex_data = mpModelData->getVertexData();
    for (int i = 0; i < 13; i++) {
        vertex_data.mNebulaArrayCount[i] = i_block->mCounts[i];
    }
    vertex_data.mNormalFloats = i_block->mNormalFloats;
    vertex_data.mVtxPosArray = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxPosArray);
    vertex_data.mVtxNrmArray = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxNrmArray);
    vertex_data.mVtxNBTArray = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxNBTArray);
    for (int i = 0; i < 2; i++) {
        vertex_data.mVtxColorArray[i] = (NebulaColor8*)JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxColorArray[i]);
    }
    for (int i = 0; i < 8; i++) {
        vertex_data.mVtxTexCoordArray[i] = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxTexCoordArray[i]);
    }

    u32 nrm_size = 12;

    void* nrm_end = NULL;
    if (vertex_data.mVtxNBTArray != NULL) {
        nrm_end = vertex_data.mVtxNBTArray;
    } else if (vertex_data.mVtxColorArray[0] != NULL) {
        nrm_end = vertex_data.mVtxColorArray[0];
    } else if (vertex_data.mVtxTexCoordArray[0] != NULL) {
        nrm_end = vertex_data.mVtxTexCoordArray[0];
    }

    if (vertex_data.mVtxNrmArray == NULL) {
        vertex_data.mNrmNum = 0;
    } else if (nrm_end != NULL) {
        vertex_data.mNrmNum = ((uintptr_t)nrm_end - (uintptr_t)vertex_data.mVtxNrmArray) / nrm_size + 1;
    } else {
        vertex_data.mNrmNum = (i_block->mBlockSize - i_block->mpVtxNrmArray) / nrm_size + 1;
    }

    void* color0_end = NULL;
    if (vertex_data.mVtxColorArray[1] != NULL) {
        color0_end = vertex_data.mVtxColorArray[1];
    } else if (vertex_data.mVtxTexCoordArray[0] != NULL) {
        color0_end = vertex_data.mVtxTexCoordArray[0];
    }

    if (vertex_data.mVtxColorArray[0] == NULL) {
        vertex_data.mColNum = 0;
    } else if (color0_end != NULL) {
        vertex_data.mColNum = ((uintptr_t)color0_end - (uintptr_t)vertex_data.mVtxColorArray[0]) / 4 + 1;
    } else {
        vertex_data.mColNum = (i_block->mBlockSize - i_block->mpVtxColorArray[0]) / 4 + 1;
    }

    if (vertex_data.mVtxTexCoordArray[0] == NULL) {
        vertex_data.mTexCoordNum = 0;
    } else {
        vertex_data.mTexCoordNum = (i_block->mBlockSize - i_block->mpVtxTexCoordArray[0]) / 8 + 1;
    }
}

void J3DModelLoader::readEnvelop(J3DEnvelopeBlock const* i_block) {
    mpModelData->getJointTree().mWEvlpMtxNum = i_block->mWEvlpMtxNum;
    mpModelData->getJointTree().mWEvlpMixMtxNum = JSUConvertOffsetToPtr< u8 >(i_block, i_block->mpWEvlpMixMtxNum);
    // The file stores the indices right after a table of bytes, at any address: a copy holds them aligned.
    u16* mixIndex = JSUConvertOffsetToPtr< u16 >(i_block, i_block->mpWEvlpMixIndex);
    if (mixIndex != nullptr && (reinterpret_cast< uintptr_t >(mixIndex) & 1u) != 0u) {
        u32 total = 0;
        for (u16 i = 0; i < i_block->mWEvlpMtxNum; i++) {
            total += mpModelData->getJointTree().mWEvlpMixMtxNum[i];
        }
        u16* aligned = new u16[total];
        memcpy(aligned, mixIndex, total * sizeof(u16));
        mixIndex = aligned;
    }
    mpModelData->getJointTree().mWEvlpMixMtxIndex = mixIndex;
    mpModelData->getJointTree().mWEvlpMixWeight = JSUConvertOffsetToPtr< f32 >(i_block, i_block->mpWEvlpMixWeight);
    mpModelData->getJointTree().mInvJointMtx = JSUConvertOffsetToPtr< Mtx >(i_block, i_block->mpInvJointMtx);
}

void J3DModelLoader::readDraw(J3DDrawBlock const* i_block) {
    u16 i;
    J3DModelData* modelData = mpModelData;
    modelData->getJointTree().mDrawMtxData.mEntryNum = i_block->mMtxNum - mpModelData->getJointTree().mWEvlpMtxNum;
    modelData->getJointTree().mDrawMtxData.mDrawMtxFlag = JSUConvertOffsetToPtr< u8 >(i_block, i_block->mpDrawMtxFlag);
    modelData->getJointTree().mDrawMtxData.mDrawMtxIndex = JSUConvertOffsetToPtr< u16 >(i_block, i_block->mpDrawMtxIndex);
    for (i = 0; i < modelData->getJointTree().mDrawMtxData.mEntryNum; i++) {
        if (modelData->getJointTree().mDrawMtxData.mDrawMtxFlag[i] == 1) {
            break;
        }
    }
    modelData->getJointTree().mDrawMtxData.mDrawFullWgtMtxNum = i;
    mpModelData->getJointTree().mWEvlpImportantMtxIdx = new u16[modelData->getJointTree().mDrawMtxData.mEntryNum];
}

void J3DModelLoader::readJoint(J3DJointBlock const* i_block) {
    J3DJointFactory factory(*i_block);
    mpModelData->getJointTree().mJointNum = i_block->mJointNum;
    if (i_block->mpNameTable != 0) {
        mpModelData->getJointTree().mJointName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpModelData->getJointTree().mJointName = NULL;
    }
    mpModelData->getJointTree().mJointNodePointer = new J3DJoint*[mpModelData->getJointTree().mJointNum];
    for (u16 i = 0; i < mpModelData->getJointTree().getJointNum(); i++) {
        mpModelData->getJointTree().mJointNodePointer[i] = factory.create(i);
    }
}

void J3DModelLoader_v26::readMaterial(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    mpMaterialTable->mUniqueMatNum = factory.countUniqueMaterials();
    if (i_block->mpNameTable != 0) {
        mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    const u32 materialIDs = allocMaterialIDs(mpMaterialTable->mMaterialNum);
    if (i_flags & 0x200000) {
        mpMaterialTable->field_0x10 = new (0x20) J3DMaterial[mpMaterialTable->mUniqueMatNum];
    } else {
        mpMaterialTable->field_0x10 = NULL;
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mUniqueMatNum; i++) {
            factory.create(&mpMaterialTable->field_0x10[i], J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
            mpMaterialTable->field_0x10[i].mDiffFlag = materialIDs + i;
        }
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = materialIDs + factory.getMaterialID(i);
            mpMaterialTable->mMaterialNodePointer[i]->mpOrigMaterial = &mpMaterialTable->field_0x10[factory.getMaterialID(i)];
        }
    } else {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = materialIDs + factory.getMaterialID(i);
        }
    }
}

void J3DModelLoader::readShape(J3DShapeBlock const* i_block, u32 i_flags) {
    mpShapeBlock = i_block;
    J3DShapeTable* shape_table = mpModelData->getShapeTable();
    J3DShapeFactory factory(*i_block);
    shape_table->mShapeNum = i_block->mShapeNum;
    if (i_block->mpNameTable != 0) {
        shape_table->mShapeName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        shape_table->mShapeName = NULL;
    }
    shape_table->mShapeNodePointer = new J3DShape*[shape_table->mShapeNum];
    J3DModelHierarchy const* hierarchy_entry = mpModelData->getHierarchy();
    for (; hierarchy_entry->mType != 0; hierarchy_entry++) {
        if (hierarchy_entry->mType == 0x12) {
            shape_table->mShapeNodePointer[hierarchy_entry->mValue] = factory.create(hierarchy_entry->mValue, i_flags);
        }
    }
}

// The model's images handed to the renderer as they load (J3DNativeTexture::registerImage).
static void registerTextureImages(const ResTIMG* pRes, u32 num) {
    for (u32 i = 0; i < num; i++) {
        const ResTIMG& rTimg = pRes[i];
        const u8* pImage = rTimg.image();
        // Every level the file holds: a material may sample levels the image's own mip flag leaves off.
        J3DNativeTexture::registerRecord(pImage);
    }
}

void J3DModelLoader::readTexture(J3DTextureBlock const* i_block) {
    ResTIMG* texture_res;
    u32 texture_num = i_block->mTextureNum;
    texture_res = JSUConvertOffsetToPtr< ResTIMG >(i_block, i_block->mpTextureRes);
    if (i_block->mpNameTable != 0) {
        mpMaterialTable->mTextureName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mTextureName = NULL;
    }
    mpMaterialTable->mTexture = new J3DTexture(texture_num, texture_res);
    registerTextureImages(texture_res, texture_num);
}

void J3DModelLoader_v26::readMaterialTable(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    if (i_block->mpNameTable != 0) {
        mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    const u32 materialIDs = allocMaterialIDs(mpMaterialTable->mMaterialNum);
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        // Bit 31: these materials never share a packet.
        mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = J3DDiffFlag_Changed | (materialIDs + factory.getMaterialID(i));
    }
}

void J3DModelLoader::readTextureTable(J3DTextureBlock const* i_block) {
    ResTIMG* texture_res;
    u32 texture_num = i_block->mTextureNum;
    texture_res = JSUConvertOffsetToPtr< ResTIMG >(i_block, i_block->mpTextureRes);
    if (i_block->mpNameTable != 0) {
        mpMaterialTable->mTextureName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mTextureName = NULL;
    }
    mpMaterialTable->mTexture = new J3DTexture(texture_num, texture_res);
    registerTextureImages(texture_res, texture_num);
}

