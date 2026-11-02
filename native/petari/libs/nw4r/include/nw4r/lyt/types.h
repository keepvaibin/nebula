#pragma once

#include "nw4r/math/types.h"
#include "nw4r/ut/LinkList.h"
#include <cstddef>
#include <mem.h>
#include <NebulaGpu.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

namespace nw4r {
    namespace lyt {
        enum { HORIZONTALPOSITION_LEFT, HORIZONTALPOSITION_CENTER, HORIZONTALPOSITION_RIGHT, HORIZONTALPOSITION_MAX };
        enum { VERTICALPOSITION_TOP, VERTICALPOSITION_CENTER, VERTICALPOSITION_BOTTOM, VERTICALPOSITION_MAX };

        enum AnimContentType { ANIMCONTENTTYPE_PANE, ANIMCONTENTTYPE_MATERIAL };
        const int ResourceNameStrMax = 16;
        const int MaterialNameStrMax = ResourceNameStrMax + 4;
        namespace detail {
            template < typename T >
            inline void SetBit(T* pBits, int pos, bool val) {
                const T mask = T(~(1 << pos));
                *pBits &= mask;
                *pBits |= (val ? 1 : 0) << pos;
            }

            // Resource fields (host order, as the importer cooks layouts).
            typedef s16 ResS16;
            typedef u16 ResU16;
            typedef s32 ResS32;
            typedef u32 ResU32;

            template < typename T >
            inline const T* ConvertOffsToPtr(const void* baseAddr, unsigned int offs) {
                return reinterpret_cast< const T* >(static_cast< const u8* >(baseAddr) + offs);
            }

            template < typename T >
            inline T* ConvertOffsToPtr(void* baseAddress, unsigned int offset) {
                return reinterpret_cast< T* >(static_cast< u8* >(baseAddress) + offset);
            }

            template < typename T >
            inline void SetBits(T* pBits, int pos, int len, T val) {
                const u32 MaxValue = 0xFFFFFFFFU >> (32 - len);
                const T mask = T(~(MaxValue << pos));
                *pBits &= mask;
                *pBits |= val << pos;
            }

            template < typename T >
            inline T GetBits(T bits, int pos, int len) {
                const u32 mask = ~(0xFFFFFFFFU << len);
                return T((bits >> pos) & mask);
            }

            template < typename T >
            inline bool TestBit(T bits, int pos) {
                const T mask = T(1 << pos);

                return 0 != (bits & mask);
            }

            enum {
                ANIMTARGET_MATCOLOR_MATCOLOR0_R,
                ANIMTARGET_MATCOLOR_MATCOLOR0_G,
                ANIMTARGET_MATCOLOR_MATCOLOR0_B,
                ANIMTARGET_MATCOLOR_MATCOLOR0_A,
                ANIMTARGET_MATCOLOR_TEVCOLOR0_R,
                ANIMTARGET_MATCOLOR_TEVCOLOR0_G,
                ANIMTARGET_MATCOLOR_TEVCOLOR0_B,
                ANIMTARGET_MATCOLOR_TEVCOLOR0_A,
                ANIMTARGET_MATCOLOR_TEVCOLOR1_R,
                ANIMTARGET_MATCOLOR_TEVCOLOR1_G,
                ANIMTARGET_MATCOLOR_TEVCOLOR1_B,
                ANIMTARGET_MATCOLOR_TEVCOLOR1_A,
                ANIMTARGET_MATCOLOR_TEVCOLOR2_R,
                ANIMTARGET_MATCOLOR_TEVCOLOR2_G,
                ANIMTARGET_MATCOLOR_TEVCOLOR2_B,
                ANIMTARGET_MATCOLOR_TEVCOLOR2_A,
                ANIMTARGET_MATCOLOR_TEVKONST0_R,
                ANIMTARGET_MATCOLOR_TEVKONST0_G,
                ANIMTARGET_MATCOLOR_TEVKONST0_B,
                ANIMTARGET_MATCOLOR_TEVKONST0_A,
                ANIMTARGET_MATCOLOR_TEVKONST1_R,
                ANIMTARGET_MATCOLOR_TEVKONST1_G,
                ANIMTARGET_MATCOLOR_TEVKONST1_B,
                ANIMTARGET_MATCOLOR_TEVKONST1_A,
                ANIMTARGET_MATCOLOR_TEVKONST2_R,
                ANIMTARGET_MATCOLOR_TEVKONST2_G,
                ANIMTARGET_MATCOLOR_TEVKONST2_B,
                ANIMTARGET_MATCOLOR_TEVKONST2_A,
                ANIMTARGET_MATCOLOR_TEVKONST3_R,
                ANIMTARGET_MATCOLOR_TEVKONST3_G,
                ANIMTARGET_MATCOLOR_TEVKONST3_B,
                ANIMTARGET_MATCOLOR_TEVKONST3_A,

