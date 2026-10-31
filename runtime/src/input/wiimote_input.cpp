#include "galaxy/input/wiimote_input.h"

#include <algorithm>
#include <cmath>

namespace galaxy::input::wiimote {
namespace {

// The camera picture in the pointer's units: x -1..1 across its 1024 columns, y up, the same scale.
constexpr float kCameraScale = 2.0f / 1024.0f;
constexpr float kCameraCenterX = 1023.0f / 1024.0f;
constexpr float kCameraCenterY = 767.0f / 1024.0f;
// Lights this close to the picture's edge may be cut off: not used.
constexpr float kEdgeMargin = 0.05f;
// The bar's lights' spacing over the camera's field of view: their spacing in the picture at distance 1.
constexpr float kBarSpan = 0.2f / 0.383864f;
// Spacings that read as the bar: nearer than the distance where the lights fill the picture, farther than 4.
constexpr float kNearestDistance = kBarSpan;
constexpr float kFarthestDistance = 4.0f;
// A pair reads as the bar when its tilt is within ~25 degrees of the remote's roll.
constexpr float kRollAgreement = 0.9f;
// The bar above the screen: the picture's centre is where the remote points when the bar sits 0.2 below the camera's
// centre line. The pointer reaches the picture's edges where the bar reaches the camera's (the shorter half-height,
// 0.75 - 0.2, against the half-diagonal 1.25).
constexpr float kBarHeight = 0.2f;
constexpr float kPointerScale = 1.25f / 0.55f;
// The game's smoothing of a camera pointer: half of each step, all of it from 0.03 on, less below (fourth power).
constexpr float kPointerPlay = 0.03f;
constexpr float kPointerSensitivity = 0.5f;

// A calibration block's checksum: its first `bytes` bytes summed, plus `add`, is the next byte.
bool checksum_ok(const std::uint8_t* block, int bytes, std::uint8_t add) {
    int sum = 0;
    for (int i = 0; i < bytes; ++i) sum += block[i];
    return static_cast<std::uint8_t>(sum + add) == block[bytes];
}

std::array<float, 3> ten_bit(const std::uint8_t* high, std::uint8_t lows) {
    return {static_cast<float>((high[0] << 2) | ((lows >> 4) & 3)), static_cast<float>((high[1] << 2) | ((lows >> 2) & 3)),
            static_cast<float>((high[2] << 2) | (lows & 3))};
}

void set_scale(std::array<float, 3>& scale, const std::array<float, 3>& zero, const std::array<float, 3>& one,
               float fallback) {
    for (std::size_t i = 0; i < 3; ++i) {
        const float range = one[i] - zero[i];
        scale[i] = range != 0.0f ? 1.0f / range : fallback;
    }
}

bool in_frame(float x, float y) {
    return std::fabs(x) < 1.0f - kEdgeMargin && std::fabs(y) < 0.75f - kEdgeMargin;
}

}  // namespace

std::size_t ir_lights_extended(std::span<const std::uint8_t> bytes, std::array<IrLight, 4>& out) noexcept {
    std::size_t count = 0;
    for (std::size_t i = 0; i < 4 && bytes.size() >= (i + 1) * 3; ++i) {
        const std::uint8_t* b = bytes.data() + i * 3;
        const int x = b[0] | ((b[2] >> 4) & 3) << 8;
        const int y = b[1] | ((b[2] >> 6) & 3) << 8;
        if ((b[2] & 0x0f) == 0 || x >= 1023 || y >= 767) continue;
        out[count++] = {static_cast<float>(x), static_cast<float>(y)};
    }
    return count;
}

std::size_t ir_lights_basic(std::span<const std::uint8_t> bytes, std::array<IrLight, 4>& out) noexcept {
    std::size_t count = 0;
    for (std::size_t pair = 0; pair < 2 && bytes.size() >= (pair + 1) * 5; ++pair) {
        const std::uint8_t* b = bytes.data() + pair * 5;
        const int x1 = b[0] | ((b[2] >> 4) & 3) << 8;
        const int y1 = b[1] | ((b[2] >> 6) & 3) << 8;
        const int x2 = b[3] | (b[2] & 3) << 8;
        const int y2 = b[4] | ((b[2] >> 2) & 3) << 8;
        if (x1 < 1023 && y1 < 767) out[count++] = {static_cast<float>(x1), static_cast<float>(y1)};
        if (x2 < 1023 && y2 < 767) out[count++] = {static_cast<float>(x2), static_cast<float>(y2)};
    }
    return count;
}

bool sensor_bar_view(std::span<const IrLight> lights, const Gravity& gravity, BarView& out) noexcept {
    // The remote's roll from gravity, as the screen would see a level bar.
    float level_x = 1.0f;
    float level_y = 0.0f;
    const float roll_length = std::sqrt(gravity[2] * gravity[2] + gravity[0] * gravity[0]);
    if (roll_length > 0.0f && roll_length < 2.0f) {
        level_x = gravity[2] / roll_length;
        level_y = gravity[0] / roll_length;
    }
    std::array<float, 4> px{};
    std::array<float, 4> py{};
    const std::size_t count = std::min<std::size_t>(lights.size(), 4);
    for (std::size_t i = 0; i < count; ++i) {
        px[i] = lights[i].x * kCameraScale - kCameraCenterX;
        py[i] = (767.0f - lights[i].y) * kCameraScale - kCameraCenterY;
    }
    float best = kRollAgreement;
    bool found = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!in_frame(px[i], py[i])) continue;
        for (std::size_t j = i + 1; j < count; ++j) {
            if (!in_frame(px[j], py[j])) continue;
            const float vx = px[j] - px[i];
            const float vy = py[j] - py[i];
            const float span = std::sqrt(vx * vx + vy * vy);
            if (span <= 0.0f) continue;
            const float distance = kBarSpan / span;
            if (distance <= kNearestDistance || distance >= kFarthestDistance) continue;
            float roll_x = vx / span;
            float roll_y = -vy / span;
            float agreement = roll_x * level_x + roll_y * level_y;
            if (agreement < 0.0f) {
                roll_x = -roll_x;
                roll_y = -roll_y;
                agreement = -agreement;
            }
            if (agreement <= best) continue;
            best = agreement;
            found = true;
            // Undo the roll about the pair's midpoint, then from the camera's view to the picture's position.
            const float mx = (px[i] + px[j]) * 0.5f;
            const float my = (py[i] + py[j]) * 0.5f;
            const float cosine = roll_x;
            const float sine = roll_y;
            const float level_mx = cosine * mx - sine * my;
            const float level_my = sine * mx + cosine * my;
            out.x = -level_mx * kPointerScale;
            out.y = (-kBarHeight - level_my) * kPointerScale;
            out.roll_x = roll_x;
            out.roll_y = roll_y;
            out.distance = distance;
        }
    }
    return found;
}

