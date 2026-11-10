#include <NebulaGpu.h>
#include <nebula_math.h>

#include "Game/Ui/Animation.hpp"
#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/Group.hpp"
#include "Game/Ui/Layout.hpp"
#include "Game/Ui/Material.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/Ui/ResourceAccessor.hpp"
#include <nebula_module.h>
#include <new>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_GetHermiteCurveValue_f1, "GetHermiteCurveValue.f1", 2.0F);
NEBULA_TUNABLE_NAMED(f32, cNum_GetHermiteCurveValue_f2, "GetHermiteCurveValue.f2", 3.0F);
NEBULA_TUNABLE_NAMED(f32, cNum_GetHermiteCurveValue_f3, "GetHermiteCurveValue.f3", 2.0F);
NEBULA_TUNABLE_NAMED(f32, cNum_GetHermiteCurveValue_f4, "GetHermiteCurveValue.f4", 3.0F);
NEBULA_TUNABLE_NAMED(f32, cNum_GetHermiteCurveValue_f5, "GetHermiteCurveValue.f5", 2.0F);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimateVertexColor_f1, "AnimateVertexColor.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimateMaterialColor_f1, "AnimateMaterialColor.f1", 0.5f);

NEBULA_TUNABLE(f32, R_FRAME_TOLERANCE, 0.001F);

namespace Ui {
    namespace {
        inline bool RIsSame(const f32 a, const f32 b, const f32 tolerance) {
            f32 c = a - b;
            return (-tolerance < c && c < tolerance);
        }

        u16 GetStepCurveValue(f32 frame, const UiStepKey* keyArray, u32 keySize) {
            if (keySize == 1 || frame <= keyArray[0].mFrame) {
                return keyArray[0].mValue;
            } else if (frame >= keyArray[keySize - 1].mFrame) {
                return keyArray[keySize - 1].mValue;
            }

            int ikeyL = 0;
            int ikeyR = (int)keySize - 1;
            while (ikeyL != ikeyR - 1 && ikeyL != ikeyR) {
                int ikeyCenter = (ikeyL + ikeyR) / 2;
                const UiStepKey& centerKey = keyArray[ikeyCenter];
                if (frame < centerKey.mFrame) {
                    ikeyR = ikeyCenter;
                } else {
                    ikeyL = ikeyCenter;
                }
            }

            if (RIsSame(frame, keyArray[ikeyR].mFrame, R_FRAME_TOLERANCE)) {
                return keyArray[ikeyR].mValue;
            } else {
                return keyArray[ikeyL].mValue;
            }
        }

        f32 GetHermiteCurveValue(f32 frame, const UiHermiteKey* keyArray, u32 keySize) {
            if (keySize == 1 || frame <= keyArray[0].mFrame) {
                return keyArray[0].mValue;
            } else if (frame >= keyArray[keySize - 1].mFrame) {
                return keyArray[keySize - 1].mValue;
            }

            int ikeyL = 0;
            int ikeyR = (int)keySize - 1;
            while (ikeyL != ikeyR - 1 && ikeyL != ikeyR) {
                int ikeyCenter = (ikeyL + ikeyR) / 2;
                if (frame <= keyArray[ikeyCenter].mFrame) {
                    ikeyR = ikeyCenter;
                } else {
                    ikeyL = ikeyCenter;
                }
            }

            const UiHermiteKey& key0 = keyArray[ikeyL];
            const UiHermiteKey& key1 = keyArray[ikeyR];
            if (RIsSame(frame, key1.mFrame, R_FRAME_TOLERANCE)) {
                if (ikeyR < (int)keySize - 1 && key1.mFrame == keyArray[ikeyR + 1].mFrame) {
                    return keyArray[ikeyR + 1].mValue;
                } else {
                    return key1.mValue;
                }
            }
            f32 t1 = frame - key0.mFrame;
            f32 t2 = 1.0F / (key1.mFrame - key0.mFrame);
            f32 v0 = key0.mValue;
            f32 v1 = key1.mValue;
            f32 s0 = key0.mSlope;
            f32 s1 = key1.mSlope;

            f32 t1t1t2 = t1 * t1 * t2;
            f32 t1t1t2t2 = t1t1t2 * t2;
            f32 t1t1t1t2t2 = t1 * t1t1t2t2;
            f32 t1t1t1t2t2t2 = t1t1t1t2t2 * t2;

            return v0 * (cNum_GetHermiteCurveValue_f1 * t1t1t1t2t2t2 - cNum_GetHermiteCurveValue_f2 * t1t1t2t2 + 1.0F) + v1 * (-cNum_GetHermiteCurveValue_f3 * t1t1t1t2t2t2 + cNum_GetHermiteCurveValue_f4 * t1t1t2t2) +
                   s0 * (t1t1t1t2t2 - cNum_GetHermiteCurveValue_f5 * t1t1t2 + t1) + s1 * (t1t1t1t2t2 - t1t1t2);
        }

