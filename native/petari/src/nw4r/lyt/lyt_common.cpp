#include "nw4r/lyt/common.h"
#include "Game/Render/TypedDraw.hpp"
#include "nw4r/lyt/layout.h"
#include <cstdio>
#include <string.h>

// A pane's quad: position, colour, as many coordinates as its material reads.
template < u32 N >
struct LayoutQuadVertex {
    f32 x, y, z;
    Rgba8 color;
    f32 uv[N + (N == 0 ? 1 : 0)][2];
};

template < u32 N >
struct VertexLayout< LayoutQuadVertex< N > > {
    static constexpr u32 count = 1 + N;
    static constexpr u16 cStride = sizeof(LayoutQuadVertex< N >);
    static const NebulaLayoutInput* inputs() {
        static constexpr NebulaLayoutInput list[9] = {
            {NEBULA_INPUT_COLOR0, 0, 2, 0, 12, cStride},
            {NEBULA_INPUT_COORD0, 0, 1, 0, 16, cStride},
            {NEBULA_INPUT_COORD0 + 1, 0, 1, 0, 24, cStride},
            {NEBULA_INPUT_COORD0 + 2, 0, 1, 0, 32, cStride},
            {NEBULA_INPUT_COORD0 + 3, 0, 1, 0, 40, cStride},
            {NEBULA_INPUT_COORD0 + 4, 0, 1, 0, 48, cStride},
            {NEBULA_INPUT_COORD0 + 5, 0, 1, 0, 56, cStride},
            {NEBULA_INPUT_COORD0 + 6, 0, 1, 0, 64, cStride},
            {NEBULA_INPUT_COORD0 + 7, 0, 1, 0, 72, cStride},
        };
        return list;
    }
};

namespace {
    enum DrawKind { Draw_Nothing, Draw_Picture, Draw_Form };
    DrawKind sDrawKind = Draw_Nothing;
    Mtx sDrawMtx;
    SiteMaterial sDrawPicture;
    const UiFormData* sDrawForm = nullptr;
    u32 sDrawConstants = 0;
    u32 sDrawTable = 0;
    u32 sDrawState = 0;

    template < u32 N >
    void writeQuadOf(TypedDraw& rSite, const nw4r::math::VEC2& rBase, const nw4r::lyt::Size& rSize, const nw4r::math::VEC2 (*pCoords)[4],
                     const nw4r::ut::Color* pColors) {
        LayoutQuadVertex< N >* pOut = rSite.template vertices< LayoutQuadVertex< N > >(NEBULA_PRIMITIVE_QUADS, 4);
        // Corners in the quad's order; a pane's arrays hold them left to right, top to bottom.
        constexpr u8 cCorner[4] = {0, 1, 3, 2};
        for (u32 i = 0; i < 4; i++) {
            const u32 corner = cCorner[i];
            LayoutQuadVertex< N >& rOut = pOut[i];
            rOut.x = rBase.x + ((corner & 1) != 0 ? rSize.width : 0.0f);
            rOut.y = rBase.y + ((corner & 2) != 0 ? rSize.height : 0.0f);
            rOut.z = 0.0f;
            rOut.color = pColors != nullptr ? Rgba8(pColors[corner]) : Rgba8(0, 0, 0, 0);
            for (u32 c = 0; c < N; c++) {
                rOut.uv[c][0] = pCoords[c][corner].x;
                rOut.uv[c][1] = pCoords[c][corner].y;
            }
        }
    }

    void writeQuad(TypedDraw& rSite, const nw4r::math::VEC2& rBase, const nw4r::lyt::Size& rSize, u8 num, const nw4r::math::VEC2 (*pCoords)[4],
                   const nw4r::ut::Color* pColors) {
        switch (num) {
        case 0:
            writeQuadOf< 0 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        case 1:
            writeQuadOf< 1 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        case 2:
            writeQuadOf< 2 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        case 3:
            writeQuadOf< 3 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        case 4:
            writeQuadOf< 4 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        case 5:
            writeQuadOf< 5 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        case 6:
            writeQuadOf< 6 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        case 7:
            writeQuadOf< 7 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        default:
            writeQuadOf< 8 >(rSite, rBase, rSize, pCoords, pColors);
            break;
        }
    }
}  // namespace

namespace nw4r {
    namespace lyt {
        namespace detail {
            bool EqualsResName(const char* a1, const char* a2) {
                return strncmp(a1, a2, 0x10) == 0;
            }

            bool EqualsMaterialName(const char* a1, const char* a2) {
                return strncmp(a1, a2, 0x14) == 0;
            }

            TexCoordAry::TexCoordAry() {
                mCap = 0;
                mNum = 0;
                mData = nullptr;
            }

