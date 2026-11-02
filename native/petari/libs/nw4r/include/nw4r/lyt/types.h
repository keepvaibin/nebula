#pragma once

#include "nw4r/math/types.h"
#include "nw4r/ut/LinkList.h"
#include <cstddef>
#include <cstring>
#include <NebulaGpu.h>
#include <nebula_math.h>

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
