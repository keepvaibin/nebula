#include "Game/System/LayoutHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include <cstdio>
#include <cstring>

#include "Game/Util/StringUtil.hpp"

extern "C" {
int strncasecmp(const char* s1, const char* s2, size_t n);
}

namespace {
    const char* const sLayoutExt[] = {
        ".brlyt",
        nullptr,
    };
    const char* const sAnimationExt[] = {
        ".brlan",
        nullptr,
    };
};  // namespace

LayoutHolder::LayoutHolder(AssetFolder& rArchive) : nw4r::lyt::ResourceAccessor(), mArchive(&rArchive) {
    initializeArc();
}

LayoutHolder::~LayoutHolder() {
}

void* LayoutHolder::GetResource(u32 resourceKind, const char* pName, u32* pResourceInfo) {
    void* pResource = nullptr;

    switch (resourceKind) {
    case 'blyt':
        pResource = mLayoutRes.getRes(pName);
        break;
    case 'anim':
        pResource = mAnimRes.getRes(pName);
        break;
    default:

        if (strstr(pName, ".brfnt") == nullptr) {
            pResource = mResOther.getRes(pName);
        }

        break;
    }

    if (pResourceInfo != nullptr) {
        if (pResource != nullptr) {
            *pResourceInfo = static_cast< u32* >(pResource)[1];
        } else {
            *pResourceInfo = 0;
        }
    }

    return pResource;
}

nw4r::ut::Font* LayoutHolder::GetFont(const char* pName) {
    if (!MR::strcasecmp(pName, "MessageFont26.brfnt")) {
        return MR::getFontOnCurrentLanguage();
    }

    if (!MR::strcasecmp(pName, "MenuFont64.brfnt")) {
        return MR::getMenuFontNW4R();
    }

    if (!MR::strcasecmp(pName, "NumberFont.brfnt")) {
        return MR::getNumberFontNW4R();
    }

    if (!MR::strcasecmp(pName, "PictureFont.brfnt")) {
        return MR::getPictureFontNW4R();
    }

    if (MR::strcasecmp(pName, "CinemaFont26.brfnt") == 0) {
        return MR::getCinemaFontNW4R();
    }

    return MR::getFontOnCurrentLanguage();
}

bool LayoutHolder::isAnimationHashEqual(u32 hash, u32 fileID) const {
    return mAnimRes.getFileInfo(fileID)->isEqualHashCode(hash);
}

void LayoutHolder::initializeArc() {
    u32 resCount = mArchive->countResource();
    resCount -= initEachResTable(&mLayoutRes, ::sLayoutExt);
    resCount -= initEachResTable(&mAnimRes, ::sAnimationExt);

    if (resCount > 0) {
        mResOther.newFileInfoTable(resCount);
    }

    mount(mArchive->getEntry());
}

u32 LayoutHolder::initEachResTable(ResTable* pResTable, const char* const* pExtensionTable) {
    u32 resCount = 0;

    for (s32 i = 0; pExtensionTable[i] != nullptr; i++) {
        resCount += count(pExtensionTable[i], mArchive->getEntry());
    }

    if (resCount > 0) {
        pResTable->newFileInfoTable(resCount);
    }

    return resCount;
}

u32 LayoutHolder::count(const char* pExtension, s32 folder) {
    u32 resCount = 0;
    AssetFolder::Item item;

    for (u32 i = 0; mArchive->getItem(folder, i, &item); i++) {
        if (item.mIsFolder) {
            resCount += count(pExtension, item.mEntry);
        } else if (pExtension == nullptr || strstr(item.mName, pExtension) != nullptr) {
            resCount++;
        }
    }

    return resCount;
}

void LayoutHolder::mount(s32 folder) {
    AssetFolder::Item item;

    for (u32 i = 0; mArchive->getItem(folder, i, &item); i++) {
        if (item.mIsFolder) {
            mount(item.mEntry);
        } else {
            ResFileInfo* pInfo = createAndRegisterObject(item.mName, item.mData);
            pInfo->_8 = item.mData;
            pInfo->_4 = item.mSize;
        }
    }
}

ResFileInfo* LayoutHolder::createAndRegisterObject(const char* pName, void* pResource) {
    if (strstr(pName, ".brlyt") != nullptr) {
        return mLayoutRes.add(pName, pResource, false);
    }

    if (strstr(pName, ".brlan") != nullptr) {
        return mAnimRes.add(pName, pResource, false);
    }

    return mResOther.add(pName, pResource, false);
}
