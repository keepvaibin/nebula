#pragma once

#include "Game/Particle/ParticleCore.hpp"

// A particle in its emitter's chunk (ParticleChunk), linked into one of the emitter's lists: alive, children or free.
struct ParticleSlot {
    ParticleSlot* mpPrev;
    ParticleSlot* mpNext;
    ParticleCore mParticle;

    ParticleSlot* getPrev() const {
        return mpPrev;
    }
    ParticleSlot* getNext() const {
        return mpNext;
    }
    ParticleCore* getObject() {
        return &mParticle;
    }
};

// An emitter's particles in order. Creation pushes to the front; calc walks first to last, draw either way.
struct ParticleList {
    ParticleSlot* mpFirst = nullptr;
    ParticleSlot* mpLast = nullptr;
    u32 mNum = 0;

    ParticleSlot* getEnd() const {
        return nullptr;
    }
    ParticleSlot* getFirst() const {
        return mpFirst;
    }
    ParticleSlot* getLast() const {
        return mpLast;
    }
    u32 getNum() const {
        return mNum;
    }

    void push_front(ParticleSlot* pSlot) {
        pSlot->mpPrev = nullptr;
        pSlot->mpNext = mpFirst;
        if (mpFirst != nullptr) {
            mpFirst->mpPrev = pSlot;
        } else {
            mpLast = pSlot;
        }
        mpFirst = pSlot;
        mNum++;
    }

    void push_back(ParticleSlot* pSlot) {
        pSlot->mpNext = nullptr;
        pSlot->mpPrev = mpLast;
        if (mpLast != nullptr) {
            mpLast->mpNext = pSlot;
        } else {
            mpFirst = pSlot;
        }
        mpLast = pSlot;
        mNum++;
    }

    ParticleSlot* pop_front() {
        ParticleSlot* pSlot = mpFirst;
        if (pSlot != nullptr) {
            erase(pSlot);
        }
        return pSlot;
    }

    ParticleSlot* pop_back() {
        ParticleSlot* pSlot = mpLast;
        if (pSlot != nullptr) {
            erase(pSlot);
        }
        return pSlot;
    }

    // The slot keeps its links; the caller pushes it onto another list next.
    ParticleSlot* erase(ParticleSlot* pSlot) {
        if (pSlot->mpPrev != nullptr) {
            pSlot->mpPrev->mpNext = pSlot->mpNext;
        } else {
            mpFirst = pSlot->mpNext;
        }
        if (pSlot->mpNext != nullptr) {
            pSlot->mpNext->mpPrev = pSlot->mpPrev;
        } else {
            mpLast = pSlot->mpPrev;
        }
        mNum--;
        return pSlot;
    }
};
