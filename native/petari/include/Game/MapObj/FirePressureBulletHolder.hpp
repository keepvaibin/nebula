#pragma once

#include "Game/MapObj/FirePressureBullet.hpp"
#include "Game/MapObj/PressureBulletHolder.hpp"

class FirePressureBulletHolder : public PressureBulletHolder< FirePressureBullet, 8 > {
public:
    FirePressureBulletHolder(const char* pName) : PressureBulletHolder(pName, "ファイアプレッシャーの弾") {}
};
