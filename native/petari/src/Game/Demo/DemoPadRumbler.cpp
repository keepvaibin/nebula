#include "Game/Demo/DemoPadRumbler.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"

DemoPadRumbler::DemoPadRumbler(const char* pName) : mNumPadRumbleEntries(), mPadRumbleEntries() {
    if (pName == nullptr)
        return;

    MR::FormattedName buf("MoviePadRumble%s", pName);
    Table* map = MR::createCsvParser(buf.c_str(), "PadRumbleData.bcsv");

    mNumPadRumbleEntries = MR::getCsvDataElementNum(map);
    PadRumbleInfo* infos = new PadRumbleInfo[mNumPadRumbleEntries];
    mPadRumbleEntries = infos;

    for (int i = 0; i < mNumPadRumbleEntries; i++) {
        PadRumbleInfo* rumble = &infos[i];
        MR::getCsvDataStrOrNULL(&rumble->mName, map, "PadRumbleName", i);
        MR::getCsvDataS32(&rumble->mStartFrame, map, "StartFrame", i);
    }
}

void DemoPadRumbler::update(s32 frame) {
    for (int i = 0; i < mNumPadRumbleEntries; i++) {
        PadRumbleInfo* rumble = &mPadRumbleEntries[i];
        if (rumble->mStartFrame == frame) {
            MR::tryRumblePad(this, rumble->mName, PLAYER_1);
        }
    }
}
