#pragma once


#include "nw4r/math/types.h"
#include "nw4r/ut/Color.h"
#include "nw4r/ut/Font.h"

namespace nw4r {
    namespace ut {
        class Font;

        class CharWriter {
        public:
            enum GradationMode { GRADMODE_NONE, GRADMODE_H, GRADMODE_V, NUM_OF_GRADMODE };

            CharWriter();
            ~CharWriter();

            typedef math::VEC2 CharScale;
            typedef math::VEC3 CursorPos;

            static const u32 DEFAULT_COLOR_MAPPING_MIN = 0;
            static const u32 DEFAULT_COLOR_MAPPING_MAX = 0xFFFFFFFF;

            static const int TEXTURE_COODINATE_FRACTION_BITS = 15;
            static const u32 TEXTURE_COODINATE_ONE = (1 << TEXTURE_COODINATE_FRACTION_BITS);

            struct ColorMapping {
                Color min;
                Color max;
            };

            struct TextColor {
                Color start;
                Color end;
                GradationMode gradationMode;
            };

            struct VertexColor {
                Color lu;
                Color ru;
                Color ld;
                Color rd;
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
            static void SetupWithColorMapping(Color, Color);

            void ResetTextureCache() {
                mLoadingTexture.Reset();
            }

            void EnableLinearFilter(bool, bool);

            void SetTextColor(Color start, Color end) {
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

            void SetFont(const Font& font) {
                mFont = &font;
            }

            const Font* GetFont() const {
                return mFont;
            }

            void SetColorMapping(Color min, Color max) {
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

            void SetTextColor(Color color) {
                mTextColor.start = color;
                UpdateVertexColor();
            }

            const Color GetTextColor() const {
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
            u8 pad[2];
            u8 mAlpha;
            bool mIsWidthFixed;
            f32 mFixedWidth;
            const Font* mFont;
        };
    };  // namespace ut
};  // namespace nw4r
