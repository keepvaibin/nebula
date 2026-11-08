#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class NPCActorItem {
public:
    inline NPCActorItem() {
    }

    NPCActorItem(const char* pName) {
        mActor = pName;
        mGoods0 = "";
        mGoodsJoint0 = "";
        mGoods1 = "";
        mGoodsJoint1 = "";
    }

    const char* mActor;
    const char* mGoods0;
    const char* mGoodsJoint0;
    const char* mGoods1;
    const char* mGoodsJoint1;
};
