#include "Game/Ui/Animation.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/DrawInfo.hpp"
#include "Game/Ui/Layout.hpp"
#include "Game/Ui/Material.hpp"
#include "Game/Ui/Pane.hpp"
#include <cstdio>
#include <cstring>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_CalculateMtxSelf_f1, "CalculateMtxSelf.f1", 255.0f);

namespace Ui {
    void ReverseYAxis(Mtx34f* pMtx) {
        pMtx->mMtx[0][1] = -pMtx->mMtx[0][1];
        pMtx->mMtx[1][1] = -pMtx->mMtx[1][1];
        pMtx->mMtx[2][1] = -pMtx->mMtx[2][1];
    }
}  // namespace Ui

namespace Ui {
    namespace {
        bool SameBits(const f32* pA, const f32* pB, u32 num) {
            for (u32 i = 0; i < num; i++) {
                if (__builtin_bit_cast(u32, pA[i]) != __builtin_bit_cast(u32, pB[i])) {
                    return false;
                }
            }
            return true;
        }
    }  // namespace

    // A pane made in code (Nebula's own panes, Picture(const TexMap&)): at the origin, unscaled, visible.
    Pane::Pane(Layout& rLayout) {
        Init();
        rLayout.Join(this);
        mBasePosition = 4;
        memset(mUserData, 0, sizeof(mUserData));
        mTranslate = Vec3f(0.0f, 0.0f, 0.0f);
        mRotate = Vec3f(0.0f, 0.0f, 0.0f);
        mScale = Vec2f(1.0f, 1.0f);
        mSize = Size(0.0f, 0.0f);
        mAlpha = 255;
        mGlbAlpha = mAlpha;
        mFlag = 0;
        SetVisible(true);
    }

    Pane::Pane(Layout& rLayout, const UiPaneData* pRes, const ResBlockSet& rBlockSet) {
        Init();
        rLayout.Join(this);
        mKind = pRes->mKind;

        mBasePosition = pRes->mBasePosition;

        SetName(rBlockSet.String(pRes->mName));
        memcpy(mUserData, pRes->mUserData, sizeof(pRes->mUserData));
        mUserData[sizeof(pRes->mUserData)] = '\0';

        mTranslate = Vec3f(pRes->mTranslate[0], pRes->mTranslate[1], pRes->mTranslate[2]);
        mRotate = Vec3f(pRes->mRotate[0], pRes->mRotate[1], pRes->mRotate[2]);
        mScale = Vec2f(pRes->mScale[0], pRes->mScale[1]);
        mSize = Size(pRes->mSize[0], pRes->mSize[1]);

        mAlpha = pRes->mAlpha;
        mGlbAlpha = mAlpha;
        mFlag = 0;
        SetVisible(pRes->mVisible != 0);
        SetInfluencedAlpha(pRes->mInfluencedAlpha != 0);
        detail::SetBit(&mFlag, 2, pRes->mLocationAdjust != 0);
    }

    // Its children and its material are the layout's too: ~Layout destroys each of them on its own.
    Pane::~Pane() {
        UnbindAnimationSelf(0);
    }

    void Pane::Init() {
        mKind = UiPaneKind_Pane;
        mpParent = NULL;
        mpMaterial = NULL;
    }

    void Pane::SetName(const char* pName) {
        mName.set(pName);
        mpLayout->TouchStructure();
    }

    void Pane::AppendChild(Pane* pChild) {
        mChildList.PushBack(pChild);
        pChild->mpParent = this;
        // Its world was built from another parent's.
        pChild->mBuiltParentVersion = cNoVersion;
        mpLayout->TouchStructure();
    }

    void Pane::RemoveChild(Pane* pChild) {
        mChildList.Erase(pChild);
        pChild->mpParent = nullptr;
        pChild->mBuiltParentVersion = cNoVersion;
        mpLayout->TouchStructure();
    }

    const Color8 Pane::GetVtxColor(u32 /*idx*/) const {
        return -1;
    }

    void Pane::SetVtxColor(u32 /*idx*/, Color8 /*color*/) {
    }

    u8 Pane::GetColorElement(u32 idx) const {
        switch (idx) {
        case 0x10: {
            return mAlpha;
        }

        default: {
            return GetVtxColorElement(idx);
        }
        }
    }

    void Pane::SetColorElement(u32 idx, u8 value) {
        switch (idx) {
        case 0x10: {
            mAlpha = value;
            break;
        }

        default: {
            SetVtxColorElement(idx, value);
            break;
        }
        }
    }

