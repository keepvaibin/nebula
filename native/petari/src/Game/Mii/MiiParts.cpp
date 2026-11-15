#include "Game/Mii/MiiParts.hpp"
#include <nebula_cooked_texture.h>
#include <string.h>

namespace {
    const u32 cMagic = 0x54504D4Eu;  // "NMPT"
    const u32 cVersion = 1;
    const u32 cHeader = 8 + 4 * (Mii::ShapeKindCount + Mii::TextureKindCount);

    u32 read32(const u8* pFile, u32 at) {
        u32 value;
        memcpy(&value, pFile + at, sizeof(value));
        return value;
    }
}  // namespace

namespace Mii {
    // Every table, shape and texture is checked once here, so the reads after are plain.
    bool Parts::init(const void* pFile, u32 size) {
        const u8* pBytes = static_cast< const u8* >(pFile);

        mFile = nullptr;
        if (pBytes == nullptr || size < cHeader || read32(pBytes, 0) != cMagic || read32(pBytes, 4) != cVersion) {
            return false;
        }
        for (u32 kind = 0; kind < ShapeKindCount + TextureKindCount; kind++) {
            const u32 table = read32(pBytes, 8 + 4 * kind);
            if (table > size - 4 || (table & 3) != 0) {
                return false;
            }
            const u32 count = read32(pBytes, table);
            if (count > (size - table - 4) / 4) {
                return false;
            }
            for (u32 file = 0; file < count; file++) {
                const u32 at = read32(pBytes, table + 4 + 4 * file);
                if (at == 0) {
                    continue;
                }
                if (kind < ShapeKindCount) {
                    if ((at & 15) != 0 || at > size - sizeof(Shape)) {
                        return false;
                    }
                    const Shape* pShape = reinterpret_cast< const Shape* >(pBytes + at);
                    const u64 bytes = sizeof(Shape) + u64(pShape->vertexCount) * sizeof(PartVertex) + u64(pShape->indexCount) * 4;
                    if (bytes > size - at || pShape->indexCount % 3 != 0) {
                        return false;
                    }
                    const u32* pIndices = pShape->indices();
                    for (u32 i = 0; i < pShape->indexCount; i++) {
                        if (pIndices[i] >= pShape->vertexCount) {
                            return false;
                        }
                    }
                } else {
                    if ((at & 63) != 0 || at > size - sizeof(NebulaTextureInfo)) {
                        return false;
                    }
                    const NebulaTextureInfo* pInfo = reinterpret_cast< const NebulaTextureInfo* >(pBytes + at);
                    if (pInfo->magic != NEBULA_TEXTURE_INFO_MAGIC || pInfo->version != NEBULA_TEXTURE_INFO_VERSION ||
                        pInfo->image_at > size - at - sizeof(NebulaTexture)) {
                        return false;
                    }
                    const NebulaTexture* pRecord = nebula_texture_record(pBytes + at + pInfo->image_at);
                    if (pRecord == nullptr || pRecord->size > size - at - pInfo->image_at) {
                        return false;
                    }
                }
            }
        }
        mFile = pBytes;
        mSize = size;
        return true;
    }

    const void* Parts::at(u32 kind, u32 file) const {
        if (mFile == nullptr) {
            return nullptr;
        }
        const u32 table = read32(mFile, 8 + 4 * kind);
        if (file >= read32(mFile, table)) {
            return nullptr;
        }
        const u32 offset = read32(mFile, table + 4 + 4 * file);
        return offset != 0 ? mFile + offset : nullptr;
    }

    const Shape* Parts::shape(ShapeKind kind, u32 file) const {
        const Shape* pShape = static_cast< const Shape* >(at(kind, file));
        return pShape != nullptr && pShape->indexCount != 0 ? pShape : nullptr;
    }

    const NebulaTextureInfo* Parts::texture(TextureKind kind, u32 file) const {
        return static_cast< const NebulaTextureInfo* >(at(ShapeKindCount + kind, file));
    }
}  // namespace Mii
