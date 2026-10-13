// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Game-side motion signals (generation-20 site hooks) for frame interpolation
// and motion vectors.
//
// Object epochs: a counter per guest object (LiveActor, its J3DModel, a JPA
// emitter), bumped when the game resets the object's position or starts a new
// life (MR::resetPosition, LiveActor::makeActorAppeared / makeActorDead,
// MarioAccess::setTrans / setBaseMtx, JPABaseEmitter::init). Draw-identity
// markers carry their object's epoch, so a new life is a new identity: it is
// drawn at its own frame instead of blended from the old one.
//
// Camera epoch: counts frames whose view the game set without blending.
// CameraViewInterpolator::reduceOscillation consumes the force-camera-change
// flag (set by setInterpolation(0), CameraDirector::resetCameraMan and a new
// CameraDirector) and copies the target matrix unsmoothed; a set flag there
// is a hard cut.
//
// Host markers (op 0xF0, kind byte): 1-4 draw identity (fifo_parser.h); 5 draw
// category or scene phase; 6 scene signals (first marker of a 3D scene); 7
// pass; 8 light load; 9-11 the 2D layer's identity (layout pane, layout
// actor, star pointer).
//
// 2D layer: GameScene::draw2D's entry and epilogue are FIFO markers like the
// 3D scene's (gx::FifoMarker::Scene2DBegin / Scene2DEnd). Layout markers sit
// at the start of a pane's, layout actor's or star pointer's draw: a function
// entry or the path after its visibility test, never inside a GXBegin.
//
// No Windows/D3D12 includes (unit-tested offline).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::motion {

// SiteHookKind::MotionCamera arguments.
enum class CameraSite : std::uint32_t {
    ReduceOscillation = 0,      // r3 CameraViewInterpolator
    DirectorResetCameraMan = 1, // r3 CameraDirector
    ResetCameraMan = 2,         // MR::resetCameraMan
    StartInterpolation = 3,     // MR::startCameraInterpolation, r3 frames
    SetInterpolation = 4,       // CameraViewInterpolator::setInterpolation, r3, r4 frames
};

// SiteHookKind::MotionObject arguments (r3 the object).
enum class ObjectSite : std::uint32_t {
    ResetPosition = 0,
    ResetPositionTo = 1,
    ResetPositionNamed = 2,
    Appeared = 3,
    Dead = 4,
    PlayerTrans = 5,
    PlayerBaseMtx = 6,
    EmitterInit = 7,
    Count = 8,
};

// SiteHookKind::LightLoad arguments.
enum class LightSite : std::uint32_t {
    ActorLightInfo = 0,
    Coin = 1,
    PointLight = 2,
    AllWhite = 3,
    Player = 4,
};

// SiteHookKind::Layout2D arguments.
enum class Layout2DSite : std::uint32_t {
    SceneBegin = 0,           // GameScene::draw2D entry
    SceneEnd = 1,             // GameScene::draw2D epilogue (after the WipeLayout category)
    Pane = 2,                 // nw4r::lyt::Pane::Draw, visible path (r3 the pane)
    LayoutActor = 3,          // LayoutActor::draw after MR::isExecuteDrawLayout (r31 the actor)
    StarPointerLayout = 4,    // StarPointerLayout::draw, drawn path (r31)
    StarPointerDirector = 5,  // StarPointerDirector::draw entry (r3)
};

// Host marker kinds.
inline constexpr std::uint8_t kMarkerShapePacket = 1u;
inline constexpr std::uint8_t kMarkerNameObj = 2u;
inline constexpr std::uint8_t kMarkerEmitter = 3u;
inline constexpr std::uint8_t kMarkerParticle = 4u;
inline constexpr std::uint8_t kMarkerCategory = 5u;   // aux source, a type or phase
inline constexpr std::uint8_t kMarkerScene = 6u;      // aux flags, a camera epoch, b object events
inline constexpr std::uint8_t kMarkerPass = 7u;       // aux PassMarker
inline constexpr std::uint8_t kMarkerLight = 8u;      // aux LightSite, a light info
inline constexpr std::uint8_t kMarkerLayoutPane = 9u;    // aux kLayoutIn2D, a pane, b its parent (0 root)
inline constexpr std::uint8_t kMarkerLayoutActor = 10u;  // aux kLayoutIn2D, a LayoutActor, b its vtable
inline constexpr std::uint8_t kMarkerStarPointer = 11u;  // aux kLayoutIn2D | kStarPointerDirector; a, b vtable (layout)
inline constexpr std::uint8_t kMarkerKinds = 12u;