                ANIMTARGET_MATCOLOR_MAX
            };
        };  // namespace detail

        struct Size {
            Size() : width(0.0f), height(0.0f) {
            }

            Size(const Size& rOther) : width(rOther.width), height(rOther.height) {
            }

            Size(f32 aWidth, f32 aHeight) : width(aWidth), height(aHeight) {
            }

            f32 width;
            f32 height;
        };

        inline bool operator==(const Size& a, const Size& b) {
            return a.width == b.width && a.height == b.height;
        }

        struct InflationLRTB {
            f32 l;
            f32 r;
            f32 t;
            f32 b;
        };

        struct WindowFrameSize {
            f32 l;
            f32 r;
            f32 t;
            f32 b;
        };

        struct ChanCtrl {
            ChanCtrl() : reserve1(0), reserve2(0) {
                Set(GPU_SRC_VTX, GPU_SRC_VTX);
            }

            ChanCtrl(GpuColorSrc colSrc, GpuColorSrc alpSrc) : reserve1(0), reserve2(0) {
                Set(colSrc, alpSrc);
            }

            void Set(GpuColorSrc colSrc, GpuColorSrc alpSrc) {
                matSrcCol = colSrc;
                matSrcAlp = alpSrc;
            }

            GpuColorSrc GetColorSrc() const {
                return GpuColorSrc(matSrcCol);
            }

            GpuColorSrc GetAlphaSrc() const {
                return GpuColorSrc(matSrcAlp);
            }

            u8 matSrcCol;
            u8 matSrcAlp;
            u8 reserve1;
            u8 reserve2;
        };

        struct TexSRT {
            math::VEC2 translate;
            f32 rotate;
            math::VEC2 scale;
        };

        struct TexCoordGen {
            TexCoordGen() : reserve(0) {
                Set(GX_TG_MTX2x4, GPU_TG_TEX0, GPU_IDENTITY);
            }

            TexCoordGen(GpuTexGenType aTexGenType, GpuTexGenSrc aTexGenSrc, u32 aTexMtx)
                : reserve(0)

            {
                Set(aTexGenType, aTexGenSrc, aTexMtx);
            }

            void Set(GpuTexGenType aTexGenType, GpuTexGenSrc aTexGenSrc, u32 aTexMtx) {
                texGenType = aTexGenType;
                texGenSrc = aTexGenSrc;
                texMtx = u8(aTexMtx);
            }

            GpuTexGenType GetTexGenType() const {
                return GpuTexGenType(texGenType);
            }

            GpuTexGenSrc GetTexGenSrc() const {
                return GpuTexGenSrc(texGenSrc);
            }

            u32 GetTexMtx() const {
                return texMtx;
            }

            u8 texGenType;
            u8 texGenSrc;
            u8 texMtx;
            u8 reserve;
        };

