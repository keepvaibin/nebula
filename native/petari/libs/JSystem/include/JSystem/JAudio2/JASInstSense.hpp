#pragma once

#include "JSystem/JAudio2/JASInstEffect.hpp"

struct JASInstParam;

class JASInstSense : public JASInstEffect {
public:
    enum SenseType {
        SENSE_ZERO = 0,
        SENSE_VELOCITY = 1,
        SENSE_PITCH = 2,
    };

    JASInstSense() : JASInstEffect(), mType(VOLUME), mSenseType(0), mMaxPoint(60), mStartLvl(1.0f), mEndLvl(1.0f){};

    virtual void effect(int pitch, int velocity, JASInstParam* pParams) const;
    void setParams(int senseType, int maxPoint, f32 startLvl, f32 endLvl);

    u32 mType;
    u8 mSenseType;
    u8 mMaxPoint;
    f32 mStartLvl;
    f32 mEndLvl;
};
