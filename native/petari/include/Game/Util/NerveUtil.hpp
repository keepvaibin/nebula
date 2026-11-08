#pragma once

class Nerve;
class NerveExecutor;

// Tests and rates on the executor's nerve step (frames since the nerve was set; negative before its first run).
namespace MR {
    bool isStep(const NerveExecutor* pExecutor, s32 step);
    bool isFirstStep(const NerveExecutor* pExecutor);
    bool isLessStep(const NerveExecutor* pExecutor, s32 step);
    bool isLessEqualStep(const NerveExecutor* pExecutor, s32 step);
    bool isGreaterStep(const NerveExecutor* pExecutor, s32 step);
    bool isGreaterEqualStep(const NerveExecutor* pExecutor, s32 step);
    // Every step-th step; with step 0, only step 0.
    bool isIntervalStep(const NerveExecutor* pExecutor, s32 step);
    bool isNewNerve(const NerveExecutor* pExecutor);

    // step / stepMax clamped to [0, 1]; 1 when stepMax <= 0.
    f32 calcNerveRate(const NerveExecutor* pExecutor, s32 stepMax);
    f32 calcNerveEaseInRate(const NerveExecutor* pExecutor, s32 stepMax);
    f32 calcNerveEaseOutRate(const NerveExecutor* pExecutor, s32 stepMax);
    f32 calcNerveValue(const NerveExecutor* pExecutor, s32 stepMax, f32 valueStart, f32 valueEnd);
    f32 calcNerveEaseInOutValue(const NerveExecutor* pExecutor, s32 stepMax, f32 valueStart, f32 valueEnd);
    // The rate runs from stepMin to stepMax.
    f32 calcNerveEaseInOutValue(const NerveExecutor* pExecutor, s32 stepMin, s32 stepMax, f32 valueStart, f32 valueEnd);

    void setNerveAtStep(NerveExecutor* pExecutor, const Nerve* pNerve, s32 step);
};  // namespace MR
