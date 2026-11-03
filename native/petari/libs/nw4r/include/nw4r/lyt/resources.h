#pragma once

#include "nw4r/lyt/types.h"
#include "nw4r/math/types.h"
#include <NebulaGpu.h>
#include <nebula_math.h>

// BRLYT/BRLAN, read in place in host order as the importer cooks them (crates/nebula-import
// cook/native_data.rs layout).

namespace nw4r {
    namespace lyt {
        namespace res {
            const u32 FILESIGNATURE_RLYT = 'RLYT';

            const u32 DATABLOCKKIND_LAYOUT = 'lyt1';
            const u32 DATABLOCKKIND_PANE = 'pan1';
            const u32 DATABLOCKKIND_PANEBEGIN = 'pas1';
            const u32 DATABLOCKKIND_PANEEND = 'pae1';
            const u32 DATABLOCKKIND_PICTURE = 'pic1';
            const u32 DATABLOCKKIND_TEXTBOX = 'txt1';
            const u32 DATABLOCKKIND_WINDOW = 'wnd1';
            const u32 DATABLOCKKIND_BOUNDING = 'bnd1';
            const u32 DATABLOCKKIND_GROUP = 'grp1';
            const u32 DATABLOCKKIND_GROUPBEGIN = 'grs1';
            const u32 DATABLOCKKIND_GROUPEND = 'gre1';
            const u32 DATABLOCKKIND_FONTLIST = 'fnl1';
            const u32 DATABLOCKKIND_TEXTURELIST = 'txl1';
            const u32 DATABLOCKKIND_MATERIALLIST = 'mat1';
            const u32 DATABLOCKKIND_USERDATALIST = 'usd1';

            const u32 FILESIGNATURE_RLAN = 'RLAN';

            const u32 ANIMATIONTYPE_RLPA = 'RLPA';
            const u32 ANIMATIONTYPE_RLVI = 'RLVI';
            const u32 ANIMATIONTYPE_RLVC = 'RLVC';
            const u32 ANIMATIONTYPE_RLMC = 'RLMC';
            const u32 ANIMATIONTYPE_RLTS = 'RLTS';
            const u32 ANIMATIONTYPE_RLTP = 'RLTP';
            const u32 ANIMATIONTYPE_RLIM = 'RLIM';

            const u32 DATABLOCKKIND_PANEANIMTAG = 'pat1';
            const u32 DATABLOCKKIND_PANEANIMSHARE = 'pah1';
            const u32 DATABLOCKKIND_PANEANIMINFO = 'pai1';

            const u32 RESOURCETYPE_LAYOUT = 'blyt';
            const u32 RESOURCETYPE_ANIMATION = 'anim';
            const u32 RESOURCETYPE_TEXTURE = 'timg';
            const u32 RESOURCETYPE_FONT = 'font';
            const u32 RESOURCETYPE_ARCHIVEFONT = 'fnta';

            struct BinaryFileHeader {
                char signature[4];
                u16 byteOrder;
                u16 version;
                u32 fileSize;
                u16 headerSize;
                u16 dataBlocks;
            };

            struct DataBlockHeader {
                char kind[4];
                u32 size;
            };

            struct Layout {
                DataBlockHeader blockHeader;
                u8 originType;
                u8 padding[3];
                Size layoutSize;
            };

            struct Font {
                u32 nameStrOffset;
                u8 type;
                u8 padding[3];
            };

            struct FontList {
                DataBlockHeader blockHeader;
                u16 fontNum;
                u8 padding[2];
            };

            struct Texture {
                u32 nameStrOffset;
                u8 type;
                u8 padding[3];
            };

            struct TextureList {
                DataBlockHeader blockHeader;
                u16 texNum;
                u8 padding[2];
            };

            struct TexMap {
                TexMap() : texIdx(0), wrapSflt(0), wrapTflt(0) {
                }

                u8 GetWarpModeS() const {
                    return u8(detail::GetBits(wrapSflt, 0, 2));
                }

                u8 GetWarpModeT() const {
                    return u8(detail::GetBits(wrapTflt, 0, 2));
                }

                u8 GetMinFilter() const {
                    const int bitLen = 3;
                    const u8 bitData = detail::GetBits(wrapSflt, 2, bitLen);
                    return u8(detail::GetBits(bitData + 1, 0, bitLen));
                }

                u8 GetMagFilter() const {
                    const int bitLen = 1;
                    const u8 bitData = detail::GetBits(wrapTflt, 2, bitLen);
                    return u8(detail::GetBits(bitData + 1, 0, bitLen));
                }

                void SetWarpModeS(u8 value) {
                    detail::SetBits(&wrapSflt, 0, 2, u8(value));
                }

                void SetWarpModeT(u8 value) {
                    detail::SetBits(&wrapTflt, 0, 2, u8(value));
                }

                void SetMinFilter(u8 value) {
                    const int bitLen = 3;
                    const u8 bitData = u8(detail::GetBits(value - 1, 0, bitLen));  // Set to zero when (value = NEBULA_FILTER_LINEAR)
                    detail::SetBits(&wrapSflt, 2, bitLen, bitData);
                }

