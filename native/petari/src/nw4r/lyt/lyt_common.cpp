#include "nw4r/lyt/common.h"
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "nw4r/lyt/layout.h"
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXGeometry.h>
#include <revolution/gx/GXVert.h>
#include <cstdio>

namespace nw4r {
    namespace lyt {
        namespace detail {
            bool EqualsResName(const char* a1, const char* a2) {
                return strncmp(a1, a2, 0x10) == 0;
            }

            bool EqualsMaterialName(const char* a1, const char* a2) {
                return strncmp(a1, a2, 0x14) == 0;
            }

            bool TestFileHeader(const res::BinaryFileHeader& fileHeader, u32 sig) {
                return GetSignatureInt(fileHeader.signature) == sig && fileHeader.byteOrder == 0xFEFF;
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
                    static const math::VEC2 texCoords[] = {math::VEC2(0.0f, 0.0f), math::VEC2(1.0f, 0.0f), math::VEC2(0.0f, 1.0f),
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
                const NEBULA_DISC(math::VEC2)(*src)[4] = reinterpret_cast< const NEBULA_DISC(math::VEC2)(*)[4] >(pResTex);
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

            void DrawQuad(const math::VEC2& basePt, const Size& size, u8 num, const math::VEC2 (*texCoords)[4], const ut::Color* vtxColors) {
                J3DNativeDraw::begin(GX_QUADS, 4,
                                     (vtxColors ? NEBULA_SHAPE_COLOR0 : 0) | ((1u << num) - 1u) * NEBULA_SHAPE_TEXCOORD0);
                J3DNativeDraw::position(basePt.x, basePt.y, 0.0f);

                if (vtxColors) {
                    J3DNativeDraw::color(vtxColors[0]);
                }

                for (int i = 0; i < num; i++) {
                    J3DNativeDraw::texCoord(texCoords[i][0].x, texCoords[i][0].y);
                }

                J3DNativeDraw::position(basePt.x + size.width, basePt.y, 0.0f);

                if (vtxColors) {
                    J3DNativeDraw::color(vtxColors[1]);
                }

                for (int i = 0; i < num; i++) {
                    J3DNativeDraw::texCoord(texCoords[i][1].x, texCoords[i][1].y);
                }

                J3DNativeDraw::position(basePt.x + size.width, basePt.y + size.height, 0.0f);

                if (vtxColors) {
                    J3DNativeDraw::color(vtxColors[3]);
                }

                for (int i = 0; i < num; i++) {
                    J3DNativeDraw::texCoord(texCoords[i][3].x, texCoords[i][3].y);
                }

                J3DNativeDraw::position(basePt.x, basePt.y + size.height, 0.0f);

                if (vtxColors) {
                    J3DNativeDraw::color(vtxColors[2]);
                }

                for (int i = 0; i < num; i++) {
                    J3DNativeDraw::texCoord(texCoords[i][2].x, texCoords[i][2].y);
                }
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
