#include "Game/Ui/Layout.hpp"
#include "Game/Ui/Material.hpp"
#include "Game/Ui/Window.hpp"

namespace Ui {
    namespace {
        // A vector's component by axis (0 x, 1 y).
        inline f32& Axis(Vec2f& rVec, int axis) {
            return (&rVec.x)[axis];
        }

        inline f32 Axis(const Vec2f& rVec, int axis) {
            return (&rVec.x)[axis];
        }

        struct TextureFlipInfo {
            u8 coords[4][2];
            u8 idx[2];
        };

        const TextureFlipInfo& GetTexutreFlipInfo(u8 textureFlip) {
            static TextureFlipInfo flipInfos[] = {
                {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {0, 1}},  // TEXTUREFLIP_NONE
                {{{1, 0}, {0, 0}, {1, 1}, {0, 1}}, {0, 1}},  // TEXTUREFLIP_FLIPH
                {{{0, 1}, {1, 1}, {0, 0}, {1, 0}}, {0, 1}},  // TEXTUREFLIP_FLIPV
                {{{0, 1}, {0, 0}, {1, 1}, {1, 0}}, {1, 0}},  // TEXTUREFLIP_ROTATE90
                {{{1, 1}, {0, 1}, {1, 0}, {0, 0}}, {0, 1}},  // TEXTUREFLIP_ROTATE180
                {{{1, 0}, {1, 1}, {0, 0}, {0, 1}}, {1, 0}},  // TEXTUREFLIP_ROTATE270
            };

            return flipInfos[textureFlip];
        }

        void GetLTFrameSize(Vec2f* pPoint, Size* pSize, const Vec2f& rBase, const Size& rWindowSize,
                            const WindowFrameSize& rFrameSize) {
            *pPoint = rBase;

            pSize->width = rWindowSize.width - rFrameSize.r;
            pSize->height = rFrameSize.t;
        }

        void GetLTTexCoord(Vec2f texCds[], const Size& polSize, const Size& texSize, u8 textureFlip) {
            const TextureFlipInfo& flipInfo = GetTexutreFlipInfo(textureFlip);
            const int ix = flipInfo.idx[0];
            const int iy = flipInfo.idx[1];
            const Vec2f tSz(texSize.width, texSize.height);

            Axis(texCds[0], ix) = Axis(texCds[2], ix) = flipInfo.coords[0][ix];
            Axis(texCds[0], iy) = Axis(texCds[1], iy) = flipInfo.coords[0][iy];

            Axis(texCds[3], ix) = Axis(texCds[1], ix) =
                polSize.width / ((flipInfo.coords[1][ix] - flipInfo.coords[0][ix]) * Axis(tSz, ix)) + flipInfo.coords[0][ix];
            Axis(texCds[3], iy) = Axis(texCds[2], iy) =
                polSize.height / ((flipInfo.coords[2][iy] - flipInfo.coords[0][iy]) * Axis(tSz, iy)) + flipInfo.coords[0][iy];
        }

        void GetRTFrameSize(Vec2f* pPoint, Size* pSize, const Vec2f& rBase, const Size& rWindowSize,
                            const WindowFrameSize& rFrameSize) {
            *pPoint = Vec2f(rBase.x + rWindowSize.width - rFrameSize.r, rBase.y);

            pSize->width = rFrameSize.r;
            pSize->height = rWindowSize.height - rFrameSize.b;
        }

        void GetRTTexCoord(Vec2f texCds[], const Size& polSize, const Size& texSize, u8 textureFlip) {
            const TextureFlipInfo& flipInfo = GetTexutreFlipInfo(textureFlip);
            const int ix = flipInfo.idx[0];
            const int iy = flipInfo.idx[1];
            const Vec2f tSz(texSize.width, texSize.height);

            Axis(texCds[1], ix) = Axis(texCds[3], ix) = flipInfo.coords[1][ix];
            Axis(texCds[1], iy) = Axis(texCds[0], iy) = flipInfo.coords[1][iy];

            Axis(texCds[2], ix) = Axis(texCds[0], ix) =
                polSize.width / ((flipInfo.coords[0][ix] - flipInfo.coords[1][ix]) * Axis(tSz, ix)) + flipInfo.coords[1][ix];
            Axis(texCds[2], iy) = Axis(texCds[3], iy) =
                polSize.height / ((flipInfo.coords[3][iy] - flipInfo.coords[1][iy]) * Axis(tSz, iy)) + flipInfo.coords[1][iy];
        }

