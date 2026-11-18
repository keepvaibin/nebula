// The plain module's collision change log (P-140): a shadow's line query is reused only while no change noted since
// reaches its line. Every change the game's float line tests could find touching the line must count as reaching it;
// changes far from it must not; a change that stands for everywhere, and more changes than the log keeps, always do.
#include "Game/Map/CollisionChanges.hpp"

#include <cmath>
#include <cstdio>

namespace {
    int sFailed = 0;

    void expect(bool value, const char* pText) {
        if (!value) {
            std::printf("FAILED: %s\n", pText);
            sFailed++;
        }
    }

    u64 sRandom = 0x2545F4914F6CDD1Dull;

    f32 random01() {
        sRandom ^= sRandom << 13;
        sRandom ^= sRandom >> 7;
        sRandom ^= sRandom << 17;
        return static_cast< f32 >((sRandom >> 40) & 0xFFFFFF) / static_cast< f32 >(0x1000000);
    }

    f32 randomIn(f32 low, f32 high) {
        return low + (high - low) * random01();
    }

    // The game's test of a sphere against a segment, in floats as it makes it: the nearest point of the segment to the
    // centre, then its distance against the radius.
    bool touchesInFloats(const Vec3f& rCenter, f32 radius, const Vec3f& rStart, const Vec3f& rLine) {
        const f32 lineSquared = rLine.x * rLine.x + rLine.y * rLine.y + rLine.z * rLine.z;
        const f32 toX = rCenter.x - rStart.x;
        const f32 toY = rCenter.y - rStart.y;
        const f32 toZ = rCenter.z - rStart.z;
        f32 along = lineSquared > 0.0f ? (toX * rLine.x + toY * rLine.y + toZ * rLine.z) / lineSquared : 0.0f;
        along = along < 0.0f ? 0.0f : (along > 1.0f ? 1.0f : along);
        const f32 offX = toX - along * rLine.x;
        const f32 offY = toY - along * rLine.y;
        const f32 offZ = toZ - along * rLine.z;
        return offX * offX + offY * offY + offZ * offZ <= radius * radius;
    }
}  // namespace

