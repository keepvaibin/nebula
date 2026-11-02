#include "nw4r/lyt/common.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include "nw4r/lyt/texMap.h"
#include <NebulaGpuEnums.h>
#include <NebulaGpuTypes.h>

namespace nw4r {
    namespace lyt {
        void TexMap::Get(J3DNativeTexObj* pTexObj) const {
            J3DNativeTexture::init(pTexObj, mImage, mWidth, mHeight, GetWrapModeS(), GetWrapModeT());
            J3DNativeTexture::setFilter(pTexObj, GetMinFilter(), GetMagFilter(), GetMinLOD(), GetMaxLOD(), GetLODBias(),
                                        IsEdgeLODEnable(), GetAnisotropy());
        }

        void TexMap::Set(const J3DNativeTexObj& texObj) {
            mImage = const_cast< void* >(texObj.mImage);
            SetSize(texObj.mWidth, texObj.mHeight);
            SetTexelFormat(TEXEL_I4);
            SetWrapMode(texObj.mWrapS, texObj.mWrapT);
            SetMipMap(false);
            SetFilter(texObj.mMinFilter, texObj.mMagFilter);
            SetLOD(texObj.mMinLod, texObj.mMaxLod);
            SetLODBias(texObj.mLodBias);
            SetBiasClampEnable(false);
            SetEdgeLODEnable(texObj.mEdgeLod != 0);
            mBits.anisotropy = texObj.mMaxAniso;
        }

        // A cooked TPL file (nebula-import): big-endian fields, offsets from the file's start. Its header holds the
        // version, the image count and the descriptor table; a descriptor names an image header, whose data is the
        // image's texture record (nebula_cooked_texture.h; a colour-indexed image has its palette applied). Read in
        // place: nothing is relocated.
        void TexMap::ReplaceImage(const void* pTplFile, u32 id) {
            const u8* const file = static_cast< const u8* >(pTplFile);
            const u32 cVersion = 2142000;
            if (*reinterpret_cast< const u32* >(file) != cVersion) {
                NEBULA_PANIC("invalid version number for texture palette (the importer cooks it to host order)");
            }
            const u32 count = *reinterpret_cast< const u32* >(file + 4);
            const u8* const descriptor = file + *reinterpret_cast< const u32* >(file + 8) + (id % count) * 8;

            const u8* const header = file + *reinterpret_cast< const u32* >(descriptor);
            const u16 height = *reinterpret_cast< const u16* >(header);
            const u16 width = *reinterpret_cast< const u16* >(header + 2);
            const u32 format = *reinterpret_cast< const u32* >(header + 4);
            void* const image = const_cast< u8* >(file + *reinterpret_cast< const u32* >(header + 8));
            const u8 maxLOD = header[34];
            mImage = image;
            J3DNativeTexture::registerRecord(image);
            SetSize(width, height);
            mBits.textureFormat = format;
        }
    };  // namespace lyt
};  // namespace nw4r