        void GetLBFrameSize(Vec2f* pPoint, Size* pSize, const Vec2f& rBase, const Size& rWindowSize,
                            const WindowFrameSize& rFrameSize) {
            *pPoint = Vec2f(rBase.x, rBase.y + rFrameSize.t);

            pSize->width = rFrameSize.l;
            pSize->height = rWindowSize.height - rFrameSize.t;
        }

        void GetLBTexCoord(Vec2f texCds[], const Size& polSize, const Size& texSize, u8 textureFlip) {
            const TextureFlipInfo& flipInfo = GetTexutreFlipInfo(textureFlip);
            const int ix = flipInfo.idx[0];
            const int iy = flipInfo.idx[1];
            const Vec2f tSz(texSize.width, texSize.height);

            Axis(texCds[2], ix) = Axis(texCds[0], ix) = flipInfo.coords[2][ix];
            Axis(texCds[2], iy) = Axis(texCds[3], iy) = flipInfo.coords[2][iy];

            Axis(texCds[1], ix) = Axis(texCds[3], ix) =
                polSize.width / ((flipInfo.coords[3][ix] - flipInfo.coords[2][ix]) * Axis(tSz, ix)) + flipInfo.coords[2][ix];
            Axis(texCds[1], iy) = Axis(texCds[0], iy) =
                polSize.height / ((flipInfo.coords[0][iy] - flipInfo.coords[2][iy]) * Axis(tSz, iy)) + flipInfo.coords[2][iy];
        }

        void GetRBFrameSize(Vec2f* pPoint, Size* pSize, const Vec2f& rBase, const Size& rWindowSize,
                            const WindowFrameSize& rFrameSize) {
            *pPoint = Vec2f(rBase.x + rFrameSize.l, rBase.y + rWindowSize.height - rFrameSize.b);

            pSize->width = rWindowSize.width - rFrameSize.l;
            pSize->height = rFrameSize.b;
        }

        void GetRBTexCoord(Vec2f texCds[], const Size& polSize, const Size& texSize, u8 textureFlip) {
            const TextureFlipInfo& flipInfo = GetTexutreFlipInfo(textureFlip);
            const int ix = flipInfo.idx[0];
            const int iy = flipInfo.idx[1];
            const Vec2f tSz(texSize.width, texSize.height);

            Axis(texCds[3], ix) = Axis(texCds[1], ix) = flipInfo.coords[3][ix];
            Axis(texCds[3], iy) = Axis(texCds[2], iy) = flipInfo.coords[3][iy];

            Axis(texCds[0], ix) = Axis(texCds[2], ix) =
                polSize.width / ((flipInfo.coords[2][ix] - flipInfo.coords[3][ix]) * Axis(tSz, ix)) + flipInfo.coords[3][ix];
            Axis(texCds[0], iy) = Axis(texCds[1], iy) =
                polSize.height / ((flipInfo.coords[1][iy] - flipInfo.coords[3][iy]) * Axis(tSz, iy)) + flipInfo.coords[3][iy];
        }

    };  // namespace