        struct IndirectStage {
            IndirectStage() {
                Set(GPU_TEXCOORD0, GPU_TEXMAP0, GPU_ITS_1, GPU_ITS_1);
            }

            IndirectStage(GpuTexCoordID aTexCoordGen, GpuTexMapID aTexMap, GpuIndTexScale aScaleS, GpuIndTexScale aScaleT) {
                Set(aTexCoordGen, aTexMap, aScaleS, aScaleT);
            }

            void Set(GpuTexCoordID aTexCoordGen, GpuTexMapID aTexMap, GpuIndTexScale aScaleS, GpuIndTexScale aScaleT) {
                texCoordGen = aTexCoordGen;
                texMap = aTexMap;
                scaleS = aScaleS;
                scaleT = aScaleT;
            }

            GpuTexCoordID GetTexCoordGen() const {
                return GpuTexCoordID(texCoordGen);
            }

            GpuTexMapID GetTexMap() const {
                return GpuTexMapID(texMap);
            }

            GpuIndTexScale GetScaleS() const {
                return GpuIndTexScale(scaleS);
            }

            GpuIndTexScale GetScaleT() const {
                return GpuIndTexScale(scaleT);
            }

            u8 texCoordGen;
            u8 texMap;
            u8 scaleS;
            u8 scaleT;
        };

        struct TevStageInOp {
            void SetIn(u8 a, u8 b, u8 c, u8 d) {
                ab = u8((a << 0) | (b << 4));
                cd = u8((c << 0) | (d << 4));
            }

            void SetOp(u8 aOp, u8 bias, u8 scale, bool clamp, u8 outReg, u8 kSel) {
                op = u8((aOp << 0) | (bias << 4) | (scale << 6));
                cl = u8(((clamp ? 1 : 0) << 0) | (outReg << 1) | (kSel << 3));
            }

            u8 GetA() const {
                return u8((ab >> 0) & 0xF);
            }

            u8 GetB() const {
                return u8((ab >> 4) & 0xF);
            }

            u8 GetC() const {
                return u8((cd >> 0) & 0xF);
            }

            u8 GetD() const {
                return u8((cd >> 4) & 0xF);
            }

            u8 GetOp() const {
                return u8((op >> 0) & 0xF);
            }

            u8 GetBias() const {
                return u8((op >> 4) & 0x3);
            }

            u8 GetScale() const {
                return u8((op >> 6) & 0x3);
            }

            bool IsClamp() const {
                return ((cl >> 0) & 0x1) != 0;
            }

            u8 GetOutReg() const {
                return u8((cl >> 1) & 0x3);
            }

            u8 GetKSel() const {
                return u8((cl >> 3) & 0x1F);
            }

            u8 ab;
            u8 cd;
            u8 op;
            u8 cl;
        };

        struct TevStage {
            TevStage() {
                SetOrder(GPU_TEXCOORD_NULL, GPU_TEXMAP_NULL, GPU_COLOR0A0, GPU_TEV_SWAP0, GPU_TEV_SWAP0);
                SetColorIn(GPU_CC_ZERO, GPU_CC_ZERO, GPU_CC_ZERO, GPU_CC_RASC);
                SetAlphaIn(GPU_CA_ZERO, GPU_CA_ZERO, GPU_CA_ZERO, GPU_CA_RASA);
                SetColorOp(GPU_TEV_ADD, GPU_TB_ZERO, GPU_CS_SCALE_1, GPU_TRUE, GPU_TEVPREV, GPU_TEV_KCSEL_K0);
                SetAlphaOp(GPU_TEV_ADD, GPU_TB_ZERO, GPU_CS_SCALE_1, GPU_TRUE, GPU_TEVPREV, GPU_TEV_KASEL_K0_R);
                SetIndirect(GPU_INDTEXSTAGE0, GPU_ITF_8, GPU_ITB_NONE, GPU_ITM_OFF, GPU_ITW_OFF, GPU_ITW_OFF, false, false, GPU_ITBA_OFF);
            }