// Kinds 9-11 aux bits.
inline constexpr std::uint16_t kLayoutIn2D = 1u;           // drawn inside GameScene::draw2D
inline constexpr std::uint16_t kStarPointerDirector = 2u;  // kind 11: StarPointerDirector::draw (else StarPointerLayout)

// Kind 6 flags.
inline constexpr std::uint16_t kSceneCameraCut = 1u;  // the view jumped since the previous scene

// Kind 7 passes.
enum class PassMarker : std::uint16_t { MirrorBegin = 1, MirrorEnd = 2 };

// Kind 5 sources (DrawCategory argument bits 8-15).
inline constexpr std::uint16_t kCategoryDrawType = 1u;   // CategoryList::execute(MR::DrawType)
inline constexpr std::uint16_t kCategoryBufferOpa = 2u;  // CategoryList::drawOpa(MR::DrawBufferType)
inline constexpr std::uint16_t kCategoryBufferXlu = 3u;  // CategoryList::drawXlu(MR::DrawBufferType)
inline constexpr std::uint16_t kCategoryPhase = 4u;      // SceneFunction phase (argument bits 0-7)

// SceneFunction draw3D phases, in draw order.
enum class ScenePhase : std::uint8_t {
    OpaBeforeVolumeShadow = 0,
    OpaBeforeSilhouette = 1,
    Opa = 2,
    Xlu = 3,
    ListOpa = 4,
    ListXlu = 5,
    SilhouetteAndFillShadow = 6,
    AlphaShadow = 7,
    AfterIndirect = 8,
    ImageEffect = 9,
    None = 0xFF,
};

[[nodiscard]] inline const char* scene_phase_name(std::uint8_t phase) noexcept {
    static constexpr const char* kNames[] = {
        "opa-before-volume-shadow", "opa-before-silhouette", "opa", "xlu", "list-opa",
        "list-xlu", "silhouette-fill-shadow", "alpha-shadow", "after-indirect", "image-effect"};
    return phase < sizeof(kNames) / sizeof(kNames[0]) ? kNames[phase] : "none";
}

// A record's category: the SceneFunction phase in force (bits 16-23, 0xFF
// none) and the latest CategoryList call (source bits 8-15, type bits 0-7).
[[nodiscard]] constexpr std::uint32_t encode_category(std::uint8_t phase, std::uint16_t source,
                                                      std::uint32_t type) noexcept {
    return (static_cast<std::uint32_t>(phase) << 16u) | ((static_cast<std::uint32_t>(source) & 0xFFu) << 8u) |
           (type & 0xFFu);
}
[[nodiscard]] constexpr std::uint8_t category_phase(std::uint32_t category) noexcept {
    return static_cast<std::uint8_t>(category >> 16u);
}
inline constexpr std::uint32_t kNoCategory = encode_category(0xFFu, 0u, 0u);

// Guest layout (RMGE01; Petari headers).
inline constexpr std::uint32_t kInterpolatorAntiOscillation = 0x08u;  // CameraViewInterpolator
inline constexpr std::uint32_t kInterpolatorForceChange = 0x09u;
inline constexpr std::uint32_t kInterpolatorOff = 0x0Au;
inline constexpr std::uint32_t kActorPosition = 0x0Cu;                // LiveActor::mPosition
inline constexpr std::uint32_t kActorModelManager = 0x48u;            // LiveActor
inline constexpr std::uint32_t kActorDeadFlag = 0x68u;                // LiveActorFlag::mIsDead (MR::isDead)
inline constexpr std::uint32_t kModelManagerModel = 0x1Cu;            // ModelManager -> J3DModel
inline constexpr std::uint32_t kShapePacketModel = 0x38u;             // J3DShapePacket -> J3DModel
inline constexpr std::uint32_t kPaneParent = 0x0Cu;                   // nw4r::lyt::Pane::mpParent
inline constexpr std::uint32_t kPaneName = 0xB8u;                     // nw4r::lyt::Pane::mName (16 + NUL)

