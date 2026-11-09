#include "Game/Ui/UiCommon.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Ui/Layout.hpp"
#include <cstdio>
#include <string.h>

namespace {
    // A pane's quad: position, colour, its matrix number (0: the quad's own; the renderer numbers merged neighbours'
    // from the first one's), then as many coordinates as its material reads, two floats each.
    struct LayoutQuadVertex {
        f32 x, y, z;
        Rgba8 color;
        u32 matrix;
    };
    constexpr u32 cQuadMatrixAt = 16;
    constexpr u32 cQuadCoordAt = 20;
    static_assert(sizeof(LayoutQuadVertex) == cQuadCoordAt, "the coordinates follow the matrix number");
    static_assert(offsetof(LayoutQuadVertex, matrix) == cQuadMatrixAt, "NEBULA_INPUT_MATRIX's place");

    enum DrawKind { Draw_Nothing, Draw_Picture, Draw_Form };
    DrawKind sDrawKind = Draw_Nothing;
    Mtx sDrawMtx;
    SiteMaterial sDrawPicture;
    const UiFormData* sDrawForm = nullptr;
    const u8* sDrawInputs = nullptr;
    u32 sDrawConstants = 0;
    u32 sDrawTable = 0;
    u32 sDrawState = 0;

    void setInput(NebulaLayoutInput& rInput, u32 semantic, u32 format, u32 offset, u32 stride) {
        rInput.semantic = static_cast< u16 >(semantic);
        rInput.stream = 0;
        rInput.format = static_cast< u8 >(format);
        rInput.offset = offset;
        rInput.stride = stride;
    }

    // Four vertices of a quad, `num` coordinates each: the stride and the inputs follow from the count. A quad with no
    // coordinate still has room for one, which nothing reads.
    void writeQuad(TypedDraw& rSite, const Vec2f& rBase, const Ui::Size& rSize, u8 num, const Vec2f (*pCoords)[4],
                   const Color8* pColors) {
        const u32 stride = cQuadCoordAt + 8 * (num > 0 ? num : 1);
        const u32 inputCount = 2 + num;
        NebulaLayoutInput* const pInputs = Frame::alloc< NebulaLayoutInput >(inputCount);
        setInput(pInputs[0], NEBULA_INPUT_MATRIX, 3, cQuadMatrixAt, stride);
        setInput(pInputs[1], NEBULA_INPUT_COLOR0, 2, 12, stride);
        for (u32 c = 0; c < num; c++) {
            setInput(pInputs[2 + c], NEBULA_INPUT_COORD0 + c, 1, cQuadCoordAt + 8 * c, stride);
        }
        u8* const pVertices = static_cast< u8* >(rSite.vertices(NEBULA_PRIMITIVE_QUADS, 4, stride, pInputs, inputCount));
        // Corners in the quad's order; a pane's arrays hold them left to right, top to bottom.
        constexpr u8 cCorner[4] = {0, 1, 3, 2};
        for (u32 i = 0; i < 4; i++) {
            const u32 corner = cCorner[i];
            LayoutQuadVertex& rOut = *reinterpret_cast< LayoutQuadVertex* >(pVertices + i * stride);
            f32 (*pUv)[2] = reinterpret_cast< f32 (*)[2] >(pVertices + i * stride + cQuadCoordAt);
            rOut.x = rBase.x + ((corner & 1) != 0 ? rSize.width : 0.0f);
            rOut.y = rBase.y + ((corner & 2) != 0 ? rSize.height : 0.0f);
            rOut.z = 0.0f;
            rOut.color = pColors != nullptr ? Rgba8(pColors[corner]) : Rgba8(0, 0, 0, 0);
            rOut.matrix = 0;
            for (u32 c = 0; c < num; c++) {
                pUv[c][0] = pCoords[c][corner].x;
                pUv[c][1] = pCoords[c][corner].y;
            }
        }
    }
}  // namespace

namespace Ui {
    namespace detail {
        bool EqualsResName(const char* a1, const char* a2) {
            return uiSameName(a1, a2);
        }

        bool EqualsMaterialName(const char* a1, const char* a2) {
            return uiSameName(a1, a2);
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
                Vec2f* const ary = static_cast< Vec2f* >(Layout::AllocMemory(coordNum * 4 * sizeof(Vec2f)));
                mData = reinterpret_cast< TexCoordQuad >(ary);
                if (mData != nullptr) {
                    mCap = num;
                }
            }
        }

        void TexCoordAry::SetSize(u8 num) {
            if (mData != nullptr && num <= mCap) {
                const Vec2f texCoords[] = {Vec2f(0.0f, 0.0f), Vec2f(1.0f, 0.0f), Vec2f(0.0f, 1.0f),
                                                       Vec2f(1.0f, 1.0f)};

                for (int j = mNum; j < num; j++) {
                    for (int i = 0; i < 4; i++) {
                        mData[j][i] = texCoords[i];
                    }
                }
                mNum = num;
            }
        }

        void TexCoordAry::Copy(const void* pResTex, u8 texCoordNum) {
            mNum = Max(mNum, texCoordNum);
            // The resource's coordinates, big-endian.
            const Vec2f(*src)[4] = reinterpret_cast< const Vec2f(*)[4] >(pResTex);
            for (int j = 0; j < texCoordNum; j++) {
                for (int i = 0; i < 4; i++) {
                    mData[j][i] = src[j][i];
                }
            }
        }

        bool IsModulateVertexColor(Color8* color, u8 alpha) {
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

        const Color8 MultipleAlpha(const Color8 col, u8 alpha) {
            Color8 ret = col;
            if (alpha != 255) {
                ret.a = u8(col.a * alpha / 255);
            }

            return ret;
        }

        void MultipleAlpha(Color8* dst, const Color8* src, u8 a) {
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

        void SetDrawForm(const UiFormData* pForm, const u8* pInputs, u32 constants, u32 table, u32 state) {
            sDrawKind = Draw_Form;
            sDrawForm = pForm;
            sDrawInputs = pInputs;
            sDrawConstants = constants;
            sDrawTable = table;
            sDrawState = state;
        }

        void DrawQuad(const Vec2f& basePt, const Size& size, u8 num, const Vec2f (*texCoords)[4], const Color8* vtxColors) {
            if (sDrawKind == Draw_Nothing) {
                return;
            }
            if (sDrawKind == Draw_Picture) {
                TypedDraw site = TypedDraw::eyeSpace(sDrawPicture, sDrawMtx);
                writeQuad(site, basePt, size, num, texCoords, vtxColors);
                return;
            }
            TypedDraw site = TypedDraw::eyeSpace(sDrawMtx);
            site.setForm(sDrawForm->mVertex, sDrawForm->mPixel, sDrawInputs, sDrawForm->mInputs.mCount, sDrawConstants, sDrawTable,
                         sDrawState);
            writeQuad(site, basePt, size, num, texCoords, vtxColors);
        }

        void DrawQuad(const Vec2f& basePt, const Size& size, u8 num, const Vec2f (*texCoords)[4], const Color8* vtxColors,
                      u8 alpha) {
            Color8 colors[4];
            if (vtxColors) {
                MultipleAlpha(colors, vtxColors, alpha);
            }
            DrawQuad(basePt, size, num, texCoords, vtxColors ? colors : 0);
        }
    };  // namespace detail
}  // namespace Ui
