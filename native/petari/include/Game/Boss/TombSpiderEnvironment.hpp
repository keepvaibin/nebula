#pragma once

class LiveActor;
class PlanetMap;
class ModelObj;

class TombSpiderEnvironment {
public:
    TombSpiderEnvironment(LiveActor*);

    LiveActor* mActor;
    PlanetMap* mPlanet;
    ModelObj* mCocoon;
};
