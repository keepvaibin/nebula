#pragma once

#include <revolution/types.h>

class SysInfoWindow;

// Nebula's cutscene skip. + during a skippable cutscene holds the game still (as the HOME Menu does) and asks "Are you
// sure you would like to skip this cutscene?" in the game's own Yes/No window, its sound faded out. No, or + again,
// resumes it where it stopped. Yes fades the held picture to black as the game's own fades do; a prerendered movie then ends as the
// game's own skip ends it (MoviePlayingSequence::trySkip), anything else runs to its end unseen, the game's own steps
// several per frame, every page of its talks shown whole and turned at once (TalkStateEvent::talk, the storybook), so
// everything it does still happens; a question it asks, a save prompt, or a wait on the player (its clock held)
// ends the skip there. The picture and sound then fade back in.
// Skippable: the prerendered movies, a galaxy's opening camera tour, the Comet Observatory's story scenes and the
// lessons the game starts itself, stars returning to the Observatory, and the bosses' scenes. Talks Mario starts, a
// star's get, Mario flying into a galaxy, questions, saves and the levels' own short scenes are not.
namespace NebulaCutsceneSkip {
    void init();                // GameSystem::initAfterStationedResourceLoaded
    void update();              // GameSystem::update, once per step
    void draw();                // GameSystem::draw, over the scene
    bool isFreezingScene();     // the prompt and the fade out hold the scene still (GameSystem::updateSceneController)
    bool tryExtraStep();        // during a skip: true while another step fits this frame (GameSystem::frameLoop)
    bool isSkipping();          // a cutscene runs unseen: its pages turn at once, its rumble stays off
    bool takeMovieSkip();       // a movie to stop now (MoviePlayingSequence::trySkip)
    void endForQuestion();      // the cutscene asks the player something: the skip ends there (TalkStateEvent::talk)
};  // namespace NebulaCutsceneSkip