    Window::Window(Layout& rLayout, const UiPaneData* pRes, const ResBlockSet& rBlockSet) : Pane(rLayout, pRes, rBlockSet) {
        const UiWindowData* const pWnd = rBlockSet.At< UiWindowData >(pRes->mPayload);
        mContentInflation.l = pWnd->mInflation[0];
        mContentInflation.r = pWnd->mInflation[1];
        mContentInflation.t = pWnd->mInflation[2];
        mContentInflation.b = pWnd->mInflation[3];

        const UiPictureData* const pResContent = rBlockSet.At< UiPictureData >(pWnd->mContent);

        for (int i = 0; i < 4; i++) {
            mContent.vtxColors[i] = detail::RecordColor(pResContent->mVertexColors[i]);
        }

        if (pResContent->mCoordCount > 0) {
            const u8 num = pResContent->mCoordCount;
            mContent.texCoordAry.Reserve(num);

            if (!mContent.texCoordAry.IsEmpty()) {
                mContent.texCoordAry.Copy(rBlockSet.At< u8 >(pResContent->mCoords), num);
            }
        }

        mpMaterial = rLayout.NewInArena< Material >(rBlockSet.MaterialAt(pResContent->mMaterial), rBlockSet);

        // The frames and their materials are the layout's, like the window.
        mFrameNum = pWnd->mFrameCount;
        mFrames = NULL;
        if (mFrameNum > 0) {
            mFrames = static_cast< Frame* >(rLayout.ArenaAlloc(mFrameNum * sizeof(Frame)));
            const UiWindowFrameData* const pResFrames = rBlockSet.At< UiWindowFrameData >(pWnd->mFrames);
            for (int i = 0; i < mFrameNum; i++) {
                new (&mFrames[i]) Frame();
                mFrames[i].textureFlip = pResFrames[i].mFlip;
                mFrames[i].pMaterial = rLayout.NewInArena< Material >(rBlockSet.MaterialAt(pResFrames[i].mMaterial), rBlockSet);
            }
        }
    }

    Window::~Window() {
        mContent.texCoordAry.Free();
    }

    Material* Window::FindMaterialByName(const char* pName, bool recursive) {
        if (mpMaterial != NULL && detail::EqualsMaterialName(mpMaterial->GetName(), pName)) {
            return mpMaterial;
        }

        for (int i = 0; i < mFrameNum; i++) {
            if (detail::EqualsMaterialName(mFrames[i].pMaterial->GetName(), pName)) {
                return mFrames[i].pMaterial;
            }
        }

        if (recursive) {
            for (Pane* pChild : mChildList) {
                if (Material* pMat = pChild->FindMaterialByName(pName, true)) {
                    return pMat;
                }
            }
        }

        return NULL;
    }

    void Window::SetAnimationEnable(AnimTransform* pAnimTrans, bool enable, bool recursive) {
        for (int i = 0; i < mFrameNum; i++) {
            mFrames[i].pMaterial->SetAnimationEnable(pAnimTrans, enable);
        }

        Pane::SetAnimationEnable(pAnimTrans, enable, recursive);
    }

    const Color8 Window::GetVtxColor(u32 idx) const {
        return mContent.vtxColors[idx];
    }

    void Window::SetVtxColor(u32 idx, Color8 color) {
        mContent.vtxColors[idx] = color;
    }

    u8 Window::GetVtxColorElement(u32 idx) const {
        return detail::GetVtxColorElement(mContent.vtxColors, idx);
    }

    void Window::SetVtxColorElement(u32 idx, u8 value) {
        detail::SetVtxColorElement(mContent.vtxColors, idx, value);
    }

    void Window::DrawSelf(const DrawInfo& rInfo) {
        LoadMtx(rInfo);

        const WindowFrameSize size = GetFrameSize(mFrameNum, mFrames);
        const Vec2f base = GetVtxPos();
        DrawContent(base, size, mGlbAlpha);

        switch (mFrameNum) {
        case 1: {
            DrawFrame(base, mFrames[0], size, mGlbAlpha);
            break;
        }

        case 4: {
            DrawFrame4(base, mFrames, size, mGlbAlpha);
            break;
        }

        case 8: {
            DrawFrame8(base, mFrames, size, mGlbAlpha);
            break;
        }
        }
    }

    void Window::AnimateSelf(u32 option) {
        Pane::AnimateSelf(option);

        if (IsVisible() || !(option & 1)) {
            for (int i = 0; i < mFrameNum; i++) {
                mFrames[i].pMaterial->Animate();
            }
        }
    }

