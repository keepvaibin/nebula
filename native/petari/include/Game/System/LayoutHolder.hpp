#pragma once

#include "Game/System/ResourceInfo.hpp"
#include <nw4r/lyt/resourceAccessor.h>

class AssetFolder;

class LayoutHolder : public nw4r::lyt::ResourceAccessor {
public:
    LayoutHolder(AssetFolder&);

    virtual ~LayoutHolder();
    virtual void* GetResource(u32, const char*, u32*);
    virtual nw4r::ut::Font* GetFont(const char*);
    virtual void* getResOther(const char* pName) const {
        return mResOther.getRes(pName);
    }
    virtual u32 getResOtherNum() const {
        return mResOther.mCount;
    }
    virtual const char* getResOtherName(u32 fileID) const {
        return mResOther.getResName(fileID);
    }
    virtual void* getResOther(u32 fileID) const {
        return mResOther.getRes(fileID);
    }
    virtual bool isExistResOther(const char* pName) const {
        return mResOther.isExistRes(pName);
    }

    bool isAnimationHashEqual(u32, u32) const;
    void initializeArc();
    u32 initEachResTable(ResTable*, const char* const*);
    u32 count(const char*, s32);
    void mount(s32);
    ResFileInfo* createAndRegisterObject(const char*, void*);

    AssetFolder* mArchive;
    ResTable mLayoutRes;
    ResTable mAnimRes;
    ResTable mResOther;
};
