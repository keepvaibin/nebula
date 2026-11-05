#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <JSystem/JKernel/JKRDisposer.hpp>
#include <MiiLib.h>

class MiiDatabase;
class MiiFaceParts;
class MiiFaceRecipe;
struct MiiDrawCoreSetting;

typedef void (MiiFaceParts::*DrawPartsFuncPtr)(const MiiDrawCoreSetting*) const;

class MiiFacePartsHolder : public LiveActorGroup, JKRDisposer {
public:
    MiiFacePartsHolder(int);

    virtual ~MiiFacePartsHolder();
    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;
    virtual void calcAnim();
    virtual void calcViewAndEntry();

    void reinitCharModel();
    bool isInitEnd() const;
    bool isError() const;
    static MiiFaceParts* createPartsFromReceipe(const char*, const MiiFaceRecipe&);
    static MiiFaceParts* createPartsFromDefault(const char*, u16);
    void drawEachActor(DrawPartsFuncPtr, const MiiDrawCoreSetting*) const;
    void drawExtra() const;
    void setStageOpa() const;
    void setStageXlu() const;

    MiiDatabase* _34;
    MiiErrcode _38;
    s32 _3C;
};
