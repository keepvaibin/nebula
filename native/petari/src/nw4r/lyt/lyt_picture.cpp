#include "nw4r/lyt/layout.h"
#include "nw4r/lyt/picture.h"
#include "nw4r/ut/inlines.h"

namespace nw4r {
    namespace lyt {
        NW4R_UT_RTTI_DEF_DERIVED(Picture, Pane);

        Picture::Picture(u8 texNum) {
            Init(texNum);
            mpMaterial = Layout::NewObj< Material >();
            if (mpMaterial) {
                mpMaterial->ReserveStorage(texNum, texNum, texNum, 0, false, false, false, false);
            }
        }

        Picture::Picture(const TexMap& texMap) {
            const int texNum = 1;
            Init(texNum);
            mpMaterial = Layout::NewObj< Material >();
            if (mpMaterial) {
                mpMaterial->ReserveStorage(texNum, texNum, texNum, 0, false, false, false, false);
                Append(texMap);
            }
        }

        Picture::Picture(const UiPaneData* pRes, const ResBlockSet& rBlockSet) : Pane(pRes, rBlockSet) {
            const UiPictureData* const pPic = rBlockSet.At< UiPictureData >(pRes->mPayload);
            u8 num = ut::Min< u8 >(pPic->mCoordCount, 8);
            Init(num);

            for (int i = 0; i < 4; i++) {
                mVtxColors[i] = detail::RecordColor(pPic->mVertexColors[i]);
            }

            if (num > 0 && !mTexCoordAry.IsEmpty()) {
                mTexCoordAry.Copy(rBlockSet.At< u8 >(pPic->mCoords), num);
            }

            void* pMaterialBuf = Layout::AllocMemory(sizeof(Material));

            if (pMaterialBuf != NULL) {
                mpMaterial = new (pMaterialBuf) Material(rBlockSet.MaterialAt(pPic->mMaterial), rBlockSet);
            }
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
            if (mpMaterial != NULL && !mpMaterial->IsUserAllocated()) {
                mpMaterial->~Material();
                Layout::FreeMemory(mpMaterial);
                mpMaterial = NULL;
            }

            mTexCoordAry.Free();
        }

        void Picture::Append(const TexMap& rTexMap) {
            if (mpMaterial->GetTextureNum() >= mpMaterial->GetTextureCap() || mpMaterial->GetTextureNum() >= mpMaterial->GetTexCoordGenCap()) {
                return;
            }

            u8 idx = mpMaterial->GetTextureNum();

            mpMaterial->SetTextureNum(idx + 1);
            mpMaterial->SetTexture(idx, rTexMap);

            mpMaterial->SetTexCoordGenNum(mpMaterial->GetTextureNum());
            mpMaterial->SetTexCoordGen(idx, TexCoordGen());

            SetTexCoordNum(mpMaterial->GetTextureNum());

            if (mSize == Size(0.0f, 0.0f) && mpMaterial->GetTextureNum() == 1) {
                mSize = detail::GetTextureSize(mpMaterial, 0);
            }
        }

        void Picture::SetTexCoordNum(u8 num) {
            mTexCoordAry.SetSize(num);
        }

        const ut::Color Picture::GetVtxColor(u32 idx) const {
            return mVtxColors[idx];
        }

        void Picture::SetVtxColor(u32 idx, ut::Color color) {
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

            detail::DrawQuad(GetVtxPos(), mSize, mTexCoordAry.GetSize(), mTexCoordAry.GetArray(), useVtxColor ? mVtxColors : NULL, mGlbAlpha);
        }

    };  // namespace lyt
};  // namespace nw4r