void Reader::connect(std::span<const std::uint8_t> eeprom, std::span<const std::uint8_t> nunchuk_calibration,
                     bool nunchuk) {
    nunchuk_ = nunchuk;
    stick_origin_set_ = false;
    pointing_ = false;
    // The accelerometer's 0 g and 1 g, twice from 0x16 with a checksum each; the first good one.
    std::array<float, 3> zero{530.0f, 530.0f, 530.0f};
    std::array<float, 3> one{636.0f, 636.0f, 636.0f};
    for (std::size_t block = 0; block < 2 && eeprom.size() >= 0x16 + (block + 1) * 10; ++block) {
        const std::uint8_t* p = eeprom.data() + 0x16 + block * 10;
        if (!checksum_ok(p, 9, 0x55)) continue;
        zero = ten_bit(p, p[3]);
        one = ten_bit(p + 4, p[7]);
        break;
    }
    zero_ = zero;
    set_scale(scale_, zero, one, 0.01f);
    // The Nunchuk's, two 16-byte blocks with two checksums each; the last good one.
    std::array<float, 3> nunchuk_zero{512.0f, 512.0f, 512.0f};
    std::array<float, 3> nunchuk_one{716.0f, 716.0f, 716.0f};
    for (std::size_t block = 0; block < 2 && nunchuk_calibration.size() >= (block + 1) * 16; ++block) {
        const std::uint8_t* p = nunchuk_calibration.data() + block * 16;
        int sum = 0;
        for (int i = 0; i < 14; ++i) sum += p[i];
        if (static_cast<std::uint8_t>(sum + 0x55) != p[14] || static_cast<std::uint8_t>(sum + 0xAA) != p[15]) continue;
        nunchuk_zero = ten_bit(p, p[3]);
        nunchuk_one = ten_bit(p + 4, p[7]);
    }
    nunchuk_zero_ = nunchuk_zero;
    set_scale(nunchuk_scale_, nunchuk_zero, nunchuk_one, 1.0f / 200.0f);
}