            void SetOrder(GpuTexCoordID aTexCoordGen, GpuTexMapID aTexMap, GpuChannelID aColChan, GpuTevSwapSel rasSel, GpuTevSwapSel texSel) {
                texCoordGen = aTexCoordGen;
                colChan = aColChan;
                texMap = u8(aTexMap);
                swapSel = u8(((aTexMap >> 8) << 0) | (rasSel << 1) | (texSel << 3));
            }

            void SetColorIn(GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d) {
                colIn.SetIn(a, b, c, d);
            }

            void SetAlphaIn(GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c, GpuTevAlphaArg d) {
                alpIn.SetIn(a, b, c, d);
            }

            void SetColorOp(GpuTevOp op, GpuTevBias bias, GpuTevScale scale, bool clamp, GpuTevRegID outReg, GpuTevKColorSel kSel) {
                colIn.SetOp(op, bias, scale, clamp, outReg, kSel);
            }

            void SetAlphaOp(GpuTevOp op, GpuTevBias bias, GpuTevScale scale, bool clamp, GpuTevRegID outReg, GpuTevKAlphaSel kSel) {
                alpIn.SetOp(op, bias, scale, clamp, outReg, kSel);
            }

            void SetIndirect(GpuIndTexStageID stage, GpuIndTexFormat format, GpuIndTexBiasSel biasSel, GpuIndTexMtxID mtxSel, GpuIndTexWrap wrapS,
                             GpuIndTexWrap wrapT, bool addPrev, bool utcLod, GpuIndTexAlphaSel alphaSel) {
                indStage = stage;
                indBiMt = u8((biasSel << 0) | (mtxSel << 3));
                indWrap = u8((wrapS << 0) | (wrapT << 3));
                indFoAdUtAl = u8((format << 0) | ((addPrev ? 1 : 0) << 2) | ((utcLod ? 1 : 0) << 3) | (alphaSel << 4));
            }

            GpuTexCoordID GetTexCoordGen() const {
                return GpuTexCoordID(texCoordGen);
            }

            GpuTexMapID GetTexMap() const {
                return GpuTexMapID((((swapSel >> 0) & 0x1) << 8) | texMap);
            }

            GpuChannelID GetColorChan() const {
                return GpuChannelID(colChan);
            }

            GpuTevSwapSel GetRasSwapSel() const {
                return GpuTevSwapSel((swapSel >> 1) & 0x3);
            }

            GpuTevSwapSel GetTexSwapSel() const {
                return GpuTevSwapSel((swapSel >> 3) & 0x3);
            }

            GpuTevColorArg GetColorInA() const {
                return GpuTevColorArg(colIn.GetA());
            }

            GpuTevColorArg GetColorInB() const {
                return GpuTevColorArg(colIn.GetB());
            }

            GpuTevColorArg GetColorInC() const {
                return GpuTevColorArg(colIn.GetC());
            }

            GpuTevColorArg GetColorInD() const {
                return GpuTevColorArg(colIn.GetD());
            }

            GpuTevAlphaArg GetAlphaInA() const {
                return GpuTevAlphaArg(alpIn.GetA());
            }

            GpuTevAlphaArg GetAlphaInB() const {
                return GpuTevAlphaArg(alpIn.GetB());
            }

            GpuTevAlphaArg GetAlphaInC() const {
                return GpuTevAlphaArg(alpIn.GetC());
            }

            GpuTevAlphaArg GetAlphaInD() const {
                return GpuTevAlphaArg(alpIn.GetD());
            }

            GpuTevOp GetColorOp() const {
                return GpuTevOp(colIn.GetOp());
            }

            GpuTevBias GetColorBias() const {
                return GpuTevBias(colIn.GetBias());
            }

            GpuTevScale GetColorScale() const {
                return GpuTevScale(colIn.GetScale());
            }

