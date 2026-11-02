#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/System/FileLoader.hpp"
#include "Game/System/Language.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/AssetFolder.hpp"
#include <cstdio>
#include <nw4r/ut/ResFont.h>

GameSystemFontHolder::GameSystemFontHolder() : _4(), mEmbeddedMessageFont(), mMessageFont(), mPictureFont(), mMenuFont(), mNumberFont() {
}

nw4r::ut::Font* GameSystemFontHolder::getMessageFont() const {
    if (mEmbeddedMessageFont != nullptr) {
        return mEmbeddedMessageFont;
    }

    return mMessageFont;
}

void GameSystemFontHolder::createFontFromEmbeddedData() {
    AssetFolder* pArchive = nullptr;
    NebulaArena* pHeap = nullptr;

    MR::getMountedArchiveAndHeap("ErrorMessageArchive.arc", &pArchive, &pHeap);

    const char* embeddedFontPath = "/LayoutData/EmbeddedFont.arc";

    _4 = pArchive->getSubFolder(embeddedFontPath, pHeap);
    mEmbeddedMessageFont = new (pHeap, -4) nw4r::ut::ResFont();
    mEmbeddedMessageFont->SetResource(_4->getResource("MessageFont26.brfnt"));
}

namespace {
    inline nw4r::ut::ResFont* createFont(AssetFolder* pArchive, const char* pName) {
        nw4r::ut::ResFont* pFont = new nw4r::ut::ResFont();
        pFont->SetResource(pArchive->getResource(pName));
        return pFont;
    }
};  // namespace

void GameSystemFontHolder::createFontFromFile() {
    AssetFolder* pArchive = MR::receiveArchive("/LayoutData/Font.arc");

    mMessageFont = createFont(pArchive, "/MessageFont26.brfnt");

    mPictureFont = createFont(pArchive, "/PictureFont.brfnt");

    mMenuFont = createFont(pArchive, "/MenuFont64.brfnt");

    mNumberFont = createFont(pArchive, "/NumberFont.brfnt");

    mCinemaFont = createFont(pArchive, "/CinemaFont26.brfnt");

    mMessageFont->SetAlternateChar('?');
    mPictureFont->SetAlternateChar('?');
    mMenuFont->SetAlternateChar('?');
    mNumberFont->SetAlternateChar('?');
    mCinemaFont->SetAlternateChar('?');

    if (mEmbeddedMessageFont != nullptr) {
        mEmbeddedMessageFont->RemoveResource();

        delete mEmbeddedMessageFont;
        delete _4;

        mEmbeddedMessageFont = nullptr;
    }
}
