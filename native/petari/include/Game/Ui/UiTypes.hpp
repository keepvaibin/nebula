#pragma once

#include "Game/Ui/UiList.hpp"
#include <nebula_geometry.hpp>
#include <cstddef>
#include <cstring>
#include <NebulaGpu.h>
#include <nebula_game_api.h>
#include <nebula_math.h>

namespace Ui {
    // A pane's texture coordinates: four corners per coordinate set.
    typedef Vec2f (*TexCoordQuad)[4];
    typedef const Vec2f (*ConstTexCoordQuad)[4];



    // A name of any length: N characters with the terminator in itself, a longer one in a new array. It reads as a
    // const char*.
    template < u32 N >
    class NameText {
    public:
        NameText() : mText(mInline) {
            mInline[0] = '\0';
        }

        ~NameText() {
            if (mText != mInline) {
                delete[] mText;
            }
        }

        NameText(const NameText&) = delete;
        NameText& operator=(const NameText&) = delete;

        // pName may lie in this name's own text: it is copied before the old text goes.
        void set(const char* pName) {
            u32 length = 0;
            while (pName[length] != '\0') {
                length++;
            }

            char* const pOld = mText != mInline ? mText : nullptr;
            char* const pNew = length < N ? mInline : new char[length + 1];
            for (u32 i = 0; i <= length; i++) {
                pNew[i] = pName[i];
            }

            mText = pNew;
            delete[] pOld;
        }

        operator const char*() const {
            return mText;
        }

    private:
        char* mText;
        char mInline[N];
    };

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

        };
    };  // namespace detail

    struct Size {
        Size() : width(0.0f), height(0.0f) {
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
            Set(MODEL_FILE_SOURCE_VERTEX, MODEL_FILE_SOURCE_VERTEX);
        }

        ChanCtrl(u8 colSrc, u8 alpSrc) : reserve1(0), reserve2(0) {
            Set(colSrc, alpSrc);
        }

        void Set(u8 colSrc, u8 alpSrc) {
            matSrcCol = colSrc;
            matSrcAlp = alpSrc;
        }

        u8 GetColorSrc() const {
            return u8(matSrcCol);
        }

        u8 GetAlphaSrc() const {
            return u8(matSrcAlp);
        }

        u8 matSrcCol;
        u8 matSrcAlp;
        u8 reserve1;
        u8 reserve2;
    };

    struct TexSRT {
        Vec2f translate;
        f32 rotate;
        Vec2f scale;
    };

    struct TexCoordGen {
        TexCoordGen() : reserve(0) {
            Set(MODEL_FILE_TEXGEN_MATRIX_2x4, MODEL_FILE_TEXGEN_SOURCE_TEX0, MODEL_FILE_MATRIX_IDENTITY);
        }

        TexCoordGen(u8 aTexGenType, u8 aTexGenSrc, u32 aTexMtx)
            : reserve(0)

        {
            Set(aTexGenType, aTexGenSrc, aTexMtx);
        }

        void Set(u8 aTexGenType, u8 aTexGenSrc, u32 aTexMtx) {
            texGenType = aTexGenType;
            texGenSrc = aTexGenSrc;
            texMtx = u8(aTexMtx);
        }

        u8 GetTexGenType() const {
            return u8(texGenType);
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
            Set(NEBULA_COMPARE_ALWAYS, 0, MODEL_FILE_ALPHA_OP_AND, NEBULA_COMPARE_ALWAYS, 0);
        }

        AlphaCompare(u8 aComp0, u8 aRef0, u8 aOp, u8 aComp1, u8 aRef1) {
            Set(aComp0, aRef0, aOp, aComp1, aRef1);
        }

        void Set(u8 aComp0, u8 aRef0, u8 aOp, u8 aComp1, u8 aRef1) {
            comp = u8(aComp0 << 0 | aComp1 << 4);
            op = aOp;
            ref0 = aRef0;
            ref1 = aRef1;
        }

        u8 GetRef0() const {
            return ref0;
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
            Set(MODEL_FILE_BLEND_ADD, MODEL_FILE_FACTOR_SRC_ALPHA, MODEL_FILE_FACTOR_INV_SRC_ALPHA, 0);
        }

        BlendMode(u8 aType, u8 aSrcFactor, u8 aDstFactor, u8 aOp) {
            Set(aType, aSrcFactor, aDstFactor, aOp);
        }

        void Set(u8 aType, u8 aSrcFactor, u8 aDstFactor, u8 aOp) {
            type = aType;
            srcFactor = aSrcFactor;
            dstFactor = aDstFactor;
            op = aOp;
        }

        u8 GetType() const {
            return u8(type);
        }

        u8 GetSrcFactor() const {
            return u8(srcFactor);
        }

        u8 GetDstFactor() const {
            return u8(dstFactor);
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

        UiLink< AnimationLink > mLink;
        AnimTransform* mAnimTrans;
        u16 mIdx;
        bool mbDisable;
    };

    typedef UiList< AnimationLink > AnimationList;
}  // namespace Ui