            bool IsColorClamp() const {
                return colIn.IsClamp();
            }

            GpuTevRegID GetColorOutReg() const {
                return GpuTevRegID(colIn.GetOutReg());
            }

            GpuTevKColorSel GetKColorSel() const {
                return GpuTevKColorSel(colIn.GetKSel());
            }

            GpuTevOp GetAlphaOp() const {
                return GpuTevOp(alpIn.GetOp());
            }

            GpuTevBias GetAlphaBias() const {
                return GpuTevBias(alpIn.GetBias());
            }

            GpuTevScale GetAlphaScale() const {
                return GpuTevScale(alpIn.GetScale());
            }

            bool IsAlphaClamp() const {
                return alpIn.IsClamp();
            }

            GpuTevRegID GetAlphaOutReg() const {
                return GpuTevRegID(alpIn.GetOutReg());
            }

            GpuTevKAlphaSel GetKAlphaSel() const {
                return GpuTevKAlphaSel(alpIn.GetKSel());
            }

            GpuIndTexStageID GetIndStage() const {
                return GpuIndTexStageID(indStage);
            }

            GpuIndTexFormat GetIndFormat() const {
                return GpuIndTexFormat((indFoAdUtAl >> 0) & 0x3);
            }

            GpuIndTexBiasSel GetIndBiasSel() const {
                return GpuIndTexBiasSel((indBiMt >> 0) & 0x7);
            }

            GpuIndTexMtxID GetIndMtxSel() const {
                return GpuIndTexMtxID((indBiMt >> 3) & 0xF);
            }

            GpuIndTexWrap GetIndWrapS() const {
                return GpuIndTexWrap((indWrap >> 0) & 0x7);
            }

            GpuIndTexWrap GetIndWrapT() const {
                return GpuIndTexWrap((indWrap >> 3) & 0x7);
            }

            bool IsIndAddPrev() const {
                return ((indFoAdUtAl >> 2) & 0x1) != 0;
            }

            bool IsIndUtcLod() const {
                return ((indFoAdUtAl >> 3) & 0x1) != 0;
            }

            GpuIndTexAlphaSel GetIndAlphaSel() const {
                return GpuIndTexAlphaSel((indFoAdUtAl >> 4) & 0x3);
            }

            u8 texCoordGen;
            u8 colChan;
            u8 texMap;
            u8 swapSel;

            TevStageInOp colIn;
            TevStageInOp alpIn;

            u8 indStage;
            u8 indBiMt;
            u8 indWrap;
            u8 indFoAdUtAl;
        };

        struct TevSwapMode {
            TevSwapMode() {
                Set(GPU_CH_RED, GPU_CH_GREEN, GPU_CH_BLUE, GPU_CH_ALPHA);
            }

            TevSwapMode(GpuTevColorChan r, GpuTevColorChan g, GpuTevColorChan b, GpuTevColorChan a) {
                Set(r, g, b, a);
            }

            void Set(GpuTevColorChan r, GpuTevColorChan g, GpuTevColorChan b, GpuTevColorChan a) {
                swap = u8(((r - GPU_CH_RED) << 0) | ((g - GPU_CH_RED) << 2) | ((b - GPU_CH_RED) << 4) | ((a - GPU_CH_RED) << 6));
            }

            GpuTevColorChan GetR() const {
                return GpuTevColorChan(GPU_CH_RED + ((swap >> 0) & 0x3));
            }

            GpuTevColorChan GetG() const {
                return GpuTevColorChan(GPU_CH_RED + ((swap >> 2) & 0x3));
            }

            GpuTevColorChan GetB() const {
                return GpuTevColorChan(GPU_CH_RED + ((swap >> 4) & 0x3));
            }

            GpuTevColorChan GetA() const {
                return GpuTevColorChan(GPU_CH_RED + ((swap >> 6) & 0x3));
            }

            u8 swap;
        };