bool Reader::read(std::span<const std::uint8_t> report, PlayerInput& out) {
    if (report.size() < 3 || report[0] < 0x30 || report[0] > 0x37) {
        return false;
    }
    const std::uint8_t id = report[0];
    const std::uint8_t b1 = report[1];
    const std::uint8_t b2 = report[2];
    out = {};
    ButtonState& buttons = out.buttons;
    buttons.left = (b1 & 0x01) != 0;
    buttons.right = (b1 & 0x02) != 0;
    buttons.down = (b1 & 0x04) != 0;
    buttons.up = (b1 & 0x08) != 0;
    buttons.plus = (b1 & 0x10) != 0;
    buttons.two = (b2 & 0x01) != 0;
    buttons.one = (b2 & 0x02) != 0;
    buttons.b = (b2 & 0x04) != 0;
    buttons.a = (b2 & 0x08) != 0;
    buttons.minus = (b2 & 0x10) != 0;
    buttons.home = (b2 & 0x80) != 0;

    // The accelerometer: 8 high bits a byte, X's 2 low bits and Y's and Z's second bit in the button bytes.
    const bool has_accel = id == 0x31 || id == 0x33 || id == 0x35 || id == 0x37;
    if (has_accel && report.size() >= 6) {
        const float raw[3] = {static_cast<float>((report[3] << 2) | ((b1 >> 5) & 3)),
                              static_cast<float>((report[4] << 2) | ((b2 >> 4) & 2)),
                              static_cast<float>((report[5] << 2) | ((b2 >> 5) & 2))};
        for (std::size_t i = 0; i < 3; ++i) gravity_[i] = (raw[i] - zero_[i]) * scale_[i];
    }
    out.gravity = gravity_;

    std::array<IrLight, 4> lights{};
    std::size_t light_count = 0;
    std::span<const std::uint8_t> extension;
    if (id == 0x33 && report.size() >= 18) {
        light_count = ir_lights_extended(report.subspan(6, 12), lights);
    } else if (id == 0x37 && report.size() >= 22) {
        light_count = ir_lights_basic(report.subspan(6, 10), lights);
        extension = report.subspan(16, 6);
    } else if (id == 0x32 && report.size() >= 9) {
        extension = report.subspan(3, 6);
    } else if (id == 0x35 && report.size() >= 12) {
        extension = report.subspan(6, 6);
    } else if (id == 0x36 && report.size() >= 19) {
        light_count = ir_lights_basic(report.subspan(3, 10), lights);
        extension = report.subspan(13, 6);
    } else if (id == 0x34 && report.size() >= 9) {
        extension = report.subspan(3, 6);
    }
    point(std::span<const IrLight>(lights.data(), light_count), out);

    if (nunchuk_ && extension.size() == 6) {
        const std::uint8_t* e = extension.data();
        buttons.c = (e[5] & 0x02) == 0;
        buttons.z = (e[5] & 0x01) == 0;
        // The stick from where it rested at the connection, in counts.
        if (!stick_origin_set_) {
            stick_origin_set_ = true;
            stick_origin_x_ = e[0];
            stick_origin_y_ = e[1];
        }
        const int sx = std::clamp(static_cast<int>(e[0]) - stick_origin_x_, -128, 127);
        const int sy = std::clamp(static_cast<int>(e[1]) - stick_origin_y_, -128, 127);
        const StickValue stick = nunchuk_stick_counts(static_cast<float>(sx), static_cast<float>(sy));
        out.stick_x = stick.x;
        out.stick_y = stick.y;
        const float raw[3] = {static_cast<float>((e[2] << 2) | ((e[5] >> 2) & 3)),
                              static_cast<float>((e[3] << 2) | ((e[5] >> 4) & 3)),
                              static_cast<float>((e[4] << 2) | ((e[5] >> 6) & 3))};
        for (std::size_t i = 0; i < 3; ++i) out.nunchuk_gravity[i] = (raw[i] - nunchuk_zero_[i]) * nunchuk_scale_[i];
    }
    return true;
}

void Reader::point(std::span<const IrLight> lights, PlayerInput& out) {
    BarView view{};
    if (!sensor_bar_view(lights, gravity_, view) || !remote_faces_screen(gravity_)) {
        pointing_ = false;
        out.pointer_on = false;
        return;
    }
    if (pointing_) {
        const float dx = view.x - pointer_x_;
        const float dy = view.y - pointer_y_;
        const float step = std::sqrt(dx * dx + dy * dy);
        float weight = 1.0f;
        if (step < kPointerPlay) {
            weight = step / kPointerPlay;
            weight *= weight;
            weight *= weight;
        }
        weight *= kPointerSensitivity;
        pointer_x_ += weight * dx;
        pointer_y_ += weight * dy;
    } else {
        pointer_x_ = view.x;
        pointer_y_ = view.y;
    }
    pointing_ = true;
    out.pointer_on = true;
    out.pointer_x = pointer_x_;
    out.pointer_y = pointer_y_;
    out.roll_x = view.roll_x;
    out.roll_y = view.roll_y;
    out.distance = view.distance;
}

}  // namespace galaxy::input::wiimote
