/* SPDX-License-Identifier: GPL-3.0-only */
/* A test request (test_scene_frame). */
typedef struct {
    uint32_t kind;     /* NEBULA_TEST_* */
    int32_t scenario;  /* NEBULA_TEST_CHANGE_STAGE */
    float position[3]; /* NEBULA_TEST_PLACE_PLAYER */
    char stage[64];    /* NEBULA_TEST_CHANGE_STAGE */
} NebulaTestAction;

enum {
    NEBULA_TEST_CHANGE_STAGE = 1, /* MR::requestChangeStageInGameMoving(stage, scenario) */
    NEBULA_TEST_PLACE_PLAYER,     /* MR::setPlayerPos(position) */
    NEBULA_TEST_KILL_PLAYER,      /* MR::forceKillPlayerByAbyss: a miss, then retry or game over */
    NEBULA_TEST_START_MOVIE,      /* MR::startMovie(scenario): where the stage has a movie sequence */
    NEBULA_TEST_GET_STAR,         /* the star `scenario` is collected (its result sent, the get demo requested) */
    NEBULA_TEST_GET_GRAND_STAR,   /* the same with the Grand Star's demo */
    NEBULA_TEST_SET_PLAYER,       /* the file's player from the next scene (`scenario` 0 Mario, 1 Luigi) */
    NEBULA_TEST_SET_GRAND_STARS,  /* the save holds Grand Stars 1-`scenario` (their galaxies' first star) */
    NEBULA_TEST_LIST_ACTORS,      /* the scene's live actors with a model, their model name and position, logged */
    NEBULA_TEST_SET_EVENT_FLAG,   /* the save's event flag named `stage` on (GameDataFunction::onGameEventFlag) */
    NEBULA_TEST_SHAKE_CAMERA,     /* the screen shakes until the scene ends (MR::shakeCameraInfinity, Dodoryu's quake) */
    NEBULA_TEST_PLACE_PLAYER_WATER, /* the player placed in water body `scenario` of the scene's, in turn */
    NEBULA_TEST_PLACE_PLAYER_ACTOR, /* the player placed at live actor `scenario` of the scene's, in turn */
    NEBULA_TEST_APPEAR_ACTOR,     /* the dead actors whose model is `stage` appear (a boss a stage switch holds back) */
    NEBULA_TEST_KILL_ACTOR,       /* the live actors whose model is `stage` killed (their kill(): what they leave behind) */
};