                void SetMagFilter(u8 value) {
                    const int bitLen = 1;
                    const u8 bitData = u8(detail::GetBits(value - 1, 0, bitLen));
                    detail::SetBits(&wrapTflt, 2, bitLen, bitData);
                }

                u16 texIdx;
                u8 wrapSflt;
                u8 wrapTflt;
            };

            struct MaterialList {
                DataBlockHeader blockHeader;
                u16 materialNum;
                u8 padding[2];
            };

            struct Pane {
                DataBlockHeader blockHeader;
                u8 flag;
                u8 basePosition;
                u8 alpha;
                u8 padding;
                char name[16];
                char userData[8];
                math::VEC3 translate;
                math::VEC3 rotate;
                math::VEC2 scale;
                Size size;
            };

            struct Picture : Pane {
                u32 vtxCols[4];
                u16 materialIdx;
                u8 texCoordNum;
                u8 padding[1];
            };

            struct TextBox : Pane {
                u16 textBufBytes;
                u16 textStrBytes;
                u16 materialIdx;
                u16 fontIdx;
                u8 textPosition;
                u8 textAlignment;
                u8 padding[2];
                u32 textStrOffset;
                u32 textCols[2];
                Size fontSize;
                f32 charSpace;
                f32 lineSpace;
            };

            struct WindowFrame {
                u16 materialIdx;
                u8 textureFlip;
                u8 padding1;
            };

            struct WindowContent {
                u32 vtxCols[4];
                u16 materialIdx;
                u8 texCoordNum;
                u8 padding[1];
            };

            struct Window : Pane {
                InflationLRTB inflation;
                u8 frameNum;
                u8 padding1;
                u8 padding2;
                u8 padding3;
                u32 contentOffset;
                u32 frameOffsetTableOffset;
            };

            struct Bounding : Pane {};

            struct ExtUserDataList {
                DataBlockHeader blockHeader;
                detail::ResU16 num;
                u8 padding[2];
            };

            struct Group {
                DataBlockHeader blockHeader;
                char name[16];
                u16 paneNum;
                u8 padding[2];
            };

            struct MaterialResourceNum {
                u8 GetTexMapNum() const {
                    return u8(detail::GetBits(u32(bits), 0, 4));
                }

                u8 GetTexSRTNum() const {
                    return u8(detail::GetBits(u32(bits), 4, 4));
                }

                u8 GetTexCoordGenNum() const {
                    return u8(detail::GetBits(u32(bits), 8, 4));
                }

                bool HasTevSwapTable() const {
                    return detail::TestBit(u32(bits), 12);
                }

                u8 GetIndTexSRTNum() const {
                    return u8(detail::GetBits(u32(bits), 13, 2));
                }

                u8 GetIndTexStageNum() const {
                    return u8(detail::GetBits(u32(bits), 15, 3));
                }

                u8 GetStageCount() const {
                    return u8(detail::GetBits(u32(bits), 18, 5));
                }

                bool HasAlphaCompare() const {
                    return detail::TestBit(u32(bits), 23);
                }

                bool HasBlendMode() const {
                    return detail::TestBit(u32(bits), 24);
                }

                u8 GetChanCtrlNum() const {
                    return u8(detail::GetBits(u32(bits), 25, 1));
                }

                u8 GetMatColNum() const {
                    return u8(detail::GetBits(u32(bits), 27, 1));
                }

                u32 bits;
            };

            struct Material {
                char name[20];
                NebulaColor16 tevCols[3];
                NebulaColor8 tevKCols[4];
                MaterialResourceNum resNum;
            };

            struct AnimationTagBlock {
                DataBlockHeader blockHeader;
                detail::ResU16 tagOrder;
                detail::ResU16 groupNum;
                detail::ResU32 nameOffset;
                detail::ResU32 groupsOffset;
                detail::ResS16 startFrame;
                detail::ResS16 endFrame;
                u8 flag;
                u8 padding[3];
            };

            struct AnimationShareBlock {
                DataBlockHeader blockHeader;
                detail::ResU32 animShareInfoOffset;
                detail::ResU16 shareNum;
                u8 padding[2];
            };

            struct AnimationBlock {
                DataBlockHeader blockHeader;
                u16 frameSize;
                u8 loop;
                u8 padding1;
                u16 fileNum;
                u16 animContNum;
                u32 animContOffsetsOffset;
            };

            struct AnimationContent {
                char name[MaterialNameStrMax];

                u8 num;
                u8 type;
                u8 padding[2];
            };

            struct AnimationInfo {
                u32 kind;

                u8 num;
                u8 padding[3];
            };

            struct AnimationTarget {
                u8 id;
                u8 target;
                u8 curveType;
                u8 padding1;

                u16 keyNum;
                u8 padding2[2];

                u32 keysOffset;
            };

            struct HermiteKey {
                f32 frame;
                f32 value;
                f32 slope;
            };

            struct StepKey {
                f32 frame;
                u16 value;
                u16 pad;
            };
        };  // namespace res
    };  // namespace lyt
};  // namespace nw4r
