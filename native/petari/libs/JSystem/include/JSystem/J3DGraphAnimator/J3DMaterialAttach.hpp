#pragma once


class J3DMaterial;
class J3DTexture;
class JUTNameTab;

class J3DMaterialTable {
public:
    J3DMaterialTable();

    virtual ~J3DMaterialTable();

    void clear();

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
    J3DMaterial* getMaterialNodePointer(u16 idx) const {
        return mMaterialNodePointer[idx];
    }

    u16 mMaterialNum;
    u16 mUniqueMatNum;
    J3DMaterial** mMaterialNodePointer;
    JUTNameTab* mMaterialName;
    J3DTexture* mTexture;
    JUTNameTab* mTextureName;
};
