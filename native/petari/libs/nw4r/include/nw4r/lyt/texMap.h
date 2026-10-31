#pragma once

#include "nw4r/lyt/types.h"
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include <revolution/gx.h>

namespace nw4r {
    namespace lyt {
        class TexMap {
        public:
            TexMap() {
                SetImage(NULL);
                SetSize(0, 0);
                SetTexelFormat(GX_TF_I4);
                SetWrapMode(GX_CLAMP, GX_CLAMP);
                SetMipMap(false);
                SetFilter(GX_LINEAR, GX_LINEAR);
                SetLOD(0.0f, 0.0f);
                SetLODBias(0.0f);
                SetBiasClampEnable(false);
                SetEdgeLODEnable(false);
                SetAnisotropy(GX_ANISO_1);
            }

            TexMap(const GXTexObj& rTexObj) {
                Set(rTexObj);
            }

            void SetImage(void* pImage) {
                mImage = pImage;
            }

            void SetAnisotropy(GXAnisotropy aniso) {
                mBits.anisotropy = aniso;
            }

            void Get(_GXTexObj*) const;
            void Set(const _GXTexObj&);
            // Image `id` of a cooked TPL file (its image a texture record).
            void ReplaceImage(const void* pTplFile, u32 id = 0);

            inline GXTexFmt GetTexelFormat() const {
                return GXTexFmt(mBits.textureFormat);
            }

            inline GXTexWrapMode GetWrapModeS() const {
                return GXTexWrapMode(mBits.wrapS);
            }

            inline GXTexWrapMode GetWrapModeT() const {
                return GXTexWrapMode(mBits.wrapT);
            }

            inline GXTexFilter GetMinFilter() const {
                return GXTexFilter(mBits.minFilter);
            }

            inline GXTexFilter GetMagFilter() const {
                return GXTexFilter(mBits.magFilter);
            }

            inline bool IsBiasClampEnable() const {
                return 0 != mBits.biasClampEnable;
            }

            inline bool IsEdgeLODEnable() const {
                return 0 != mBits.edgeLODEnable;
            }

            inline GXAnisotropy GetAnisotropy() const {
                return GXAnisotropy(mBits.anisotropy);
            }

            inline f32 GetLODBias() const {
                return mLODBias / 256.0f;
            }

            inline f32 GetMinLOD() const {
                return mMinLOD;
            }

            inline f32 GetMaxLOD() const {
                return mMaxLOD;
            }

            inline bool IsMipMap() const {
                return 0 != mBits.mipmap;
            }

            void SetMipMap(bool mipmap) {
                mBits.mipmap = mipmap;
            }

            void SetLODBias(f32 bias) {
                mLODBias = u16((s32)(bias * 256));
            }

            void SetWrapMode(GXTexWrapMode wrapS, GXTexWrapMode wrapT) {
                mBits.wrapS = wrapS;
                mBits.wrapT = wrapT;
            }

            void SetFilter(GXTexFilter minFlt, GXTexFilter magFlt) {
                mBits.minFilter = minFlt;
                mBits.magFilter = magFlt;
            }

            void SetLOD(f32 minLOD, f32 maxLOD) {
                mMinLOD = minLOD;
                mMaxLOD = maxLOD;
            }

            void SetBiasClampEnable(bool enable) {
                mBits.biasClampEnable = enable;
            }

            void SetEdgeLODEnable(bool enable) {
                mBits.edgeLODEnable = enable;
            }

            void SetSize(u16 width, u16 height) {
                mWidth = width;
                mHeight = height;
            }

            void SetTexelFormat(GXTexFmt value) {
                mBits.textureFormat = value;
            }

            void Set(const TexMap& texMap) {
                *this = texMap;
            }

            const Size GetSize() const {
                return Size(mWidth, mHeight);
            }

            void* mImage;
            u16 mWidth;
            u16 mHeight;
            f32 mMinLOD;
            f32 mMaxLOD;
            u16 mLODBias;

            /* 0x18 */ struct {
                /* 0x18 */ u32 textureFormat : 4;
                /* 0x1C */ u32 mipmap : 1;
                /* 0x20 */ u32 wrapS : 2;
                /* 0x24 */ u32 wrapT : 2;
                /* 0x28 */ u32 minFilter : 3;
                /* 0x2C */ u32 magFilter : 1;
                /* 0x30 */ u32 biasClampEnable : 1;
                /* 0x34 */ u32 edgeLODEnable : 1;
                /* 0x38 */ u32 anisotropy : 2;
            } mBits;
        };
    };  // namespace lyt
};  // namespace nw4r