        // One animation's targets and their keys, by offset from the animation file's start.
        struct Curves {
            const UiAnimationHeader* pFile;
            const UiAnimTargetData* pTargets;
            u32 num;

            Curves(const UiAnimationHeader* pRes, const UiAnimInfoData& rInfo)
                : pFile(pRes), pTargets(uiAt< UiAnimTargetData >(pRes, rInfo.mTargets)), num(rInfo.mTargetCount) {
            }
            f32 Hermite(const UiAnimTargetData& rTarget, f32 frame) const {
                return GetHermiteCurveValue(frame, uiAt< UiHermiteKey >(pFile, rTarget.mKeys), rTarget.mKeyCount);
            }
            u16 Step(const UiAnimTargetData& rTarget, f32 frame) const {
                return GetStepCurveValue(frame, uiAt< UiStepKey >(pFile, rTarget.mKeys), rTarget.mKeyCount);
            }
        };

        void AnimatePaneSRT(Pane* pPane, const Curves& rCurves, f32 frame) {
            for (u32 i = 0; i < rCurves.num; ++i) {
                const UiAnimTargetData& rTarget = rCurves.pTargets[i];
                pPane->SetSRTElement(rTarget.mTarget, rCurves.Hermite(rTarget, frame));
            }
        }

        void AnimateVisibility(Pane* pPane, const Curves& rCurves, f32 frame) {
            for (u32 i = 0; i < rCurves.num; ++i) {
                pPane->SetVisible(0 != rCurves.Step(rCurves.pTargets[i], frame));
            }
        }

        void AnimateVertexColor(Pane* pPane, const Curves& rCurves, f32 frame) {
            for (u32 i = 0; i < rCurves.num; ++i) {
                const UiAnimTargetData& rTarget = rCurves.pTargets[i];
                f32 value = rCurves.Hermite(rTarget, frame);
                value += cNum_AnimateVertexColor_f1;
                pPane->SetColorElement(rTarget.mTarget, nebula_f32_to_u8(value));
            }
        }

        void AnimateMaterialColor(Material* pMaterial, const Curves& rCurves, f32 frame) {
            for (u32 i = 0; i < rCurves.num; ++i) {
                const UiAnimTargetData& rTarget = rCurves.pTargets[i];
                f32 value = rCurves.Hermite(rTarget, frame);
                value += cNum_AnimateMaterialColor_f1;
                s16 s16Val = nebula_f32_to_s16(value);
                s16Val = Min(Max(s16Val, s16(-1024)), s16(1023));
                pMaterial->SetColorElement(rTarget.mTarget, s16Val);
            }
        }

        void AnimateTextureSRT(Material* pMaterial, const Curves& rCurves, f32 frame) {
            for (u32 i = 0; i < rCurves.num; ++i) {
                const UiAnimTargetData& rTarget = rCurves.pTargets[i];
                if (rTarget.mId < pMaterial->GetTexSRTCap()) {
                    pMaterial->SetTexSRTElement(rTarget.mId, rTarget.mTarget, rCurves.Hermite(rTarget, frame));
                }
            }
        }

        void AnimateTexturePattern(Material* pMaterial, const Curves& rCurves, f32 frame, void** tpls) {
            for (u32 j = 0; j < rCurves.num; ++j) {
                const UiAnimTargetData& rTarget = rCurves.pTargets[j];
                if (rTarget.mId < pMaterial->GetTextureNum()) {
                    const u16 fileIdx = rCurves.Step(rTarget, frame);
                    if (tpls[fileIdx]) {
                        pMaterial->GetTexturePtr(rTarget.mId)->ReplaceImage(tpls[fileIdx]);
                    }
                }
            }
        }