int main() {
    const Vec3f start(1000.0f, 2000.0f, -500.0f);
    const Vec3f line(0.0f, -1050.0f, 0.0f);

    u64 since = CollisionChanges::count();
    expect(CollisionChanges::isLineUnchanged(since, start, line), "no change since: unchanged");

    CollisionChanges::note(Vec3f(5000.0f, 2000.0f, -500.0f), 300.0f);
    expect(CollisionChanges::isLineUnchanged(since, start, line), "a change 4000 away with radius 300: unchanged");

    CollisionChanges::note(Vec3f(1200.0f, 1500.0f, -500.0f), 250.0f);
    expect(!CollisionChanges::isLineUnchanged(since, start, line), "a change whose sphere crosses the line: changed");
    since = CollisionChanges::count();
    expect(CollisionChanges::isLineUnchanged(since, start, line), "a query made after the change: unchanged");

    CollisionChanges::note(Vec3f(1000.0f, 900.0f, -500.0f), 100.0f);
    expect(!CollisionChanges::isLineUnchanged(since, start, line), "a sphere over the segment's end: changed");
    since = CollisionChanges::count();
    CollisionChanges::note(Vec3f(1000.0f, 500.0f, -500.0f), 100.0f);
    expect(CollisionChanges::isLineUnchanged(since, start, line), "a sphere 450 past the segment's end: unchanged");

    since = CollisionChanges::count();
    CollisionChanges::note(Vec3f(0.0f, 0.0f, 0.0f), -1.0f);
    expect(!CollisionChanges::isLineUnchanged(since, start, line), "a negative radius stands for everywhere");
    since = CollisionChanges::count();
    CollisionChanges::note(Vec3f(0.0f, 0.0f, 0.0f), INFINITY);
    expect(!CollisionChanges::isLineUnchanged(since, start, line), "an infinite radius stands for everywhere");
    since = CollisionChanges::count();
    CollisionChanges::note(Vec3f(NAN, 0.0f, 0.0f), 1.0f);
    expect(!CollisionChanges::isLineUnchanged(since, start, line), "a centre that is not a number stands for everywhere");
    since = CollisionChanges::count();
    CollisionChanges::note(Vec3f(0.0f, 0.0f, 0.0f), NAN);
    expect(!CollisionChanges::isLineUnchanged(since, start, line), "a radius that is not a number stands for everywhere");

    since = CollisionChanges::count();
    for (int i = 0; i < 64; i++) {
        CollisionChanges::note(Vec3f(90000.0f, 0.0f, 0.0f), 10.0f);
    }
    expect(CollisionChanges::isLineUnchanged(since, start, line), "64 far changes: unchanged");
    CollisionChanges::note(Vec3f(90000.0f, 0.0f, 0.0f), 10.0f);
    expect(!CollisionChanges::isLineUnchanged(since, start, line), "65 changes, more than the log keeps: changed");

    since = CollisionChanges::count();
    expect(!CollisionChanges::isLineUnchanged(since, Vec3f(NAN, 0.0f, 0.0f), line), "a line that is not a number: changed");

    // Random lines and spheres, many grazing the line: whatever the float test finds touching counts as reaching.
    u32 touched = 0;
    u32 missedFar = 0;
    for (int i = 0; i < 2000000; i++) {
        const f32 size = (i & 1) ? 100000.0f : 3000.0f;
        const Vec3f lineStart(randomIn(-size, size), randomIn(-size, size), randomIn(-size, size));
        const Vec3f lineVector(randomIn(-2000.0f, 2000.0f), randomIn(-2000.0f, 2000.0f), randomIn(-2000.0f, 2000.0f));
        const f32 along = randomIn(-0.2f, 1.2f);
        const Vec3f onLine(lineStart.x + lineVector.x * along, lineStart.y + lineVector.y * along, lineStart.z + lineVector.z * along);
        const f32 radius = randomIn(0.0f, 2000.0f);
        // A centre at about the radius from the line's point: grazing, in or out by a little.
        Vec3f off(randomIn(-1.0f, 1.0f), randomIn(-1.0f, 1.0f), randomIn(-1.0f, 1.0f));
        const f32 offLength = std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z);
        if (offLength < 1e-3f) {
            continue;
        }
        const f32 distance = radius * randomIn(0.999f, 1.001f) + randomIn(-0.01f, 0.01f);
        const Vec3f center(onLine.x + off.x / offLength * distance, onLine.y + off.y / offLength * distance,
                           onLine.z + off.z / offLength * distance);

        const u64 before = CollisionChanges::count();
        CollisionChanges::note(center, radius);
        const bool unchanged = CollisionChanges::isLineUnchanged(before, lineStart, lineVector);
        if (touchesInFloats(center, radius, lineStart, lineVector)) {
            touched++;
            if (unchanged) {
                std::printf("a touching sphere passed: centre (%g, %g, %g) radius %g, line (%g, %g, %g) + (%g, %g, %g)\n", center.x,
                            center.y, center.z, radius, lineStart.x, lineStart.y, lineStart.z, lineVector.x, lineVector.y,
                            lineVector.z);
                expect(false, "a sphere the float test finds touching counts as reaching the line");
                break;
            }
        }

        // The same sphere moved far off: unchanged.
        const f32 far = radius + 50.0f + 2e-3f * (std::fabs(center.x) + std::fabs(center.y) + std::fabs(center.z) + 4.0f * size);
        const u64 farBefore = CollisionChanges::count();
        CollisionChanges::note(Vec3f(onLine.x + off.x / offLength * far, onLine.y + off.y / offLength * far,
                                     onLine.z + off.z / offLength * far), radius);
        if (along >= 0.0f && along <= 1.0f && CollisionChanges::isLineUnchanged(farBefore, lineStart, lineVector)) {
            missedFar++;
        }
    }
    std::printf("random grazing spheres: %u touching in floats, all counted; %u far ones left the line unchanged\n", touched, missedFar);
    expect(touched > 100000, "many random spheres touch");
    expect(missedFar > 100000, "far spheres leave the line unchanged");

    std::printf("collision_changes_tests: %s\n", sFailed == 0 ? "all passed" : "FAILED");
    return sFailed == 0 ? 0 : 1;
}
