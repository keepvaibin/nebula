#pragma once

#include "JSystem/JParticle/JPATexture.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class NebulaArena;
class JPAResource;
struct ResTIMG;

class JPAResourceManager {
public:
    JPAResourceManager(void const*, NebulaArena*);
    JPAResource* getResource(u16) const;
    bool checkUserIndexDuplication(u16) const;
    const ResTIMG* swapTexture(ResTIMG const*, char const*);
    void registRes(JPAResource*);
    void registTex(JPATexture*);
    u32 getResUserWork(u16) const;

    void use(u16 idx, u32 slot) {
        mpTexArr[idx]->use(slot);
    }

public:
    NebulaArena* mpHeap;
    JPAResource** mpResArr;
    JPATexture** mpTexArr;
    u16 mResMax;
    u16 mResNum;
    u16 mTexMax;
    u16 mTexNum;
};
