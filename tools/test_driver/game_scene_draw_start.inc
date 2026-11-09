// SPDX-License-Identifier: GPL-3.0-only
// The start of GameScene::draw in a test build.
    if (sTestWalkActive && MR::isPlayerInWaterMode()) {
        ++sTestSwimFrames;
    }
    // The dome's launch demo (GALAXY_TEST_EVENTS domelaunch@N), each frame: its part and rate, Mario's base position,
    // the camera, and Mario's projected place (x right, y up, -1..1) on a 4:3 view and on this one.
    if (MR::isDemoActive("マリオ飛び出し")) {
        const char* const parts[] = {"スピンドライバ出現", "スピンドライバ起動", "飛び出す"};
        s32 part = -1;
        for (s32 p = 0; p < 3; ++p) {
            if (MR::isDemoPartActive(parts[p])) {
                part = p;
            }
        }
        MtxPtr base = MarioAccess::getPlayerActor()->getBaseMtx();
        const Vec3f mario(base[0][3], base[1][3], base[2][3]);
        Vec3f view;
        MR::getCameraViewMtx().mult(mario, view);
        const Vec3f eye = MR::getCamPos();
        Vec3f apart(mario);
        apart.sub(eye);
        const f32 slope = -view.z * tanf(MR::getRenderFovy() * (PI / 360.0f));
        nebula_log("dome: part %d rate %.4f mario %.1f %.1f %.1f cam %.1f %.1f %.1f fovy %.2f render %.2f aspect %.3f "
                   "dist %.0f ndc43 %.3f %.3f ndc %.3f %.3f\n",
                   part, part >= 0 ? MR::calcDemoPartStepRate(parts[part]) : -1.0f, mario.x, mario.y, mario.z, eye.x, eye.y,
                   eye.z, MR::getFovy(), MR::getRenderFovy(), MR::getAspect(), apart.length(),
                   view.x / (slope * (4.0f / 3.0f)), view.y / slope, view.x / (slope * MR::getAspect()), view.y / slope);
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
