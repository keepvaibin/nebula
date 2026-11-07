#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>


class FixedPosition;
class MiiFaceRecipe;
struct MiiCharModel;
struct MiiDrawCoreSetting;

class MiiFaceParts : public LiveActor {
public:
    MiiFaceParts(const char* pName, const MiiFaceRecipe& rRecipe);

    virtual ~MiiFaceParts();
    virtual void init(const TableIter& rIter);
    virtual void calcAnim();
    virtual void calcViewAndEntry();

    void drawOpa(const MiiDrawCoreSetting* pSetting) const;
    void drawXlu(const MiiDrawCoreSetting* pSetting) const;
    void initFixedPosition(MtxPtr, const Vec3f&, const Vec3f&);
    void changeExpressionNormal();
    void changeExpressionBlink();
    void changeFaceModel(const MiiFaceRecipe& rRecipe);
    void initFaceModel();
    bool tryChangeExpression(u32 expression);

    MiiCharModel* mCharModel;
    void* _90;
    MiiFaceRecipe* mRecipe;
    Pos3f _98;
    MtxPtr _C8;
    FixedPosition* _CC;
    bool _D0;
    bool _D1;
};
