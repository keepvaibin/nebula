#include "Game/Util/RumbleCalculator.hpp"
#include <nebula_geometry.hpp>

RumbleCalculator::RumbleCalculator(f32 frequency, f32 phaseOffset, f32 amplitude, u32 duration) {
    mFrame = duration;
    mDuration = duration;
    mValue.x = 0.0f;
    mValue.y = 0.0f;
    mValue.z = 0.0f;
    mFrequency = frequency;
    mPhaseOffset = phaseOffset;
    mAmplitude = amplitude;
}

void RumbleCalculator::start(u32 duration) {
    if (duration) {
        mDuration = duration;
    }

    mFrame = 0;
    mValue.x = 0.0f;
    mValue.y = 0.0f;
    mValue.z = 0.0f;
}

void RumbleCalculator::calc() {
    if (!isRumbling()) {
        mValue.zero();
    } else {
        f32 progress = static_cast< f32 >(mFrame) / static_cast< f32 >(mDuration);
        f32 attenuation = 1.0f + -progress;

        Vec3f phases;
        f32 phase = progress;
        phase *= TWO_PI;
        phases.x = mFrequency * phase;
        phases.y = phases.x + mPhaseOffset;
        phases.z = phases.y + mPhaseOffset;

        calcValues(&mValue, phases);
        mValue.scale(attenuation * mAmplitude);
        mFrame++;
    }
}

void RumbleCalculator::reset() {
    mFrame = mDuration;
    mValue.zero();
}

RumbleCalculatorCosMultLinear::RumbleCalculatorCosMultLinear(f32 frequency, f32 phaseOffset, f32 amplitude, u32 duration)
    : RumbleCalculator(frequency, phaseOffset, amplitude, duration) {
}

void RumbleCalculatorCosMultLinear::calcValues(Vec3f* pValues, const Vec3f& rPhases) {
    pValues->set< f32 >(Angle::cosRadian(rPhases.x), Angle::cosRadian(rPhases.y), Angle::cosRadian(rPhases.z));
}
