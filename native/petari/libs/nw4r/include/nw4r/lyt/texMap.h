#pragma once

#include "nw4r/lyt/types.h"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <NebulaGpu.h>

namespace nw4r {
    namespace lyt {
        class TexMap {
        public:
            TexMap() {
                SetImage(NULL);
                SetSize(0, 0);
                SetTexelFormat(GPU_TF_I4);
                SetWrapMode(GPU_CLAMP, GPU_CLAMP);
                SetMipMap(false);
                SetFilter(GPU_LINEAR, GPU_LINEAR);
                SetLOD(0.0f, 0.0f);
                SetLODBias(0.0f);
                SetBiasClampEnable(false);
                SetEdgeLODEnable(false);
                SetAnisotropy(GPU_ANISO_1);
            }

            TexMap(const J3DNativeTexObj& rTexObj) {
                Set(rTexObj);
            }

            void SetImage(void* pImage) {
                mImage = pImage;
            }

            void SetAnisotropy(GpuAnisotropy aniso) {
                mBits.anisotropy = aniso;
            }

            void Get(J3DNativeTexObj*) const;
            void Set(const J3DNativeTexObj&);
            // Image `id` of a cooked TPL file (its image a texture record).
            void ReplaceImage(const void* pTplFile, u32 id = 0);

            inline GpuTexFmt GetTexelFormat() const {
                return GpuTexFmt(mBits.textureFormat);
            }

            inline GpuTexWrapMode GetWrapModeS() const {
                return GpuTexWrapMode(mBits.wrapS);
            }

            inline GpuTexWrapMode GetWrapModeT() const {
                return GpuTexWrapMode(mBits.wrapT);
            }

            inline GpuTexFilter GetMinFilter() const {
                return GpuTexFilter(mBits.minFilter);
            }

            inline GpuTexFilter GetMagFilter() const {
                return GpuTexFilter(mBits.magFilter);
            }

            inline bool IsBiasClampEnable() const {
                return 0 != mBits.biasClampEnable;
            }

            inline bool IsEdgeLODEnable() const {
                return 0 != mBits.edgeLODEnable;
            }

            inline GpuAnisotropy GetAnisotropy() const {
                return GpuAnisotropy(mBits.anisotropy);
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

            void SetWrapMode(GpuTexWrapMode wrapS, GpuTexWrapMode wrapT) {
                mBits.wrapS = wrapS;
                mBits.wrapT = wrapT;
            }

            void SetFilter(GpuTexFilter minFlt, GpuTexFilter magFlt) {
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

            void SetTexelFormat(GpuTexFmt value) {
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