        struct AlphaCompare {
            AlphaCompare() {
                Set(GPU_ALWAYS, 0, GPU_AOP_AND, GPU_ALWAYS, 0);
            }

            AlphaCompare(GpuCompare aComp0, u8 aRef0, GpuAlphaOp aOp, GpuCompare aComp1, u8 aRef1) {
                Set(aComp0, aRef0, aOp, aComp1, aRef1);
            }

            void Set(GpuCompare aComp0, u8 aRef0, GpuAlphaOp aOp, GpuCompare aComp1, u8 aRef1) {
                comp = u8(aComp0 << 0 | aComp1 << 4);
                op = aOp;
                ref0 = aRef0;
                ref1 = aRef1;
            }

            GpuCompare GetComp0() const {
                return GpuCompare((comp >> 0) & 0xF);
            }

            u8 GetRef0() const {
                return ref0;
            }

            GpuAlphaOp GetOp() const {
                return GpuAlphaOp(op);
            }

            GpuCompare GetComp1() const {
                return GpuCompare((comp >> 4) & 0xF);
            }

            u8 GetRef1() const {
                return ref1;
            }

            u8 comp;
            u8 op;
            u8 ref0;
            u8 ref1;
        };

        struct BlendMode {
            BlendMode() {
                Set(GPU_BM_BLEND, GPU_BL_SRCALPHA, GPU_BL_INVSRCALPHA, GPU_LO_SET);
            }

            BlendMode(GpuBlendMode aType, GpuBlendFactor aSrcFactor, GpuBlendFactor aDstFactor, GpuLogicOp aOp) {
                Set(aType, aSrcFactor, aDstFactor, aOp);
            }

            void Set(GpuBlendMode aType, GpuBlendFactor aSrcFactor, GpuBlendFactor aDstFactor, GpuLogicOp aOp) {
                type = aType;
                srcFactor = aSrcFactor;
                dstFactor = aDstFactor;
                op = aOp;
            }

            GpuBlendMode GetType() const {
                return GpuBlendMode(type);
            }

            GpuBlendFactor GetSrcFactor() const {
                return GpuBlendFactor(srcFactor);
            }

            GpuBlendFactor GetDstFactor() const {
                return GpuBlendFactor(dstFactor);
            }

            GpuLogicOp GetOp() const {
                return GpuLogicOp(op);
            }

            u8 type;
            u8 srcFactor;
            u8 dstFactor;
            u8 op;
        };

        class AnimTransform;

        class AnimationLink {
        public:
            AnimationLink() : mbDisable(false) {
                Reset();
            }

            void Reset() {
                Set(nullptr, 0);
            }

            void Set(AnimTransform* pTrans, u16 idx) {
                mAnimTrans = pTrans;
                mIdx = idx;
            }

            bool IsEnable() const {
                return !mbDisable;
            }

            void SetEnable(bool enable) {
                mbDisable = !enable;
            }

            u16 GetIndex() const {
                return mIdx;
            }
            AnimTransform* GetAnimTransform() const {
                return mAnimTrans;
            }

            ut::LinkListNode mLink;
            AnimTransform* mAnimTrans;
            u16 mIdx;
            bool mbDisable;
        };

        typedef ut::LinkList< AnimationLink, offsetof(AnimationLink, mLink) > AnimationList;

        struct AnimationGroupRef {
            AnimationGroupRef() : flag(0) {
                memset(name, 0, sizeof(name));
                memset(padding, 0, sizeof(padding));
            }

            const char* GetName() const {
                return name;
            }

            char name[17];
            u8 flag;
            u8 padding[2];
        };

        struct AnimationShareInfo {
            const char* GetSrcPaneName() const {
                return srcPaneName;
            }

            const char* GetTargetGroupName() const {
                return targetGroupName;
            }

            char srcPaneName[17];
            char targetGroupName[17];
            u8 padding[2];
        };
    };  // namespace lyt
};  // namespace nw4r
