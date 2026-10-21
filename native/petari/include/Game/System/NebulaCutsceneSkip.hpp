#pragma once

#include <revolution/types.h>

class SysInfoWindow;

// Nebula's cutscene skip. + during a prerendered movie holds the game still (as the HOME Menu does) and asks "Are you
// sure you would like to skip this cutscene?" in the game's own Yes/No window, its sound faded out. Yes ends the movie
// as the game's own skip does (MoviePlayingSequence::trySkip); No resumes it where it stopped. Demos are next: Yes then
// runs one to its end behind a black screen, the game's own steps several per frame and A pressed for it where it waits
// for a page to turn, so everything it does still happens; a question it asks, or a save prompt, ends the skip there.
namespace NebulaCutsceneSkip {
    void init();                // GameSystem::initAfterStationedResourceLoaded
    void update();              // GameSystem::update, once per step
    void draw();                // GameSystem::draw, over the scene
    bool isFreezingScene();     // the prompt holds the scene still (GameSystem::updateSceneController)
    bool tryExtraStep();        // during a skip: true while another step fits this frame (GameSystem::frameLoop)
    bool isPressingA(s32 step); // during a skip: A held on this pad step (WPadButton::update)
    bool takeMovieSkip();       // a movie to stop now (MoviePlayingSequence::trySkip)
    void endForQuestion();      // the cutscene asks the player something: the skip ends there (TalkStateEvent::talk)
};  // namespace NebulaCutsceneSkip