// Epochs of guest objects. Open addressing, no deletion: a full table is
// cleared and a salt added to every epoch, so all identities change once.
// Epochs are 15 bits (they travel in a marker's aux field above its pass bit).
class EpochTable {
public:
    static constexpr std::size_t kCapacity = std::size_t{1} << 15u;
    static constexpr std::uint16_t kMask = 0x7FFFu;

    [[nodiscard]] std::uint16_t get(std::uint32_t object) const noexcept {
        if (used_ == 0u || object == 0u) {
            return salt_;
        }
        std::size_t slot = hash(object);
        while (keys_[slot] != 0u) {
            if (keys_[slot] == object) {
                return static_cast<std::uint16_t>((values_[slot] + salt_) & kMask);
            }
            slot = (slot + 1u) & (kCapacity - 1u);
        }
        return salt_;
    }

    // Returns the object's new epoch.
    std::uint16_t bump(std::uint32_t object) {
        if (object == 0u) {
            return salt_;
        }
        if (keys_.empty()) {
            keys_.assign(kCapacity, 0u);
            values_.assign(kCapacity, 0u);
        }
        std::size_t slot = hash(object);
        while (keys_[slot] != 0u && keys_[slot] != object) {
            slot = (slot + 1u) & (kCapacity - 1u);
        }
        if (keys_[slot] == 0u) {
            if (used_ + 1u > kCapacity * 3u / 4u) {
                reset();
                return bump(object);
            }
            keys_[slot] = object;
            values_[slot] = 0u;
            ++used_;
        }
        values_[slot] = static_cast<std::uint16_t>((values_[slot] + 1u) & kMask);
        return static_cast<std::uint16_t>((values_[slot] + salt_) & kMask);
    }

    [[nodiscard]] std::size_t size() const noexcept { return used_; }
    [[nodiscard]] std::uint32_t resets() const noexcept { return resets_; }

private:
    static std::size_t hash(std::uint32_t object) noexcept {
        return static_cast<std::size_t>((object * 0x9E3779B1u) >> (32u - 15u));
    }
    void reset() noexcept {
        std::fill(keys_.begin(), keys_.end(), 0u);
        used_ = 0u;
        salt_ = static_cast<std::uint16_t>((salt_ + 0x2B5u) & kMask);
        ++resets_;
    }
    std::vector<std::uint32_t> keys_;
    std::vector<std::uint16_t> values_;
    std::size_t used_ = 0u;
    std::uint16_t salt_ = 0u;
    std::uint32_t resets_ = 0u;
};

// Particle continuity: a JPA particle's age advances by one per calculated
// frame (0 while its emitter is paused). A pooled node holding a new particle
// restarts at age 0, so a key match whose age went down or jumped is a new life.
[[nodiscard]] constexpr bool particle_age_continues(std::uint32_t age_before, std::uint32_t age_now) noexcept {
    return age_now >= age_before && age_now - age_before <= 1u;
}

// MarioAccess::setTrans / setBaseMtx also carry the player every frame (demos,
// carriers, the dome's galaxy map): only a jump of more than this many units
// from the player's position is a new life. Walking moves ~15 units a frame.
inline constexpr float kPlayerTeleportDistance = 500.0f;

[[nodiscard]] constexpr bool player_jump_is_new_life(const float now[3], const float target[3]) noexcept {
    const float dx = target[0] - now[0];
    const float dy = target[1] - now[1];
    const float dz = target[2] - now[2];
    // Comparisons are false for NaN: an unreadable position is no new life.
    return dx * dx + dy * dy + dz * dz > kPlayerTeleportDistance * kPlayerTeleportDistance;
}

}  // namespace galaxy::motion