        void AnimateIndTexSRT(Material* pMaterial, const Curves& rCurves, f32 frame) {
            for (u32 i = 0; i < rCurves.num; ++i) {
                const UiAnimTargetData& rTarget = rCurves.pTargets[i];
                if (rTarget.mId < pMaterial->GetIndTexSRTCap()) {
                    pMaterial->SetIndTexSRTElement(rTarget.mId, rTarget.mTarget, rCurves.Hermite(rTarget, frame));
                }
            }
        }

        const UiAnimContentData& ContentAt(const UiAnimationHeader* pRes, u32 idx) {
            return uiAt< UiAnimContentData >(pRes, pRes->mContents)[idx];
        }

    }  // namespace

    AnimTransform::AnimTransform() : mpRes(0), mFrame(0) {
    }

    AnimTransform::~AnimTransform() {
    }

    u16 AnimTransform::GetFrameSize() const {
        return static_cast< u16 >(mpRes->mFrameSize);
    }

    bool AnimTransform::IsLoopData() const {
        return mpRes->mLoop != 0;
    }

    AnimTransformBasic::AnimTransformBasic() : mpFileResAry(0), mAnimLinkAry(0), mAnimLinkNum(0) {
    }

    AnimTransformBasic::~AnimTransformBasic() {
        Layout::DeleteArray(mAnimLinkAry, mAnimLinkNum);
        Layout::DeletePrimArray(mpFileResAry);
    }

    void AnimTransformBasic::SetResource(const UiAnimationHeader* pRes, ResourceAccessor* pResAccessor) {
        SetResource(pRes, pResAccessor, static_cast< u16 >(pRes->mContentCount));
    }

    void AnimTransformBasic::SetResource(const UiAnimationHeader* pRes, ResourceAccessor* pResAccessor, u16 animNum) {
        mpRes = pRes;
        mpFileResAry = nullptr;

        if (pRes->mFileCount > 0) {
            mpFileResAry = static_cast< void** >(Layout::AllocMemory(sizeof(void*) * pRes->mFileCount));
            if (mpFileResAry != nullptr) {
                const u32* const pNames = uiAt< u32 >(pRes, pRes->mFiles);
                for (u32 i = 0; i < pRes->mFileCount; i++) {
                    mpFileResAry[i] = pResAccessor->GetResource('timg', uiAt< char >(pRes, pRes->mStrings + pNames[i]), 0);
                }
            }
        }

        mAnimLinkAry = static_cast< AnimationLink* >(Layout::AllocMemory(sizeof(AnimationLink) * animNum));
        if (mAnimLinkAry != nullptr) {
            mAnimLinkNum = animNum;
            memset((void*)mAnimLinkAry, 0, animNum * sizeof(AnimationLink));

            for (u16 i = 0; i < animNum; i++) {
                new (&mAnimLinkAry[i]) AnimationLink();
            }
        }
    }

    void AnimTransformBasic::Bind(Pane* pPane, bool bRecursive) {
        AnimationLink* pCrAnimLink = 0;

        for (u16 i = 0; i < mpRes->mContentCount; ++i) {
            const UiAnimContentData& animCont = ContentAt(mpRes, i);
            const char* const pName = uiAt< char >(mpRes, mpRes->mStrings + animCont.mName);
            if (animCont.mMaterial == 0) {
                if (Pane* const pFindPane = pPane->FindPaneByName(pName, bRecursive)) {
                    pCrAnimLink = Bind(pFindPane, pCrAnimLink, i);
                    if (!pCrAnimLink) {
                        break;
                    }
                }
            } else {
                if (Material* const pFindMat = pPane->FindMaterialByName(pName, bRecursive)) {
                    pCrAnimLink = Bind(pFindMat, pCrAnimLink, i);
                    if (!pCrAnimLink) {
                        break;
                    }
                }
            }
        }
    }

    void AnimTransformBasic::Bind(Material* pMaterial) {
        AnimationLink* pCrAnimLink = 0;

        for (u16 i = 0; i < mpRes->mContentCount; ++i) {
            const UiAnimContentData& animCont = ContentAt(mpRes, i);
            if (animCont.mMaterial != 0 &&
                detail::EqualsMaterialName(pMaterial->GetName(), uiAt< char >(mpRes, mpRes->mStrings + animCont.mName))) {
                pCrAnimLink = Bind(pMaterial, pCrAnimLink, i);
                if (!pCrAnimLink) {
                    break;
                }
            }
        }
    }

