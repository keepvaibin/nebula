#include "nw4r/lyt/common.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include "nw4r/lyt/texMap.h"
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXGet.h>
#include <revolution/gx/GXStruct.h>
#include <revolution/tpl.h>

namespace nw4r {
    namespace lyt {
        void TexMap::Get(_GXTexObj* pTexObj) const {
            if (detail::IsCITexelFormat(GetTexelFormat())) {
                u32 tlutName = J3DNativeTexture::getTlut(pTexObj);
                J3DNativeTexture::initCI(pTexObj, mImage, mWidth, mHeight, GXCITexFmt(GetTexelFormat()), GetWrapModeS(), GetWrapModeT(), IsMipMap(), tlutName);
            } else {
                J3DNativeTexture::init(pTexObj, mImage, mWidth, mHeight, GetTexelFormat(), GetWrapModeS(), GetWrapModeT(), IsMipMap());
            }

            J3DNativeTexture::initLOD(pTexObj, GetMinFilter(), GetMagFilter(), GetMinLOD(), GetMaxLOD(), GetLODBias(), IsBiasClampEnable(), IsEdgeLODEnable(),
                            GetAnisotropy());
        }

        void TexMap::Get(_GXTlutObj* pTlutObj) const {
            J3DNativeTexture::initTlut(pTlutObj, GetPalette(), GetPaletteFormat(), GetPaletteEntryNum());
        }

        void TexMap::Set(const GXTexObj& texObj) {
            void* image;
            u16 width, height;
            GXTexFmt format;
            GXTexWrapMode wrapS, wrapT;
            GXBool mipmap;

            J3DNativeTexture::getAll(&texObj, &image, &width, &height, &format, &wrapS, &wrapT, &mipmap);

            mImage = image;
            SetSize(width, height);
            mBits.textureFormat = format;
            SetWrapMode(wrapS, wrapT);
            SetMipMap(mipmap);

            GXTexFilter minFilter, magFilter;
            f32 minLOD, maxLOD, lodBias;
            GXBool biasCLampEnable, edgeLODEnable;
            GXAnisotropy aniso;
            J3DNativeTexture::getLODAll(&texObj, &minFilter, &magFilter, &minLOD, &maxLOD, &lodBias, &biasCLampEnable, &edgeLODEnable, &aniso);

            SetFilter(minFilter, magFilter);
            SetLOD(minLOD, maxLOD);
            SetLODBias(lodBias);
            SetBiasClampEnable(biasCLampEnable);
            SetEdgeLODEnable(edgeLODEnable);
            mBits.anisotropy = aniso;
        }

        void TexMap::ReplaceImage(const TPLDescriptor* pTPLDesc) {
            const TPLHeader& header = *pTPLDesc->textureHeader;
            mImage = header.data;
            J3DNativeTexture::registerImage(header.data, static_cast< u8 >(header.format), header.width, header.height,
                                            static_cast< u8 >(header.maxLOD + 1));
            SetSize(header.width, header.height);
            SetTexelFormat(GXTexFmt(u32(header.format)));

            if (const TPLClutHeader* const pClut = pTPLDesc->CLUTHeader) {
                SetPalette(pClut->data);
                SetPaletteFormat(pClut->format);
                SetPaletteEntryNum(pClut->numEntries);
            } else {
                SetPalette(nullptr);
                SetPaletteFormat(GXTlutFmt(0));
                SetPaletteEntryNum(0);
            }
        }

        void TexMap::ReplaceImage(TPLPalette* p, u32 id) {
#ifdef TARGET_PC
            // Natively TPLBind itself tells a bound palette from one still in file form: the console's test, a
            // descriptor pointer below MEM1 (an offset), does not hold for host addresses.
            TPLBind(p);
#else
            if (reinterpret_cast< u32 >(p->descriptorArray) < 0x80000000) {
                TPLBind(p);
            }
#endif

            ReplaceImage(TPLGet(p, id));
        }
    };  // namespace lyt
};  // namespace nw4r
