#pragma once


#include <nebula_geometry.hpp>
#include "Game/Util/Color.hpp"
#include "Game/Ui/Font.hpp"

namespace Ui {
    class Font;

    class CharWriter {
    public:
        enum GradationMode { GRADMODE_NONE, GRADMODE_H, GRADMODE_V, };

        CharWriter();
        ~CharWriter();

        typedef Vec2f CharScale;
        typedef Vec3f CursorPos;

        static const u32 DEFAULT_COLOR_MAPPING_MIN = 0;
        static const u32 DEFAULT_COLOR_MAPPING_MAX = 0xFFFFFFFF;

        struct ColorMapping {
            Color8 min;
            Color8 max;
        };

        struct TextColor {
            Color8 start;
            Color8 end;
            GradationMode gradationMode;
        };

        struct VertexColor {
            Color8 lu;
            Color8 ru;
            Color8 ld;
            Color8 rd;
        };

        class TextureFilter {
        public:
            bool operator!=(const TextureFilter& rhs) const {
                return (atSmall != rhs.atSmall) || (atLarge != rhs.atLarge);
            }

            u8 atSmall;
            u8 atLarge;
        };

        class LoadingTexture {
        public:
            u8 slot;
            const void* texture;
            TextureFilter filter;

            void Reset() {
                slot = 0xFF;
                texture = nullptr;
            }

            bool operator!=(const LoadingTexture& rhs) const {
                return (slot != rhs.slot) || (texture != rhs.texture) || (filter != rhs.filter);
            }
        };

        void SetCursor(f32 x, f32 y) {
            mCursorPos.x = x;
            mCursorPos.y = y;
        }

        void SetCursor(f32 x, f32 y, f32 z) {
            mCursorPos.x = x;
            mCursorPos.y = y;
            mCursorPos.z = z;
        }

        void SetScale(f32 hScale, f32 vScale) {
            mScale.x = hScale;
            mScale.y = vScale;
        }

        void SetScale(f32 hvScale) {
            mScale.x = hvScale;
            mScale.y = hvScale;
        }

        void SetGradationMode(GradationMode mode) {
            mTextColor.gradationMode = mode;
            UpdateVertexColor();
        }

        void Setup();
        void SetupForIntensity();
        void SetupDefault();
        void SetupForColor();
        void SetupWithColorMapping(Color8, Color8);
        // The font's distance range (Font::GetFieldRange), 0 without a font.
        u8 FieldRange() const;

        void ResetTextureCache() {
            mLoadingTexture.Reset();
        }

        void EnableLinearFilter(bool, bool);

        void SetTextColor(Color8 start, Color8 end) {
            mTextColor.start = start;
            mTextColor.end = end;
            UpdateVertexColor();
        }

        f32 GetFontWidth() const;
        f32 GetFontHeight() const;
        void SetFontSize(f32 width, f32 height);
        void LoadTexture(const Glyph&, u8);
        f32 GetFontAscent() const;
        f32 Print(CharCode);
        void PrintGlyph(f32, f32, f32, const Glyph&);
        void UpdateVertexColor();
        static void SetupVertexFormat();
        // Sends the glyphs printed so far (PrintGlyph gathers a run of them into one draw); every Print ends with it.
        static void EndRun();

        void SetFont(const Font& font) {
            mFont = &font;
        }

        const Font* GetFont() const {
            return mFont;
        }

        void SetColorMapping(Color8 min, Color8 max) {
            mColorMapping.min = min;
            mColorMapping.max = max;
        }

        void ResetColorMapping() {
            SetColorMapping(DEFAULT_COLOR_MAPPING_MIN, DEFAULT_COLOR_MAPPING_MAX);
        }

        void SetAlpha(u8 alpha) {
            mAlpha = alpha;
            UpdateVertexColor();
        }

        u8 GetAlpha() const {
            return mAlpha;
        }

        void SetTextColor(Color8 color) {
            mTextColor.start = color;
            UpdateVertexColor();
        }

        const Color8 GetTextColor() const {
            return mTextColor.start;
        }

        f32 GetScaleH() const {
            return mScale.x;
        }

        f32 GetScaleV() const {
            return mScale.y;
        }

        bool IsWidthFixed() const {
            return mIsWidthFixed;
        }

        f32 GetFixedWidth() const {
            return mFixedWidth;
        }

        void SetCursorX(f32 x) {
            mCursorPos.x = x;
        }

        void SetCursorY(f32 y) {
            mCursorPos.y = y;
        }

        void MoveCursorX(f32 dx) {
            mCursorPos.x += dx;
        }

        void MoveCursorY(f32 dy) {
            mCursorPos.y += dy;
        }

        f32 GetCursorX() const {
            return mCursorPos.x;
        }

        f32 GetCursorY() const {
            return mCursorPos.y;
        }

        static LoadingTexture mLoadingTexture;

        ColorMapping mColorMapping;
        VertexColor mVertexColor;
        TextColor mTextColor;
        CharScale mScale;
        CursorPos mCursorPos;
        TextureFilter mFilter;
        u8 mAlpha;
        bool mIsWidthFixed;
        f32 mFixedWidth;
        const Font* mFont;
    };
}  // namespace Ui