            void TexCoordAry::Free() {
                if (mData != nullptr) {
                    Layout::FreeMemory(mData);
                    mData = nullptr;
                    mCap = 0;
                    mNum = 0;
                }
            }

            void TexCoordAry::Reserve(u8 num) {
                if (mCap < num) {
                    Free();
                    const u32 coordNum = num;
                    math::VEC2* const ary = static_cast< math::VEC2* >(Layout::AllocMemory(coordNum * 4 * sizeof(math::VEC2)));
                    mData = reinterpret_cast< TexCoordQuad >(ary);
                    if (mData != nullptr) {
                        mCap = num;
                    }
                }
            }

            void TexCoordAry::SetSize(u8 num) {
                if (mData != nullptr && num <= mCap) {
                    const math::VEC2 texCoords[] = {math::VEC2(0.0f, 0.0f), math::VEC2(1.0f, 0.0f), math::VEC2(0.0f, 1.0f),
                                                           math::VEC2(1.0f, 1.0f)};

                    for (int j = mNum; j < num; j++) {
                        for (int i = 0; i < 4; i++) {
                            mData[j][i] = texCoords[i];
                        }
                    }
                    mNum = num;
                }
            }

            void TexCoordAry::Copy(const void* pResTex, u8 texCoordNum) {
                mNum = ut::Max(mNum, texCoordNum);
                // The resource's coordinates, big-endian.
                const math::VEC2(*src)[4] = reinterpret_cast< const math::VEC2(*)[4] >(pResTex);
                for (int j = 0; j < texCoordNum; j++) {
                    for (int i = 0; i < 4; i++) {
                        mData[j][i] = src[j][i];
                    }
                }
            }

            bool IsModulateVertexColor(ut::Color* color, u8 alpha) {
                if (alpha != 0xFF) {
                    return true;
                }

                if (color) {
                    if (color[0] != 0xFFFFFFFF || color[1] != 0xFFFFFFFF || color[2] != 0xFFFFFFFF || color[3] != 0xFFFFFFFF) {
                        return true;
                    }
                }

                return false;
            }

            const ut::Color MultipleAlpha(const ut::Color col, u8 alpha) {
                ut::Color ret = col;
                if (alpha != ut::Color::ALPHA_MAX) {
                    ret.a = u8(col.a * alpha / ut::Color::ALPHA_MAX);
                }

                return ret;
            }

            void MultipleAlpha(ut::Color* dst, const ut::Color* src, u8 a) {
                for (s32 i = 0; i < 4; i++) {
                    dst[i] = MultipleAlpha(src[i], a);
                }
            }

            void SetDrawMtx(const f32 (*pMtx)[4]) {
                memcpy(sDrawMtx, pMtx, sizeof(sDrawMtx));
            }

            const f32 (*GetDrawMtx())[4] {
                return sDrawMtx;
            }

            void SetDrawNothing() {
                sDrawKind = Draw_Nothing;
            }

            void SetDrawPicture(const SiteMaterial& rMaterial) {
                sDrawKind = Draw_Picture;
                sDrawPicture = rMaterial;
            }

            void SetDrawForm(const UiFormData* pForm, u32 constants, u32 table, u32 state) {
                sDrawKind = Draw_Form;
                sDrawForm = pForm;
                sDrawConstants = constants;
                sDrawTable = table;
                sDrawState = state;
            }

            void DrawQuad(const math::VEC2& basePt, const Size& size, u8 num, const math::VEC2 (*texCoords)[4], const ut::Color* vtxColors) {
                if (sDrawKind == Draw_Nothing) {
                    return;
                }
                if (sDrawKind == Draw_Picture) {
                    TypedDraw site = TypedDraw::eyeSpace(sDrawPicture, sDrawMtx);
                    writeQuad(site, basePt, size, num, texCoords, vtxColors);
                    return;
                }
                TypedDraw site = TypedDraw::eyeSpace(sDrawMtx);
                site.setForm(sDrawForm->mVertex, sDrawForm->mPixel, sDrawForm->mInputs, sDrawForm->mInputCount, sDrawConstants, sDrawTable,
                             sDrawState);
                writeQuad(site, basePt, size, num, texCoords, vtxColors);
            }

            void DrawQuad(const math::VEC2& basePt, const Size& size, u8 num, const math::VEC2 (*texCoords)[4], const ut::Color* vtxColors,
                          u8 alpha) {
                ut::Color colors[4];
                if (vtxColors) {
                    MultipleAlpha(colors, vtxColors, alpha);
                }
                DrawQuad(basePt, size, num, texCoords, vtxColors ? colors : 0);
            }
        };  // namespace detail
    };  // namespace lyt
};  // namespace nw4r
