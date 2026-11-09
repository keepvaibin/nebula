#include "Game/Ui/Layout.hpp"
#include "Game/Ui/Picture.hpp"
#include "Game/Ui/UiInlines.hpp"

namespace Ui {
    Picture::Picture(Layout& rLayout, const TexMap& texMap) : Pane(rLayout) {
        mKind = cKind;
        const int texNum = 1;
        Init(texNum);
        mpMaterial = rLayout.NewInArena< Material >();
        mpMaterial->ReserveStorage(texNum, texNum, texNum, 0, false, false, false, false);
        Append(texMap);
    }

    Picture::Picture(Layout& rLayout, const UiPaneData* pRes, const ResBlockSet& rBlockSet) : Pane(rLayout, pRes, rBlockSet) {
        const UiPictureData* const pPic = rBlockSet.At< UiPictureData >(pRes->mPayload);
        const u8 num = pPic->mCoordCount;
        Init(num);

        for (int i = 0; i < 4; i++) {
            mVtxColors[i] = detail::RecordColor(pPic->mVertexColors[i]);
        }

        if (num > 0 && !mTexCoordAry.IsEmpty()) {
            mTexCoordAry.Copy(rBlockSet.At< u8 >(pPic->mCoords), num);
        }

        mpMaterial = rLayout.NewInArena< Material >(rBlockSet.MaterialAt(pPic->mMaterial), rBlockSet);
    }

    void Picture::Init(u8 num) {
        if (num > 0) {
            ReserveTexCoord(num);
        }
    }

    void Picture::ReserveTexCoord(u8 num) {
        mTexCoordAry.Reserve(num);
    }

    Picture::~Picture() {
        mTexCoordAry.Free();
    }

    void Picture::Append(const TexMap& rTexMap) {
        if (mpMaterial->GetTextureNum() >= mpMaterial->GetTextureCap() || mpMaterial->GetTextureNum() >= mpMaterial->GetTexCoordGenCap()) {
            return;
        }

        const u32 idx = mpMaterial->GetTextureNum();

        mpMaterial->SetTextureNum(idx + 1);
        mpMaterial->SetTexture(idx, rTexMap);

        mpMaterial->SetTexCoordGenNum(mpMaterial->GetTextureNum());
        mpMaterial->SetTexCoordGen(idx, TexCoordGen());

        SetTexCoordNum(static_cast< u8 >(mpMaterial->GetTextureNum()));

        if (mSize == Size(0.0f, 0.0f) && mpMaterial->GetTextureNum() == 1) {
            mSize = detail::GetTextureSize(mpMaterial, 0);
        }
    }

    void Picture::SetTexCoordNum(u8 num) {
        mTexCoordAry.SetSize(num);
    }

    const Color8 Picture::GetVtxColor(u32 idx) const {
        return mVtxColors[idx];
    }

    void Picture::SetVtxColor(u32 idx, Color8 color) {
        mVtxColors[idx] = color;
    }

    u8 Picture::GetVtxColorElement(u32 idx) const {
        return detail::GetVtxColorElement(mVtxColors, idx);
    }

    void Picture::SetVtxColorElement(u32 idx, u8 value) {
        detail::SetVtxColorElement(mVtxColors, idx, value);
    }

    void Picture::DrawSelf(const DrawInfo& rInfo) {
        if (mpMaterial == NULL) {
            return;
        }

        LoadMtx(rInfo);

        bool useVtxColor = mpMaterial->Setup(detail::IsModulateVertexColor(mVtxColors, mGlbAlpha), mGlbAlpha);

        detail::DrawQuad(GetVtxPos(), GetDrawSize(), mTexCoordAry.GetSize(), mTexCoordAry.GetArray(), useVtxColor ? mVtxColors : NULL, mGlbAlpha);
    }
}  // namespace Ui
