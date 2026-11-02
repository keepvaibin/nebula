#pragma once

#include <nebula_cooked_texture.h>
#include "nw4r/lyt/types.h"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <NebulaGpu.h>

namespace nw4r {
    namespace lyt {
        class TexMap {
        public:
            // The picture's format as a layout's file names it.
            enum TexelFormat { TEXEL_I4 = 0, TEXEL_IA4 = 2, TEXEL_RGBA8 = 6 };

            TexMap() {
                SetImage(NULL);
                SetSize(0, 0);
                SetTexelFormat(TEXEL_I4);
                SetWrapMode(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
                SetMipMap(false);
                SetFilter(NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
                SetLOD(0.0f, 0.0f);
                SetLODBias(0.0f);
                SetBiasClampEnable(false);
                SetEdgeLODEnable(false);
                SetAnisotropy(0);
            }

            TexMap(const J3DNativeTexObj& rTexObj) {
                Set(rTexObj);
            }

            void SetImage(void* pImage) {
                mImage = pImage;
            }

            void SetAnisotropy(u8 aniso) {
                mBits.anisotropy = aniso;
            }

            void Get(J3DNativeTexObj*) const;
            void Set(const J3DNativeTexObj&);
            // Image `id` of a cooked TPL file (its image a texture record).
            void ReplaceImage(const void* pTplFile, u32 id = 0);

            inline u8 GetTexelFormat() const {
                return u8(mBits.textureFormat);
            }

            inline u8 GetWrapModeS() const {
                return u8(mBits.wrapS);
            }

            inline u8 GetWrapModeT() const {
                return u8(mBits.wrapT);
            }

            inline u8 GetMinFilter() const {
                return u8(mBits.minFilter);
            }

            inline u8 GetMagFilter() const {
                return u8(mBits.magFilter);
            }

            inline bool IsBiasClampEnable() const {
                return 0 != mBits.biasClampEnable;
            }

            inline bool IsEdgeLODEnable() const {
                return 0 != mBits.edgeLODEnable;
            }

            inline u8 GetAnisotropy() const {
                return u8(mBits.anisotropy);
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

            void SetWrapMode(u8 wrapS, u8 wrapT) {
                mBits.wrapS = wrapS;
                mBits.wrapT = wrapT;
            }

            void SetFilter(u8 minFlt, u8 magFlt) {
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

            void SetTexelFormat(u8 value) {
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

            struct {
                u32 textureFormat : 4;
                u32 mipmap : 1;
                u32 wrapS : 2;
                u32 wrapT : 2;
                u32 minFilter : 3;
                u32 magFilter : 1;
                u32 biasClampEnable : 1;
                u32 edgeLODEnable : 1;
                u32 anisotropy : 2;
            } mBits;
        };
    };  // namespace lyt
};  // namespace nw4r
