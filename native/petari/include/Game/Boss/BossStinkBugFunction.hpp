#pragma once

#include <nebula_geometry.hpp>

class BossStinkBug;
class HitSensor;

namespace BossStinkBugFunction {
    void addHeightOffset(Vec3f*, const BossStinkBug*, f32);
    void addLocalHeightOffset(Vec3f*, BossStinkBug*, f32);
    void calcDiffCurrentRailPosition(Vec3f*, BossStinkBug*, f32, f32);
    void moveRail(BossStinkBug*, f32, f32, f32);
    void turnRailGround(BossStinkBug*, const Vec3f&, f32, bool);
    void turnRail(BossStinkBug*, const Vec3f&, f32, f32, f32, f32, f32, bool);
    void regainToRail(BossStinkBug*, const Vec3f&, f32, f32, f32, f32);
    void setFallVelocity(BossStinkBug*, const Vec3f&, f32, f32, f32);
    void throwBomb(BossStinkBug*);
    void invalidateAttack(BossStinkBug*);
    bool isHipDropableSensor(const BossStinkBug*, const HitSensor*);
    bool isExistPlayerBack(const BossStinkBug*, f32);
    void validateAttack(BossStinkBug*);
};  // namespace BossStinkBugFunction
