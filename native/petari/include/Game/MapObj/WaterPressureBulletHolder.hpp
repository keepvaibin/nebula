#pragma once

#include "Game/MapObj/PressureBulletHolder.hpp"
#include "Game/MapObj/WaterPressureBullet.hpp"

// Shared by WaterPressure and WaterBazooka.
class WaterPressureBulletHolder : public PressureBulletHolder< WaterPressureBullet, 16 > {
public:
    WaterPressureBulletHolder(const char* pName) : PressureBulletHolder(pName, "ウォータープレッシャーの弾") {}
};
