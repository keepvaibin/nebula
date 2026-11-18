#pragma once

#include "Game/NameObj/NameObjCategoryList.hpp"

#if defined(NEBULA_PLAIN_PATHS)
// The animation pass of the module with no hook points (the calcAnim lists, P-040). A run of actors whose calcAnim is
// LiveActor's is taken as one batch: their base matrices and animation frames on the game thread in list order, their
// models' joint walks on the step threads, then their draw lists and collision matrices on the game thread in list
// order. Any other object ends the batch first and runs its calcAnim as before. What each part reads and writes is the
// same as in the calls one after another (docs in ActorAnimPass.cpp).
namespace ActorAnimPass {
    void execute(NameObjCategoryList::CategoryInfo& rInfo);
}  // namespace ActorAnimPass
#endif
