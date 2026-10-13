#include "galaxy/tilt.h"

#include <algorithm>
#include <cmath>

namespace galaxy::input::tilt {

namespace {

constexpr float kDegrees = 3.14159265358979f / 180.0f;

float dot(const Vec3& a, const Vec3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalized(const Vec3& v, const Vec3& fallback) noexcept {
    const float length = std::sqrt(dot(v, v));
    if (!(length > 1e-6f) || !std::isfinite(length)) return fallback;
    return {v[0] / length, v[1] / length, v[2] / length};
}

}  // namespace

const char* section_name(Section section) noexcept {
    switch (section) {
    case Section::StarBall: return "star-ball";
    case Section::Ray: return "ray";
    case Section::None: break;
    }
    return "none";
}

Vec3 relative_up(const Vec3& up_in, const Vec3& neutral_in) noexcept {
    constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
    const Vec3 up = normalized(up_in, kUp);
    const Vec3 neutral = normalized(neutral_in, kUp);
    const float c = std::clamp(dot(neutral, kUp), -1.0f, 1.0f);
    Vec3 axis = cross(neutral, kUp);
    const float s = std::sqrt(dot(axis, axis));
    if (s < 1e-6f) {
        if (c > 0.0f) return up;
        // Upside down: half a turn about the right axis.
        return {up[0], -up[1], -up[2]};
    }
    axis = {axis[0] / s, axis[1] / s, axis[2] / s};
    // Rodrigues: v cos + (k x v) sin + k (k.v)(1 - cos).
    const Vec3 kv = cross(axis, up);
    const float kd = dot(axis, up) * (1.0f - c);
    return normalized({up[0] * c + kv[0] * s + axis[0] * kd, up[1] * c + kv[1] * s + axis[1] * kd,
                       up[2] * c + kv[2] * s + axis[2] * kd},
                      kUp);
}

Vec3 up_from_angles(float roll_degrees, float pitch_degrees) noexcept {
    const float roll = (std::isfinite(roll_degrees) ? roll_degrees : 0.0f) * kDegrees;
    const float pitch = (std::isfinite(pitch_degrees) ? pitch_degrees : 0.0f) * kDegrees;
    return {-std::cos(pitch) * std::sin(roll), std::cos(pitch) * std::cos(roll), std::sin(pitch)};
}

Vec3 up_from_stick(Section section, float x, float y) noexcept {
    x = std::clamp(std::isfinite(x) ? x : 0.0f, -1.0f, 1.0f);
    y = std::clamp(std::isfinite(y) ? y : 0.0f, -1.0f, 1.0f);
    if (section == Section::Ray) return up_from_angles(x * kRayStickDegrees, 0.0f);
    return up_from_angles(x * kStarBallStickDegrees, y * kStarBallStickDegrees);
}

Vec3 remote_gravity(Section section, const Vec3& relative_in) noexcept {
    const Vec3 u = normalized(relative_in, {0.0f, 1.0f, 0.0f});
    switch (section) {
    case Section::StarBall: {
        // Flat controller -> upright remote (top up, A face toward the
        // player), then the 10-degree lean of clacXY's neutral.
        const Vec3 g{-u[0], -u[1], u[2]};
        const float c = std::cos(kStarBallLeanDegrees * kDegrees);
        const float s = std::sin(kStarBallLeanDegrees * kDegrees);
        return normalized({g[0], g[1] * c + g[2] * s, -g[1] * s + g[2] * c}, {0.0f, -1.0f, 0.0f});
    }
    case Section::Ray:
        // Flat controller -> flat remote pointing at the screen.
        return normalized({-u[0], u[2], u[1]}, {0.0f, 0.0f, 1.0f});
    case Section::None:
        break;
    }
    return {0.0f, 0.0f, 1.0f};
}

Counts accel_counts(const Vec3& g) noexcept {
    const auto count = [](int base, float value) {
        const float v = std::isfinite(value) ? std::clamp(value, -3.0f, 3.0f) : 0.0f;
        return static_cast<std::uint16_t>(std::clamp(base + static_cast<int>(std::lround(v * 100.0f)), 0, 1023));
    };
    return {count(0x1FC, g[0]), count(0x204, g[1]), count(0x200, g[2])};
}

Tilt::Output Tilt::update(Section active, const Vec3* controller_up, float stick_x, float stick_y,
                          bool recenter) noexcept {
    if (active == Section::None) {
        section = Section::None;
        neutral_valid = false;
        return {};
    }
    if (active != section) {
        section = active;
        neutral_valid = false;
    }
    Output out{};
    out.active = true;
    if (controller_up == nullptr) {
        const bool stick = std::hypot(stick_x, stick_y) > kStickSteer;
        out.from_stick = true;
        out.game = remote_gravity(section, up_from_stick(section, stick ? stick_x : 0.0f, stick ? stick_y : 0.0f));
        return out;
    }
    if (!neutral_valid || recenter) {
        neutral = *controller_up;
        neutral_valid = true;
    }
    out.game = remote_gravity(section, relative_up(*controller_up, neutral));
    return out;
}

}  // namespace galaxy::input::tilt
