#include "nw4r/lyt/animation.h"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "nw4r/lyt/common.h"
#include "nw4r/lyt/drawInfo.h"
#include "nw4r/lyt/layout.h"
#include "nw4r/lyt/material.h"
#include "nw4r/lyt/pane.h"
#include "nw4r/math/triangular.h"
#include <cstdio>
#include <cstring>

namespace nw4r {
    void ReverseYAxis(math::MTX34* pMtx) {
        pMtx->m[0][1] = -pMtx->m[0][1];
        pMtx->m[1][1] = -pMtx->m[1][1];
        pMtx->m[2][1] = -pMtx->m[2][1];
    }
}  // namespace nw4r

namespace nw4r {
    namespace lyt {
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
            memset(mName, 0, sizeof(mName));
            memset(mUserData, 0, sizeof(mUserData));
            mTranslate = math::VEC3(0.0f, 0.0f, 0.0f);
            mRotate = math::VEC3(0.0f, 0.0f, 0.0f);
            mScale = math::VEC2(1.0f, 1.0f);
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

            mTranslate = math::VEC3(pRes->mTranslate[0], pRes->mTranslate[1], pRes->mTranslate[2]);
            mRotate = math::VEC3(pRes->mRotate[0], pRes->mRotate[1], pRes->mRotate[2]);
            mScale = math::VEC2(pRes->mScale[0], pRes->mScale[1]);
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
            strncpy(mName, pName, 0x10);
            mName[0x10] = '\0';
            mpLayout->TouchStructure();
        }

        void Pane::SetUserData(const char* pUserData) {
            strncpy(mUserData, pUserData, 8);
            mUserData[8] = '\0';
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

        const ut::Color Pane::GetVtxColor(u32 idx) const {
            return -1;
        }

        void Pane::SetVtxColor(u32 idx, ut::Color color) {
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

        u8 Pane::GetVtxColorElement(u32 idx) const {
            return 0xFF;
        }

        void Pane::SetVtxColorElement(u32 idx, u8 value) {
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
            math::VEC2 scale = mScale;
            if (rInfo.IsLocationAdjust() && IsLocationAdjust()) {
                scale.x *= rInfo.GetLocationAdjustScale().x;
                scale.y *= rInfo.GetLocationAdjustScale().y;
            }

            const bool localChanged = !mLocalBuilt || !SameBits(&mTranslate.x, &mBuiltTranslate.x, 3) ||
                                      !SameBits(&mRotate.x, &mBuiltRotate.x, 3) || !SameBits(&scale.x, &mBuiltScale.x, 2);
            if (localChanged) {
                math::MTX34 mtx1, mtx2;
                math::MTX34 rotateMtx;

                NebulaMtxScale(mtx2, scale.x, scale.y, 1.0f);

                NebulaMtxRotRad(rotateMtx, 'x', NW4R_MATH_DEG_TO_RAD(mRotate.x));
                NebulaMtxConcat(rotateMtx, mtx2, mtx1);

                NebulaMtxRotRad(rotateMtx, 'y', NW4R_MATH_DEG_TO_RAD(mRotate.y));
                NebulaMtxConcat(rotateMtx, mtx1, mtx2);

                NebulaMtxRotRad(rotateMtx, 'z', NW4R_MATH_DEG_TO_RAD(mRotate.z));
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
                math::MTX34 world;
                if (mpParent != NULL) {
                    math::MTX34Mult(&world, &mpParent->mWorldMtx, &mMtx);
                } else if (rInfo.IsMultipleViewMtxOnDraw()) {
                    world = mMtx;
                } else {
                    math::MTX34Mult(&world, &rInfo.GetViewMtx(), &mMtx);
                }
                if (mpFollow != nullptr) {
                    mpFollow->Follow(*this, world);
                }
                if (mWorldVersion == 0 || !SameBits(&world.m[0][0], &mWorldMtx.m[0][0], 12)) {
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
                            mGlbMtx.m[row][col] *= mScreenScale;
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
                rContextAlpha = rContextAlpha * mAlpha * (1.0f / 255.0f);
                rContextInfluenced = true;
            }
        }

        void Pane::Draw(const DrawInfo& rInfo) {
            if (mpLayout != nullptr) {
                mpLayout->DrawBranch(this, rInfo);
            }
        }

        void Pane::DrawSelf(const DrawInfo& rInfo) {
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
            math::MTX34 mtx;
            math::MTX34* pMtx = NULL;

            if (rInfo.IsMultipleViewMtxOnDraw()) {
                math::MTX34Mult(&mtx, &rInfo.GetViewMtx(), &mGlbMtx);

                if (rInfo.IsYAxisUp()) {
                    ReverseYAxis(&mtx);
                }

                pMtx = &mtx;
            } else if (rInfo.IsYAxisUp()) {
                math::MTX34Copy(&mtx, &mGlbMtx);
                pMtx = &mtx;

                ReverseYAxis(&mtx);
            } else {
                pMtx = &mGlbMtx;
            }

            detail::SetDrawMtx(*pMtx);
        }

        const math::VEC2 Pane::GetVtxPos() const {
            math::VEC2 base(0.0f, 0.0f);

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

    };  // namespace lyt
};  // namespace nw4r
