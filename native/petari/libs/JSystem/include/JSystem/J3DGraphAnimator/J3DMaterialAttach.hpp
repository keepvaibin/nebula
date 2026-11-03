#pragma once


class J3DAnmColor;
class J3DAnmTevRegKey;
class J3DAnmTexPattern;
class J3DAnmTextureSRTKey;
class J3DMaterial;
class J3DTexture;
class JUTNameTab;

class J3DMaterialTable {
public:
    J3DMaterialTable();

    virtual ~J3DMaterialTable();

    void clear();
    int removeMatColorAnimator(J3DAnmColor*);
    int removeTexNoAnimator(J3DAnmTexPattern*);
    int removeTexMtxAnimator(J3DAnmTextureSRTKey*);
    int removeTevRegAnimator(J3DAnmTevRegKey*);
    int createTexMtxForAnimator(J3DAnmTextureSRTKey*);
    int entryMatColorAnimator(J3DAnmColor*);
    int entryTexNoAnimator(J3DAnmTexPattern*);
    int entryTexMtxAnimator(J3DAnmTextureSRTKey*);
    int entryTevRegAnimator(J3DAnmTevRegKey*);

    u16 getMaterialNum() const {
        return mMaterialNum;
    }
    J3DTexture* getTexture() const {
        return mTexture;
    }
    JUTNameTab* getTextureName() const {
        return mTextureName;
    }
    JUTNameTab* getMaterialName() const {
        return mMaterialName;
    }
    bool isLocked() const {
        return field_0x1c == 1;
    }
    J3DMaterial* getMaterialNodePointer(u16 idx) const {
        return mMaterialNodePointer[idx];
    }

    u16 mMaterialNum;
    u16 mUniqueMatNum;
    J3DMaterial** mMaterialNodePointer;
    JUTNameTab* mMaterialName;
    J3DMaterial* field_0x10;
    J3DTexture* mTexture;
    JUTNameTab* mTextureName;
    u16 field_0x1c;
};
