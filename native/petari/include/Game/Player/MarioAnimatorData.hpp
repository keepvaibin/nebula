#pragma once
// Mario's and Luigi's animation tables, defined in src/Game/Player/MarioAnimatorData.cpp. The counts are the
// definitions' (the compiler checks they agree).

#include "Game/Animation/XanimeResource.hpp"

extern XanimeBckTable4 quadAnimeTable[7];
extern XanimeBckTable3 tripleAnimeTable[8];
extern XanimeBckTable2 doubleAnimeTable[4];
extern XanimeBckTable1 singleAnimeTable[292];
extern XanimeGroupInfo marioAnimeTable[308];
extern XanimeAuxInfo marioAnimeAuxTable[1];
extern XanimeOfsInfo marioAnimeOfsTable[35];
extern XanimeSwapTable luigiAnimeSwapTable[14];
