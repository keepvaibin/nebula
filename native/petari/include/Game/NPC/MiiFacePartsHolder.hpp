#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Mii/MiiHead.hpp"

class MiiFaceParts;
class MiiFaceRecipe;

typedef void (MiiFaceParts::*DrawPartsFuncPtr)(const Mii::Style&) const;

// The scene's Mii heads: the Mii parts from MiiFaceDatabase.arc, each head built at the draw after it is asked for, and
// drawn with the game's Mii NPC stages.
class MiiFacePartsHolder : public LiveActorGroup {
public:
    MiiFacePartsHolder(int partsNumMax);

    virtual ~MiiFacePartsHolder() override;
    virtual void init(const TableIter& rIter) override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;

    void buildWaitingHeads();
    bool isInitEnd() const;
    // The parts could not be read: no heads.
    bool isError() const;
    static MiiFaceParts* createPartsFromReceipe(const char* pName, const MiiFaceRecipe& rRecipe);
    static MiiFaceParts* createPartsFromDefault(const char* pName, u32 index);
    void drawEachActor(DrawPartsFuncPtr pDrawFunc, const Mii::Style& rStyle) const;

private:
    bool mReady;
};
