#pragma once

#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DShapeMtxConcatView;
typedef void (J3DShapeMtxConcatView::*J3DShapeMtxConcatView_LoadFunc)(int, u16) const;

class J3DShapeMtxMulti : public J3DShapeMtx {
public:
    J3DShapeMtxMulti(u16 useMtxIndex, u16 useMtxNum, u16* useMtxIndexTable)
        : J3DShapeMtx(useMtxIndex), mUseMtxNum(useMtxNum), mUseMtxIndexTable(useMtxIndexTable) {
    }

    virtual ~J3DShapeMtxMulti() {
    }
    virtual u32 getType() const {
        return 'SMML';
    }
    virtual u16 getUseMtxNum() const {
        return mUseMtxNum;
    }
    virtual u16 getUseMtxIndex(u16 no) const {
        return mUseMtxIndexTable[no];
    }
    virtual void load() const;
    virtual void calcNBTScale(Vec const&, f32 (*)[3][3], f32 (*)[3][3]);

private:
    u16 mUseMtxNum;
    // In the file.
    u16* mUseMtxIndexTable;
};

class J3DShapeMtxConcatView : public J3DShapeMtx {
public:
    J3DShapeMtxConcatView(u16 useMtxIndex) : J3DShapeMtx(useMtxIndex) {
    }

    virtual ~J3DShapeMtxConcatView() {
    }
    virtual u32 getType() const {
        return 'SMCV';
    }
    virtual void load() const;
    virtual void loadNrmMtx(int, u16) const {
    }
    virtual void loadNrmMtx(int, u16, f32 (*)[4]) const;

    void loadMtxConcatView_PNGP(int, u16) const;
    // The model's block for this view (its matrices through the packet's base matrix, sent the first time they are
    // read after they changed); its id.
    static u32 loadPalette();
    // What a coordinate's own matrix needs from draw matrix `index` (a normal matrix as a texture matrix), for shapes
    // whose material asks.
    void loadTextureInputs(int slot, u16 index) const;

    static MtxPtr sMtxPtrTbl[2];
};

class J3DShapeMtxMultiConcatView : public J3DShapeMtxConcatView {
public:
    J3DShapeMtxMultiConcatView(u16 useMtxIndex, u16 useMtxNum, u16* useMtxIndexTable)
        : J3DShapeMtxConcatView(useMtxIndex), mUseMtxNum(useMtxNum), mUseMtxIndexTable(useMtxIndexTable) {
    }

    virtual ~J3DShapeMtxMultiConcatView() {
    }
    virtual u32 getType() const {
        return 'SMMC';
    }
    virtual u16 getUseMtxNum() const {
        return mUseMtxNum;
    }
    virtual u16 getUseMtxIndex(u16 no) const {
        return mUseMtxIndexTable[no];
    }
    virtual void load() const;
    virtual void loadNrmMtx(int, u16) const {
    }
    virtual void loadNrmMtx(int, u16, f32 (*)[4]) const;

private:
    u16 mUseMtxNum;
    // In the file.
    u16* mUseMtxIndexTable;
};

class J3DShapeMtxBBoardConcatView : public J3DShapeMtxConcatView {
public:
    J3DShapeMtxBBoardConcatView(u16 useMtxIndex) : J3DShapeMtxConcatView(useMtxIndex) {
    }

    virtual ~J3DShapeMtxBBoardConcatView() {
    }
    virtual u32 getType() const {
        return 'SMBB';
    }
    virtual void load() const;
};

class J3DShapeMtxYBBoardConcatView : public J3DShapeMtxConcatView {
public:
    J3DShapeMtxYBBoardConcatView(u16 useMtxIndex) : J3DShapeMtxConcatView(useMtxIndex) {
    }

    virtual ~J3DShapeMtxYBBoardConcatView() {
    }
    virtual u32 getType() const {
        return 'SMYB';
    }
    virtual void load() const;
};