    u8 Pane::GetVtxColorElement(u32 /*idx*/) const {
        return 0xFF;
    }

    void Pane::SetVtxColorElement(u32 /*idx*/, u8 /*value*/) {
    }

    Pane* Pane::FindPaneByName(const char* pName, bool recursive) {
        if (detail::EqualsResName(mName, pName)) {
            return this;
        }

        if (recursive) {
            for (Pane* pChild : mChildList) {
                if (Pane* pPane = pChild->FindPaneByName(pName, true)) {
                    return pPane;
                }
            }
        }

        return nullptr;
    }

    Material* Pane::FindMaterialByName(const char* pName, bool recursive) {
        if (mpMaterial != NULL && detail::EqualsMaterialName(mpMaterial->GetName(), pName)) {
            return mpMaterial;
        }

        if (recursive) {
            for (Pane* pChild : mChildList) {
                if (Material* pMat = pChild->FindMaterialByName(pName, true)) {
                    return pMat;
                }
            }
        }

        return nullptr;
    }

    void Pane::CalculateMtxSelf(const DrawInfo& rInfo, f32& rContextAlpha, bool& rContextInfluenced, u32 rootVersion,
                                bool resetScreen) {
        Vec2f scale = mScale;
        if (rInfo.IsLocationAdjust() && IsLocationAdjust()) {
            scale.x *= rInfo.GetLocationAdjustScale().x;
            scale.y *= rInfo.GetLocationAdjustScale().y;
        }

        const bool localChanged = !mLocalBuilt || !SameBits(&mTranslate.x, &mBuiltTranslate.x, 3) ||
                                  !SameBits(&mRotate.x, &mBuiltRotate.x, 3) || !SameBits(&scale.x, &mBuiltScale.x, 2);
        if (localChanged) {
            Mtx34f mtx1, mtx2;
            Mtx34f rotateMtx;

            NebulaMtxScale(mtx2, scale.x, scale.y, 1.0f);

            NebulaMtxRotRad(rotateMtx, 'x', detail::DegToRad(mRotate.x));
            NebulaMtxConcat(rotateMtx, mtx2, mtx1);

            NebulaMtxRotRad(rotateMtx, 'y', detail::DegToRad(mRotate.y));
            NebulaMtxConcat(rotateMtx, mtx1, mtx2);

            NebulaMtxRotRad(rotateMtx, 'z', detail::DegToRad(mRotate.z));
            NebulaMtxConcat(rotateMtx, mtx2, mtx1);

            NebulaMtxTransApply(mtx1, mMtx, mTranslate.x, mTranslate.y, mTranslate.z);
            mBuiltTranslate = mTranslate;
            mBuiltRotate = mRotate;
            mBuiltScale = scale;
            mLocalBuilt = true;
        }

        // A follow places the pane anew each pass; the world's version moves only when its bits do, so the
        // children rebuild only then.
        const u32 parentVersion = mpParent != NULL ? mpParent->mWorldVersion : rootVersion;
        bool worldChanged = false;
        if (localChanged || parentVersion != mBuiltParentVersion || mpFollow != nullptr) {
            Mtx34f world;
            if (mpParent != NULL) {
                world.concat(mpParent->mWorldMtx, mMtx);
            } else if (rInfo.IsMultipleViewMtxOnDraw()) {
                world = mMtx;
            } else {
                world.concat(rInfo.GetViewMtx(), mMtx);
            }
            if (mpFollow != nullptr) {
                mpFollow->Follow(*this, world);
            }
            if (mWorldVersion == 0 || !SameBits(&world.mMtx[0][0], &mWorldMtx.mMtx[0][0], 12)) {
                mWorldMtx = world;
                mWorldVersion = mWorldVersion + 1 == cNoVersion ? 1 : mWorldVersion + 1;
                worldChanged = true;
            }
            mBuiltParentVersion = parentVersion;
        }

        if (worldChanged || resetScreen || !SameBits(&mScreenScale, &mBuiltScreenScale, 1)) {
            mGlbMtx = mWorldMtx;
            if (mScreenScale != 1.0f) {
                for (s32 row = 0; row < 2; row++) {
                    for (s32 col = 0; col < 3; col++) {
                        mGlbMtx.mMtx[row][col] *= mScreenScale;
                    }
                }
            }
            mBuiltScreenScale = mScreenScale;
        }

        // The alpha context comes in as the parent's children got it and leaves as this pane's children get it.
        if (rContextInfluenced && mpParent != NULL) {
            mGlbAlpha = static_cast< u8 >((s32)(mAlpha * rContextAlpha));
        } else {
            mGlbAlpha = mAlpha;
        }

        if (IsInfluencedAlpha() && mAlpha != 255) {
            rContextAlpha = rContextAlpha * mAlpha * (1.0f / cNum_CalculateMtxSelf_f1);
            rContextInfluenced = true;
        }
    }

