#include "JSystem/JUtility/JUTPalette.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <revolution/os.h>

void JUTPalette::storeTLUT(_GXTlut param_0, ResTLUT* tlut) {
    if (tlut == nullptr) {
        OSPanic(__FILE__, 35, "JUTTexture: TLUT is NULL\n");
    }
    mName = param_0;
    mFormat = tlut->mFormat;
    mTransparency = tlut->mTransparency;
    mLutNum = tlut->mLutNum;
    mColorTable = tlut + 8;
    J3DNativeTexture::initTlut(&mObj, mColorTable, static_cast< GXTlutFmt >(mFormat), mLutNum);
}

void JUTPalette::storeTLUT(_GXTlut param_0, _GXTlutFmt param_1, JUTTransparency param_2, u16 param_3, void* param_4) {
    mName = param_0;
    mFormat = param_1;
    mTransparency = param_2;
    mLutNum = param_3;
    mColorTable = static_cast< ResTLUT* >(param_4);
    J3DNativeTexture::initTlut(&mObj, mColorTable, static_cast< GXTlutFmt >(mFormat), mLutNum);
}

bool JUTPalette::load() {
    bool check = mLutNum != 0;
    if (check) {
        J3DNativeTexture::loadTlut(&mObj, mName);
    }

    return check;
}