    void Window::UnbindAnimationSelf(AnimTransform* pAnimTrans) {
        for (int i = 0; i < mFrameNum; i++) {
            mFrames[i].pMaterial->UnbindAnimation(pAnimTrans);
        }

        Pane::UnbindAnimationSelf(pAnimTrans);
    }

    void Window::DrawContent(const Vec2f& rBase, const WindowFrameSize& rFrameSize, u8 alpha) {
        bool useVtxColor = mpMaterial->Setup(detail::IsModulateVertexColor(mContent.vtxColors, alpha), alpha);

        detail::DrawQuad(Vec2f(rBase.x + rFrameSize.l - mContentInflation.l, rBase.y + rFrameSize.t - mContentInflation.t),

                         Size(GetDrawSize().width - rFrameSize.l + mContentInflation.l - rFrameSize.r + mContentInflation.r,
                              GetDrawSize().height - rFrameSize.t + mContentInflation.t - rFrameSize.b + mContentInflation.b),

                         mContent.texCoordAry.GetSize(), mContent.texCoordAry.GetArray(),

                         useVtxColor ? mContent.vtxColors : NULL, alpha);
    }

    void Window::DrawFrame(const Vec2f& basePt, const Frame& frame, const WindowFrameSize& frameSize, u8 alpha) {
        if (frame.pMaterial->GetTextureNum() == 0) {
            return;
        }

        const u8 texCoordNum = 1;

        const bool bUseVtxCol = frame.pMaterial->Setup(detail::IsModulateVertexColor(NULL, alpha), alpha);

        const Size texSize = detail::GetTextureSize(frame.pMaterial, 0);
        Color8 vtxColors[4];
        Vec2f texCds[texCoordNum][4];
        Vec2f polPt;
        Size polSize;

        GetLTFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
        GetLTTexCoord(texCds[0], polSize, texSize, 0);
        detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);

        GetRTFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
        GetRTTexCoord(texCds[0], polSize, texSize, 1);
        detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);

        GetRBFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
        GetRBTexCoord(texCds[0], polSize, texSize, 4);
        detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);

        GetLBFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
        GetLBTexCoord(texCds[0], polSize, texSize, 2);
        detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
    }

    void Window::DrawFrame4(const Vec2f& basePt, const Frame* frames, const WindowFrameSize& frameSize, u8 alpha) {
        const u8 texCoordNum = 1;

        Color8 vtxColors[4];
        Vec2f texCds[texCoordNum][4];
        Vec2f polPt;
        Size polSize;
        const bool bModVtxCol = detail::IsModulateVertexColor(NULL, alpha);

        if (frames[0].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[0].pMaterial->Setup(bModVtxCol, alpha);
            GetLTFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
            GetLTTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[0].pMaterial, 0), frames[0].textureFlip);
            detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[1].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[1].pMaterial->Setup(bModVtxCol, alpha);
            GetRTFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
            GetRTTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[1].pMaterial, 0), frames[1].textureFlip);
            detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[3].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[3].pMaterial->Setup(bModVtxCol, alpha);
            GetRBFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
            GetRBTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[3].pMaterial, 0), frames[3].textureFlip);
            detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[2].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[2].pMaterial->Setup(bModVtxCol, alpha);
            GetLBFrameSize(&polPt, &polSize, basePt, GetDrawSize(), frameSize);
            GetLBTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[2].pMaterial, 0), frames[2].textureFlip);
            detail::DrawQuad(polPt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }
    }

    void Window::DrawFrame8(const Vec2f& basePt, const Frame* frames, const WindowFrameSize& frameSize, u8 alpha) {
        const u8 texCoordNum = 1;
        Color8 vtxColors[4];
        Vec2f texCds[texCoordNum][4];
        Size polSize;
        const bool bModVtxCol = detail::IsModulateVertexColor(NULL, alpha);

        if (frames[0].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[0].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(frameSize.l, frameSize.t);
            GetLTTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[0].pMaterial, 0), frames[0].textureFlip);
            detail::DrawQuad(basePt, polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[6].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[6].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(GetDrawSize().width - frameSize.l - frameSize.r, frameSize.t);
            GetLTTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[6].pMaterial, 0), frames[6].textureFlip);
            detail::DrawQuad(Vec2f(basePt.x + frameSize.l, basePt.y), polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[1].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[1].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(frameSize.r, frameSize.t);
            GetRTTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[1].pMaterial, 0), frames[1].textureFlip);
            detail::DrawQuad(Vec2f(basePt.x + GetDrawSize().width - frameSize.r, basePt.y), polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0,
                             alpha);
        }

        if (frames[5].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[5].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(frameSize.r, GetDrawSize().height - frameSize.t - frameSize.b);
            GetRTTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[5].pMaterial, 0), frames[5].textureFlip);
            detail::DrawQuad(Vec2f(basePt.x + GetDrawSize().width - frameSize.r, basePt.y + frameSize.t), polSize, texCoordNum, texCds,
                             bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[3].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[3].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(frameSize.r, frameSize.b);
            GetRBTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[3].pMaterial, 0), frames[3].textureFlip);
            detail::DrawQuad(Vec2f(basePt.x + GetDrawSize().width - frameSize.r, basePt.y + GetDrawSize().height - frameSize.b), polSize, texCoordNum,
                             texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[7].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[7].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(GetDrawSize().width - frameSize.l - frameSize.r, frameSize.b);
            GetRBTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[7].pMaterial, 0), frames[7].textureFlip);
            detail::DrawQuad(Vec2f(basePt.x + frameSize.l, basePt.y + GetDrawSize().height - frameSize.b), polSize, texCoordNum, texCds,
                             bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[2].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[2].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(frameSize.l, frameSize.b);
            GetLBTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[2].pMaterial, 0), frames[2].textureFlip);
            detail::DrawQuad(Vec2f(basePt.x, basePt.y + GetDrawSize().height - frameSize.b), polSize, texCoordNum, texCds,
                             bUseVtxCol ? vtxColors : 0, alpha);
        }

        if (frames[4].pMaterial->GetTextureNum() > 0) {
            const bool bUseVtxCol = frames[4].pMaterial->Setup(bModVtxCol, alpha);
            polSize = Size(frameSize.l, GetDrawSize().height - frameSize.t - frameSize.b);
            GetLBTexCoord(texCds[0], polSize, detail::GetTextureSize(frames[4].pMaterial, 0), frames[4].textureFlip);
            detail::DrawQuad(Vec2f(basePt.x, basePt.y + frameSize.t), polSize, texCoordNum, texCds, bUseVtxCol ? vtxColors : 0, alpha);
        }
    }

    const WindowFrameSize Window::GetFrameSize(u8 frameNum, const Frame* pFrames) {
        WindowFrameSize frameSize = {0.0f, 0.0f, 0.0f, 0.0f};

        switch (frameNum) {
        case 1: {
            Size texSize = detail::GetTextureSize(pFrames[0].pMaterial, 0);

            frameSize.l = texSize.width;
            frameSize.t = texSize.height;

            frameSize.r = texSize.width;
            frameSize.b = texSize.height;
            break;
        }

        case 4:
        case 8: {
            Size texSize = detail::GetTextureSize(pFrames[0].pMaterial, 0);

            frameSize.l = texSize.width;
            frameSize.t = texSize.height;

            texSize = detail::GetTextureSize(pFrames[3].pMaterial, 0);

            frameSize.r = texSize.width;
            frameSize.b = texSize.height;
            break;
        }
        }

        return frameSize;
    }

    Material* Window::GetFrameMaterial(u32 idx) const {
        if (idx >= mFrameNum) {
            return NULL;
        }

        return mFrames[idx].pMaterial;
    }

    Material* Window::GetContentMaterial() const {
        return GetMaterial();
    }
}  // namespace Ui