    void Pane::Draw(const DrawInfo& rInfo) {
        if (mpLayout != nullptr) {
            mpLayout->DrawBranch(this, rInfo);
        }
    }

    void Pane::DrawSelf(const DrawInfo& /*rInfo*/) {
    }

    void Pane::AnimateSelf(u32 option) {
        for (AnimationLink* pLink : mAnimList) {
            if (pLink->IsEnable()) {
                AnimTransform* animTrans = pLink->GetAnimTransform();
                animTrans->Animate(pLink->GetIndex(), this);
            }
        }

        if (IsVisible() || !(option & 1)) {
            if (mpMaterial != NULL) {
                mpMaterial->Animate();
            }
        }
    }

    void Pane::BindAnimation(AnimTransform* pAnimTrans, bool recursive) {
        pAnimTrans->Bind(this, recursive);
    }

    void Pane::UnbindAnimation(AnimTransform* pAnimTrans, bool recursive) {
        UnbindAnimationSelf(pAnimTrans);

        if (recursive) {
            for (Pane* pChild : mChildList) {
                pChild->UnbindAnimation(pAnimTrans, recursive);
            }
        }
    }

    void Pane::UnbindAllAnimation(bool recursive) {
        UnbindAnimation(nullptr, recursive);
    }

    void Pane::UnbindAnimationSelf(AnimTransform* pAnimTrans) {
        if (mpMaterial != NULL) {
            mpMaterial->UnbindAnimation(pAnimTrans);
        }

        detail::UnbindAnimationLink(&mAnimList, pAnimTrans);
    }

    void Pane::AddAnimationLink(AnimationLink* pAnimLink) {
        mAnimList.PushBack(pAnimLink);
    }

    AnimationLink* Pane::FindAnimationLinkSelf(AnimTransform* pAnimTrans) {
        return detail::FindAnimationLink(&mAnimList, pAnimTrans);
    }

    void Pane::SetAnimationEnable(AnimTransform* pAnimTrans, bool enable, bool recursive) {
        AnimationLink* pAnimLink = FindAnimationLinkSelf(pAnimTrans);

        if (pAnimLink != NULL) {
            pAnimLink->SetEnable(enable);
        }

        if (mpMaterial != NULL) {
            mpMaterial->SetAnimationEnable(pAnimTrans, enable);
        }

        if (recursive) {
            for (Pane* pChild : mChildList) {
                pChild->SetAnimationEnable(pAnimTrans, enable, recursive);
            }
        }
    }

    void Pane::LoadMtx(const DrawInfo& rInfo) {
        Mtx34f mtx;
        Mtx34f* pMtx = NULL;

        if (rInfo.IsMultipleViewMtxOnDraw()) {
            mtx.concat(rInfo.GetViewMtx(), mGlbMtx);

            if (rInfo.IsYAxisUp()) {
                ReverseYAxis(&mtx);
            }

            pMtx = &mtx;
        } else if (rInfo.IsYAxisUp()) {
            mtx.set(mGlbMtx);
            pMtx = &mtx;

            ReverseYAxis(&mtx);
        } else {
            pMtx = &mGlbMtx;
        }

        detail::SetDrawMtx(*pMtx);
    }

    const Vec2f Pane::GetVtxPos() const {
        Vec2f base(0.0f, 0.0f);

        switch (mBasePosition % 3) {
        default:
        case 0: {
            base.x = 0.0f;
            break;
        }

        case 1: {
            base.x = -GetDrawSize().width / 2;
            break;
        }

        case 2: {
            base.x = -GetDrawSize().width;
            break;
        }
        }

        switch (mBasePosition / 3) {
        default:
        case 0: {
            base.y = 0.0f;
            break;
        }

        case 1: {
            base.y = -GetDrawSize().height / 2;
            break;
        }

        case 2: {
            base.y = -GetDrawSize().height;
            break;
        }
        }

        return base;
    }

    Material* Pane::GetMaterial() const {
        return mpMaterial;
    }
}  // namespace Ui
