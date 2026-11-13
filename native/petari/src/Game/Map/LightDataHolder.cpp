#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaHost.hpp"

namespace {
    static const char* sDefaultAreaLightName = "デフォルト";

    static LightInfoCoin sDefaultLightSetCoin = {{{0xFF, 0xFF, 0, 0}, {0.0f, 0.0f, 0.0f}, true}, {0xFF, 0xFF, 0xFF, 0}, 65.0f};

    // Shadow light sets whose lit partner their names do not give, paired by their colours (the shadow set's key is
    // the partner's dimmed: agents/rt/tools/stage_lights.py --unpaired).
    const char* const cHandPartners[][2] = {
        {"[共通]昼の影（新エデン下）", "[共通]昼（どら焼き）"},
        {"[共通]昼の影（明るい）", "[共通]昼（どら焼き）"},
        {"ダンジョンケイブ（影）", "[共通]昼（どら焼き）"},
        {"ピーチ城（影）", "[共通]夜（エデン）"},
    };
    // The names' shadow suffixes and the stage defaults' prefix.
    const char* const cShadowSuffixes[] = {"（影）", "（影2）", "（影２）"};
    const char cShadowInfix[] = "の影";
    const char cDefaultPrefix[] = "[デフォルト]";

    u32 textLength(const char* pText) {
        u32 n = 0;
        while (pText[n] != '\0') {
            n++;
        }
        return n;
    }

    bool sameText(const char* pA, const char* pB) {
        u32 i = 0;
        while (pA[i] != '\0' && pA[i] == pB[i]) {
            i++;
        }
        return pA[i] == pB[i];
    }

    // pOut = the parts joined; false when they do not fit.
    bool joinText(char* pOut, u32 room, const char* pA, u32 aLength, const char* pB, u32 bLength, const char* pC, u32 cLength) {
        if (aLength + bLength + cLength + 1 > room) {
            return false;
        }
        u32 at = 0;
        for (u32 i = 0; i < aLength; i++) {
            pOut[at++] = pA[i];
        }
        for (u32 i = 0; i < bLength; i++) {
            pOut[at++] = pB[i];
        }
        for (u32 i = 0; i < cLength; i++) {
            pOut[at++] = pC[i];
        }
        pOut[at] = '\0';
        return true;
    }

    f32 keyBrightness(const AreaLightInfo& rInfo) {
        const NebulaColor8& c = rInfo.mPlayerLight.mInfo0.mColor;
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    }

    // The lit set a shadow set darkens, by name: "X（影）" (also （影2）, （影２）) is X or [デフォルト]X; "Aの影B" is AB;
    // then the hand pairs. -1: not a shadow set, no partner held, or a partner no brighter for actors.
    s32 litPartner(const LightDataHolder& rHolder, s32 index) {
        const char* const name = rHolder.mLights[index].mAreaLightName;
        if (name == nullptr) {
            return -1;
        }
        const u32 length = textLength(name);
        const u32 prefix = textLength(cDefaultPrefix);
        char candidate[160];
        u32 found = MR::NameIndex::cNoValue;
        for (u32 s = 0; s < sizeof(cShadowSuffixes) / sizeof(cShadowSuffixes[0]) && found == MR::NameIndex::cNoValue; s++) {
            const u32 tail = textLength(cShadowSuffixes[s]);
            if (length <= tail || !sameText(name + length - tail, cShadowSuffixes[s])) {
                continue;
            }
            if (joinText(candidate, sizeof(candidate), name, length - tail, "", 0, "", 0)) {
                found = rHolder.mIndex.find(candidate);
            }
            if (found == MR::NameIndex::cNoValue && joinText(candidate, sizeof(candidate), cDefaultPrefix, prefix, name, length - tail, "", 0)) {
                found = rHolder.mIndex.find(candidate);
            }
        }
        const u32 infix = textLength(cShadowInfix);
        for (u32 at = 0; at + infix <= length && found == MR::NameIndex::cNoValue; at++) {
            bool here = true;
            for (u32 i = 0; i < infix && here; i++) {
                here = name[at + i] == cShadowInfix[i];
            }
            if (here && joinText(candidate, sizeof(candidate), name, at, name + at + infix, length - at - infix, "", 0)) {
                found = rHolder.mIndex.find(candidate);
            }
        }
        for (u32 p = 0; p < sizeof(cHandPartners) / sizeof(cHandPartners[0]) && found == MR::NameIndex::cNoValue; p++) {
            if (sameText(name, cHandPartners[p][0])) {
                found = rHolder.mIndex.find(cHandPartners[p][1]);
            }
        }
        if (found == MR::NameIndex::cNoValue || static_cast< s32 >(found) == index) {
            return -1;
        }
        return keyBrightness(rHolder.mLights[found]) > keyBrightness(rHolder.mLights[index]) ? static_cast< s32 >(found) : -1;
    }
};  // namespace

LightDataHolder::LightDataHolder() {
    mLightCount = 0;
    mLights = 0;
    mPartner = nullptr;
    mLit = nullptr;
}

void LightDataHolder::initLightData() {
    Table* data = NULL;
    mLightCount = LightFunction::createLightDataParser(&data);
    // At least one light is held (zeroed), so a name that is not found has a first light to take.
    mLights = new AreaLightInfo[mLightCount > 0 ? mLightCount : 1]();

    mIndex.reset(static_cast< u32 >(mLightCount));
    for (s32 i = 0; i < mLightCount; i++) {
        LightFunction::getAreaLightLightData(data, i, &mLights[i]);
        if (mLights[i].mAreaLightName != nullptr) {
            mIndex.add(mLights[i].mAreaLightName, static_cast< u32 >(i));
        }
    }

    mCoinLight = ::sDefaultLightSetCoin;

    // Each shadow set's lit partner, and the record an actor in its zone takes while the tracer draws the shade.
    mPartner = new s32[mLightCount > 0 ? mLightCount : 1];
    mLit = new AreaLightInfo[mLightCount > 0 ? mLightCount : 1]();
    mPartner[0] = -1;
    for (s32 i = 0; i < mLightCount; i++) {
        mPartner[i] = ::litPartner(*this, i);
        if (mPartner[i] >= 0) {
            const AreaLightInfo& rLit = mLights[mPartner[i]];
            mLit[i] = mLights[i];
            mLit[i].mPlayerLight = rLit.mPlayerLight;
            mLit[i].mStrongLight = rLit.mStrongLight;
            mLit[i].mWeakLight = rLit.mWeakLight;
        }
    }
}

// A name not held gets the first light. A shadow set while the tracer draws the shade: its lit partner's actor lights.
AreaLightInfo* LightDataHolder::findAreaLight(const char* pName) const {
    const u32 index = mIndex.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return mLights;
    }
    if (mPartner != nullptr && mPartner[index] >= 0 &&
        (NebulaHost::option(NEBULA_OPTION_RAY_TRACING) & static_cast< u32 >(NEBULA_RAY_TRACING_SHADOW_SETS)) != 0) {
        return &mLit[index];
    }
    return getLightInfo(static_cast< s32 >(index));
}

const char* LightDataHolder::getDefaultAreaLightName() const {
    return ::sDefaultAreaLightName;
}

s32 LightDataHolder::getDefaultStepInterpolate() const {
    return 0x1E;
}
