#include "Game/Screen/IconComet.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"

namespace NrvIconComet {
    NEW_NERVE(IconCometNrvWait, IconComet, Wait);
};  // namespace NrvIconComet

IconComet::IconComet() : LayoutActor("\203\122\203\201\203\142\203\147\203\101\203\103\203\122\203\223" /* コメットアイコン */, true) {
}

void IconComet::init(const JMapInfoIter& rIter) {
    initLayoutManager("IconComet", 1);
    initNerve(GET_NERVE(IconComet, IconCometNrvWait));
}

bool IconComet::appearIfLanding(const char* pStageName) {
    if (!MR::isGalaxyCometLandInStage(pStageName)) {
        return false;
    }

    MR::setCometAnimFromId(this, MR::getEncounterGalaxyCometNameId(pStageName), 0);
    LayoutActor::appear();

    return true;
}

void IconComet::appearByCometNameId(int id) {
    MR::setCometAnimFromId(this, id, 0);
    LayoutActor::appear();
}

void IconComet::exeWait() {
}
