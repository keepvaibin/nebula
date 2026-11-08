#pragma once

#include <nebula_geometry.hpp>

class BossStinkBug;
class HitSensor;

namespace BossStinkBugFunction {
    void addHeightOffset(Vec3f* pOut, const BossStinkBug*, f32 height);
    void addLocalHeightOffset(Vec3f* pOut, BossStinkBug*, f32 height);
    void calcDiffCurrentRailPosition(Vec3f* pOut, BossStinkBug*, f32 heightOffset, f32 localHeightOffset);
    void moveRail(BossStinkBug*, f32 speed, f32 heightOffset, f32 localHeightOffset);
    void turnRailGround(BossStinkBug*, const Vec3f& rTurnFront, f32 rate, bool isRailToEnd);
    void turnRail(BossStinkBug*, const Vec3f& rTurnFront, f32 baseRoll, f32 rate, f32 speed, f32 heightOffset, f32 localHeightOffset,
                  bool isRailToEnd);
    void regainToRail(BossStinkBug*, const Vec3f& rRailDiff, f32 rate, f32 speed, f32 heightOffset, f32 localHeightOffset);
    void setFallVelocity(BossStinkBug*, const Vec3f& rBase, f32 rate, f32 peakRate, f32 height);
    void throwBomb(BossStinkBug*);
    void invalidateAttack(BossStinkBug*);
    bool isHipDropableSensor(const BossStinkBug*, const HitSensor*);
    bool isExistPlayerBack(const BossStinkBug*, f32 distance);
    void validateAttack(BossStinkBug*);
};  // namespace BossStinkBugFunction
