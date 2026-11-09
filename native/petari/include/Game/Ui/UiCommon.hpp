#pragma once

#include "Game/Ui/UiData.hpp"
#include "Game/Ui/UiTypes.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Color.hpp"
#include "Game/Ui/UiInlines.hpp"

struct SiteMaterial;

namespace Ui {
    class Material;

    namespace detail {
        inline f32 DegToRad(f32 deg) {
            return deg * 0.01745329252f;
        }

        inline f32 SinDeg(f32 deg) {
            return __builtin_sinf(DegToRad(deg));
        }

        inline f32 CosDeg(f32 deg) {
            return __builtin_cosf(DegToRad(deg));
        }

        inline u8 GetHorizontalPosition(u8 packed) {
            return packed % 3;
        }

        inline u8 GetVerticalPosition(u8 packed) {
            return packed / 3;
        }

        bool IsModulateVertexColor(Color8*, u8);
        const Color8 MultipleAlpha(const Color8, u8);
        void MultipleAlpha(Color8*, const Color8*, u8);
        // What the pane draws with next: its eye matrix, and a written shader or a form with its values.
        void SetDrawMtx(const f32 (*pMtx)[4]);
        const f32 (*GetDrawMtx())[4];
        void SetDrawNothing();
        void SetDrawPicture(const SiteMaterial& rMaterial);
        void SetDrawForm(const UiFormData* pForm, const u8* pInputs, u32 constants, u32 table, u32 state);

        void DrawQuad(const Vec2f&, const Size&, u8, const Vec2f (*)[4], const Color8*);
        void DrawQuad(const Vec2f&, const Size&, u8, const Vec2f (*)[4], const Color8*, u8);

        class TexCoordAry {
        public:
            typedef const Vec2f (*ConstArrayType)[4];
            TexCoordAry();

            void Free();
            void Reserve(u8);
            void SetSize(u8);
            void Copy(const void*, u8);

            bool IsEmpty() const {
                return mCap == 0;
            }
            u8 GetSize() const {
                return mNum;
            }
            ConstArrayType GetArray() const {
                return mData;
            }

            u8 mCap;
            u8 mNum;
            Vec2f (*mData)[4];
        };
    };  // namespace detail

    class ResourceAccessor;

    // The cooked layout a pane is built from (UiData.hpp), and where its images and fonts are found.
    class Pane;
    struct ResBlockSet {
        const UiLayoutHeader* pLayout;
        ResourceAccessor* pResAccessor;
        Pane** ppPanes;  // while a layout builds: its panes so far, by index (groups name theirs by index)

        template < typename T >
        const T* At(u32 offset) const {
            return uiAt< T >(pLayout, offset);
        }
        const char* String(u32 offset) const {
            return At< char >(pLayout->mStrings + offset);
        }
        const UiPaneData* PaneAt(u32 idx) const {
            return At< UiPaneData >(pLayout->mPanes) + idx;
        }
        const UiMaterialData* MaterialAt(u32 idx) const {
            return At< UiMaterialData >(pLayout->mMaterials) + idx;
        }
        const char* TextureName(u32 idx) const {
            return String(At< u32 >(pLayout->mTextures)[idx]);
        }
        const char* FontName(u32 idx) const {
            return String(At< u32 >(pLayout->mFonts)[idx]);
        }
    };

    namespace detail {
        inline Color8 RecordColor(const u8 (&rgba)[4]) {
            return Color8(u32(rgba[0]) << 24 | u32(rgba[1]) << 16 | u32(rgba[2]) << 8 | u32(rgba[3]));
        }

        bool EqualsResName(const char*, const char*);
        bool EqualsMaterialName(const char*, const char*);
        inline u8 GetVtxColorElement(const Color8* pColors, u32 idx) {
            return reinterpret_cast< const u8* >(&pColors[idx / 4])[idx % 4];
        }
        inline void SetVtxColorElement(Color8* pColors, u32 idx, u8 value) {
            reinterpret_cast< u8* >(&pColors[idx / 4])[idx % 4] = value;
        }

        const Size GetTextureSize(Material* pMaterial, u32 texMapIdx);

        inline void SetHorizontalPosition(u8* pVar, u8 newVal) {
            *pVar = u8(GetVerticalPosition(*pVar) * 3 + newVal);
        }

        inline void SetVerticalPosition(u8* pVar, u8 newVal) {
            *pVar = u8(newVal * 3 + GetHorizontalPosition(*pVar));
        }

    };  // namespace detail
}  // namespace Ui
