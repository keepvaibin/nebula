#pragma once

class EffectSound;

namespace AudSeStrategy {
    // A playing effect's step each frame: the effector's send and low-pass for the effects that take them.
    void calc(EffectSound* pSe);
};  // namespace AudSeStrategy
