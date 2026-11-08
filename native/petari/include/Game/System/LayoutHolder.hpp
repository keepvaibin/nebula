#pragma once

#include "Game/System/ResourceInfo.hpp"
#include <Game/Ui/ResourceAccessor.hpp>

class AssetFolder;

class LayoutHolder : public Ui::ResourceAccessor {
public:
    LayoutHolder(AssetFolder&);

    virtual void* GetResource(u32, const char*, u32*);
    virtual Ui::Font* GetFont(const char*);
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
    // The table a NEBULA_ASSET_KIND_* goes in.
    ResTable& tableOf(u32);
    // The files below a folder, by kind (NEBULA_ASSET_KIND_COUNT counts).
    void count(s32, u32*) const;
    void mount(s32);

    AssetFolder* mArchive;
    ResTable mLayoutRes;
    ResTable mAnimRes;
    ResTable mResOther;
};
