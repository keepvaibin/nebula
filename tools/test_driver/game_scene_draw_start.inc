// SPDX-License-Identifier: GPL-3.0-only
// The start of GameScene::draw in a test build.
    if (sTestWalkActive && MR::isPlayerInWaterMode()) {
        ++sTestSwimFrames;
    }
    // Hidden test runs (the host's GALAXY_TEST_GALAXY_TOUR and GALAXY_TEST_TELEPORT): through the game's own calls.
    // A star taken in a test waits for play (the galaxy's opening camera and starter end in SceneAction, and would
    // replace the get demo); then what PowerStar::exeStageClearDemo does when the player takes it.
    static u32 sPendingStar;
    static s32 sPendingStarId;
    if (sPendingStar != 0 && isNerve(GET_NERVE(GameScene, GameSceneAction))) {
        MR::sendStageResultSequenceParam(sPendingStarId);
        sPendingStar == NEBULA_TEST_GET_GRAND_STAR ? MR::requestGrandStarGetDemo() : MR::requestPowerStarGetDemo();
        sPendingStar = 0;
    }
