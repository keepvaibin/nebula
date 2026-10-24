#include "Game/Screen/NebulaPaneKit.hpp"

#ifdef TARGET_PC
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/System/LayoutHolder.hpp"
#include <cstring>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/resources.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>

NebulaPaneKit::NebulaPaneKit() : mLayout(nullptr) {
    memset(&mBlocks, 0, sizeof(mBlocks));
}

// As Layout::Build reads it: the texture, font and material lists every pane block refers to.
bool NebulaPaneKit::init(LayoutHolder* pHolder) {
    mLayout = nullptr;
    if (pHolder == nullptr || pHolder->mLayoutRes.mCount == 0) {
        return false;
    }
    const void* pLayout = pHolder->mLayoutRes.getRes(0u);
    const nw4r::lyt::res::BinaryFileHeader* pHeader = static_cast< const nw4r::lyt::res::BinaryFileHeader* >(pLayout);
    if (pLayout == nullptr || !nw4r::lyt::detail::TestFileHeader(*pHeader, 'RLYT')) {
        return false;
    }
    memset(&mBlocks, 0, sizeof(mBlocks));
    mBlocks.pResAccessor = pHolder;
    const u8* pBlock = static_cast< const u8* >(pLayout) + pHeader->headerSize;
    for (s32 i = 0; i < pHeader->dataBlocks; i++) {
        const nw4r::lyt::res::DataBlockHeader* pBlockHeader = reinterpret_cast< const nw4r::lyt::res::DataBlockHeader* >(pBlock);
        switch (nw4r::lyt::detail::GetSignatureInt(pBlockHeader->kind)) {
        case 'txl1':
            mBlocks.pTextureList = reinterpret_cast< const nw4r::lyt::res::TextureList* >(pBlock);
            break;
        case 'fnl1':
            mBlocks.pFontList = reinterpret_cast< const nw4r::lyt::res::FontList* >(pBlock);
            break;
        case 'mat1':
            mBlocks.pMaterialList = reinterpret_cast< const nw4r::lyt::res::MaterialList* >(pBlock);
            break;
        default:
            break;
        }
        pBlock += pBlockHeader->size;
    }
    mLayout = pLayout;
    return true;
}

const void* NebulaPaneKit::findBlock(const char* pName, s32* pKind) const {
    if (mLayout == nullptr) {
        return nullptr;
    }
    const nw4r::lyt::res::BinaryFileHeader* pHeader = static_cast< const nw4r::lyt::res::BinaryFileHeader* >(mLayout);
    const u8* pBlock = static_cast< const u8* >(mLayout) + pHeader->headerSize;
    for (s32 i = 0; i < pHeader->dataBlocks; i++) {
        const nw4r::lyt::res::DataBlockHeader* pBlockHeader = reinterpret_cast< const nw4r::lyt::res::DataBlockHeader* >(pBlock);
        const s32 kind = nw4r::lyt::detail::GetSignatureInt(pBlockHeader->kind);
        if (kind == 'pan1' || kind == 'pic1' || kind == 'txt1' || kind == 'wnd1' || kind == 'bnd1') {
            const nw4r::lyt::res::Pane* pPane = reinterpret_cast< const nw4r::lyt::res::Pane* >(pBlock);
            if (strncmp(pPane->name, pName, sizeof(pPane->name)) == 0) {
                *pKind = kind;
                return pBlock;
            }
        }
        pBlock += pBlockHeader->size;
    }
    return nullptr;
}

nw4r::lyt::Pane* NebulaPaneKit::make(const char* pName, const char* pNewName) const {
    s32 kind = 0;
    const void* pBlock = findBlock(pName, &kind);
    if (pBlock == nullptr) {
        return nullptr;
    }
    nw4r::lyt::Pane* pPane = nw4r::lyt::Layout::BuildPaneObj(kind, pBlock, mBlocks);
    if (pPane != nullptr) {
        pPane->SetName(pNewName);
        pPane->mTranslate = nw4r::math::VEC3(0.0f, 0.0f, 0.0f);
        pPane->SetVisible(true);
        pPane->SetInfluencedAlpha(true);
        // Placed by its screen, which keeps its own 16:9 width (the authored location adjust would narrow it again).
        nw4r::lyt::detail::SetBit(&pPane->mFlag, 2, false);
    }
    return pPane;
}

nw4r::lyt::Window* NebulaPaneKit::makeWindow(const char* pName, const char* pNewName) const {
    nw4r::lyt::Pane* pPane = make(pName, pNewName);
    nw4r::lyt::Window* pWindow = pPane != nullptr ? nw4r::ut::DynamicCast< nw4r::lyt::Window* >(pPane) : nullptr;
    if (pPane != nullptr && pWindow == nullptr) {
        nw4r::lyt::Layout::DeleteObj(pPane);
    }
    return pWindow;
}

nw4r::lyt::TextBox* NebulaPaneKit::makeText(const char* pName, const char* pNewName, u32 length) const {
    nw4r::lyt::Pane* pPane = make(pName, pNewName);
    if (pPane == nullptr) {
        return nullptr;
    }
    if (!pPane->GetRuntimeTypeInfo()->IsDerivedFrom(&nw4r::lyt::TextBox::typeInfo)) {
        nw4r::lyt::Layout::DeleteObj(pPane);
        return nullptr;
    }
    nw4r::lyt::TextBox* pText = static_cast< nw4r::lyt::TextBox* >(pPane);
    LayoutCoreUtil::initTextBoxPane(pText, nullptr, length);
    LayoutCoreUtil::setTextBoxMessage(pText, L"");
    return pText;
}
#endif
