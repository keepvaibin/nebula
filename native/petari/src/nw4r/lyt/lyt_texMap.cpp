#include "nw4r/lyt/common.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include "nw4r/lyt/texMap.h"
#include <NebulaGpuEnums.h>
#include <NebulaGpuTypes.h>

namespace nw4r {
    namespace lyt {
        void TexMap::Get(_GpuTexObj* pTexObj) const {
            J3DNativeTexture::init(pTexObj, mImage, mWidth, mHeight, GetTexelFormat(), GetWrapModeS(), GetWrapModeT(), IsMipMap());

            J3DNativeTexture::initLOD(pTexObj, GetMinFilter(), GetMagFilter(), GetMinLOD(), GetMaxLOD(), GetLODBias(), IsBiasClampEnable(), IsEdgeLODEnable(),
                            GetAnisotropy());
        }

        void TexMap::Set(const GpuTexObj& texObj) {
            void* image;
            u16 width, height;
            GpuTexFmt format;
            GpuTexWrapMode wrapS, wrapT;
            GpuBool mipmap;

            J3DNativeTexture::getAll(&texObj, &image, &width, &height, &format, &wrapS, &wrapT, &mipmap);

            mImage = image;
            SetSize(width, height);
            mBits.textureFormat = format;
            SetWrapMode(wrapS, wrapT);
            SetMipMap(mipmap);

            GpuTexFilter minFilter, magFilter;
            f32 minLOD, maxLOD, lodBias;
            GpuBool biasCLampEnable, edgeLODEnable;
            GpuAnisotropy aniso;
            J3DNativeTexture::getLODAll(&texObj, &minFilter, &magFilter, &minLOD, &maxLOD, &lodBias, &biasCLampEnable, &edgeLODEnable, &aniso);

            SetFilter(minFilter, magFilter);
            SetLOD(minLOD, maxLOD);
            SetLODBias(lodBias);
            SetBiasClampEnable(biasCLampEnable);
            SetEdgeLODEnable(edgeLODEnable);
            mBits.anisotropy = aniso;
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
            J3DNativeTexture::registerImage(image, static_cast< u8 >(format), width, height, static_cast< u8 >(maxLOD + 1));
            SetSize(width, height);
            mBits.textureFormat = format;
        }
    };  // namespace lyt
};  // namespace nw4r