    template < typename T >
    AnimationLink* AnimTransformBasic::Bind(T* pTarget, AnimationLink* pAnimLink, u16 idx) {
        pAnimLink = FindUnbindLink(pAnimLink);
        if (!pAnimLink) {
            return 0;
        }

        pAnimLink->Set(this, idx);
        pTarget->AddAnimationLink(pAnimLink);

        return ++pAnimLink;
    }

    void AnimTransformBasic::Animate(u32 idx, Pane* pPane) {
        const UiAnimContentData& animCont = ContentAt(mpRes, idx);
        const UiAnimInfoData* const pInfos = uiAt< UiAnimInfoData >(mpRes, animCont.mInfos);
        for (u32 i = 0; i < animCont.mInfoCount; ++i) {
            const Curves curves(mpRes, pInfos[i]);
            switch (pInfos[i].mKind) {
            case UiAnimKind_PaneSrt:
                AnimatePaneSRT(pPane, curves, mFrame);
                break;
            case UiAnimKind_Visibility:
                AnimateVisibility(pPane, curves, mFrame);
                break;
            case UiAnimKind_VertexColor:
                AnimateVertexColor(pPane, curves, mFrame);
                break;
            }
        }
    }

    void AnimTransformBasic::Animate(u32 idx, Material* pMaterial) {
        const UiAnimContentData& animCont = ContentAt(mpRes, idx);
        const UiAnimInfoData* const pInfos = uiAt< UiAnimInfoData >(mpRes, animCont.mInfos);
        for (u32 i = 0; i < animCont.mInfoCount; ++i) {
            const Curves curves(mpRes, pInfos[i]);
            switch (pInfos[i].mKind) {
            case UiAnimKind_MaterialColor:
                AnimateMaterialColor(pMaterial, curves, mFrame);
                break;
            case UiAnimKind_TextureSrt:
                AnimateTextureSRT(pMaterial, curves, mFrame);
                break;
            case UiAnimKind_TexturePattern:
                if (mpFileResAry) {
                    AnimateTexturePattern(pMaterial, curves, mFrame, mpFileResAry);
                }
                break;
            case UiAnimKind_IndirectSrt:
                AnimateIndTexSRT(pMaterial, curves, mFrame);
                break;
            }
        }
    }

    AnimationLink* AnimTransformBasic::FindUnbindLink(AnimationLink* pLink) const {
        if (pLink == 0) {
            pLink = mAnimLinkAry;
        }

        while (pLink < mAnimLinkAry + mAnimLinkNum) {
            if (pLink->GetAnimTransform() == 0) {
                return pLink;
            }

            ++pLink;
        }

        return 0;
    }

    AnimResource::AnimResource(const void* anmResBuf) : mpResBlock(nullptr) {
        const UiAnimationHeader* const pHead = static_cast< const UiAnimationHeader* >(anmResBuf);
        if (pHead->mMagic == cUiAnimationMagic && pHead->mVersion == cUiDataVersion) {
            mpResBlock = pHead;
        } else if (pHead->mMagic == cUiAnimationMagic) {
            nebula_log("UiData: a layout animation of version %u; this game reads version %u (the pak needs a new cook)\n",
                    static_cast< unsigned >(pHead->mVersion), static_cast< unsigned >(cUiDataVersion));
        }
    }

    namespace detail {

        AnimationLink* FindAnimationLink(AnimationList* pAnimList, AnimTransform* pAnimTrans) {
            for (AnimationLink* pLink : *pAnimList) {
                if (pAnimTrans == pLink->GetAnimTransform()) {
                    return pLink;
                }
            }

            return 0;
        }

        void UnbindAnimationLink(AnimationList* pAnimList, AnimTransform* pAnimTrans) {
            for (AnimationLink *pLink = pAnimList->First(), *pNext; pLink != nullptr; pLink = pNext) {
                pNext = AnimationList::Next(pLink);
                if (!pAnimTrans || pLink->GetAnimTransform() == pAnimTrans) {
                    pAnimList->Erase(pLink);
                    pLink->Reset();
                }
            }
        }

    };  // namespace detail
}  // namespace Ui
