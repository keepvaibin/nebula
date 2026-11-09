#pragma once

// The Mii parts as the importer cooks them (crates/nebula-import/src/cook/mii_parts.rs): MiiParts.npt in
// MiiFaceDatabase.arc, read in place. Shapes are triangle lists of position, normal and texture coordinate; textures are
// texture records after their NebulaTextureInfo.

#include <nebula_cooked_texture.h>
#include <nebula_types.h>

namespace Mii {
    // The shapes a head is made of, in the file's order.
    enum ShapeKind {
        ShapeNose,
        ShapeForehead,
        ShapeFaceLine,
        ShapeHair,
        ShapeCap,
        ShapeBeard,
        ShapeNoseLine,
        ShapeMask,
        ShapeGlasses,
        ShapeKindCount
    };

    // The part textures, in the file's order: the face texture's parts (eye to mole) and the shapes' own.
    // The flag bit of a shape record in the cooked Mii parts.
    enum TextureKind {
        TextureEye,
        TextureEyebrow,
        TextureMouth,
        TextureMustache,
        TextureMole,
        TextureFace,
        TextureCap,
        TextureNoseLine,
        TextureGlasses,
        TextureKindCount
    };

    struct PartVertex {
        f32 x, y, z;
        f32 nx, ny, nz;
        f32 s, t;
    };

    struct Shape {
        u32 vertexCount;
        u32 indexCount;
        u32 flags;  // ShapeHasCoordinates
        u32 reserved;
        f32 nosePlace[3];   // the face line's places for the nose, beard and hair; zero in other shapes
        f32 beardPlace[3];
        f32 hairPlace[3];
        f32 reserved2[3];

        const PartVertex* vertices() const { return reinterpret_cast< const PartVertex* >(this + 1); }
        const u32* indices() const { return reinterpret_cast< const u32* >(vertices() + vertexCount); }
    };

    enum { ShapeHasCoordinates = 1 };

    class Parts {
    public:
        // `pFile` and `size`: the cooked file, kept for as long as the parts are used. False when it is not one.
        bool init(const void* pFile, u32 size);
        bool isReady() const { return mFile != nullptr; }

        // Null for a file the kind does not have or that is empty.
        const Shape* shape(ShapeKind kind, u32 file) const;
        const NebulaTextureInfo* texture(TextureKind kind, u32 file) const;

    private:
        const void* at(u32 table, u32 file) const;

        const u8* mFile = nullptr;
        u32 mSize = 0;
    };
}  // namespace Mii
