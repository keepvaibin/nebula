#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class Clip;
class Model;
class ResTable;

class BvaPlayer : public AnmPlayerBase {
public:
    BvaPlayer(const ResTable*, Model*);

    void calc();
    Clip* getAnmVisibility();

private:
    Model* mModel;
};
