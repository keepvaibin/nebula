#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JGeometry/TVec.hpp>

class FixedPosition;
class MiiFaceRecipe;
struct MiiCharModel;
struct MiiDrawCoreSetting;

class MiiFaceParts : public LiveActor {
public:
    MiiFaceParts(const char* pName, const MiiFaceRecipe& rRecipe);

    virtual ~MiiFaceParts();
    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAnim();
    virtual void calcViewAndEntry();

    void drawOpa(const MiiDrawCoreSetting* pSetting) const;
    void drawXlu(const MiiDrawCoreSetting* pSetting) const;
    void initFixedPosition(MtxPtr, const TVec3f&, const TVec3f&);
    void changeExpressionNormal();
    void changeExpressionBlink();
    void changeFaceModel(const MiiFaceRecipe& rRecipe);
    void initFaceModel();
    bool tryChangeExpression(u32 expression);

    MiiCharModel* mCharModel;
    void* _90;
    MiiFaceRecipe* mRecipe;
    TPos3f _98;
    MtxPtr _C8;
    FixedPosition* _CC;
    bool _D0;
    bool _D1;
};
