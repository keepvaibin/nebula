// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Game-side motion signals for frame interpolation and motion vectors: host
// markers the game writes through its own calls (Petari's MotionSignal,
// Game/Util/MotionSignal.hpp).
//
// Object epochs: a counter per game object (LiveActor, its J3DModel, a JPA
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
// actor, star pointer); 12 material.
//
// 2D layer: GameScene::draw2D's entry and epilogue are FIFO markers like the
// 3D scene's (gx::FifoMarker::Scene2DBegin / Scene2DEnd). Layout markers sit
// at the start of a pane's, layout actor's or star pointer's draw: a function
// entry or the path after its visibility test, never inside a GXBegin.
//
// No Windows or D3D12 includes: the header is unit-tested on its own.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::motion {

// The light load a kMarkerLight marker reports (its aux field).
enum class LightSite : std::uint32_t {
    ActorLightInfo = 0,
    Coin = 1,
    PointLight = 2,
    AllWhite = 3,
    Player = 4,
};

// Host marker kinds.
inline constexpr std::uint8_t kMarkerShapePacket = 1u;
inline constexpr std::uint8_t kMarkerNameObj = 2u;
inline constexpr std::uint8_t kMarkerEmitter = 3u;
inline constexpr std::uint8_t kMarkerParticle = 4u;
inline constexpr std::uint8_t kMarkerCategory = 5u;   // aux source, a type or phase
inline constexpr std::uint8_t kMarkerScene = 6u;      // aux flags, a camera epoch, b object events
inline constexpr std::uint8_t kMarkerPass = 7u;       // aux PassMarker
inline constexpr std::uint8_t kMarkerLight = 8u;      // aux LightSite, a light info, b follow-camera bits
inline constexpr std::uint8_t kMarkerLayoutPane = 9u;    // aux kLayoutIn2D, a pane, b its parent (0 root)
inline constexpr std::uint8_t kMarkerLayoutActor = 10u;  // aux kLayoutIn2D, a LayoutActor, b its vtable
inline constexpr std::uint8_t kMarkerStarPointer = 11u;  // aux kLayoutIn2D | kStarPointerDirector; a, b vtable (layout)
inline constexpr std::uint8_t kMarkerMaterial = 12u;     // a the material's identity (rt_material_identity; 0 unknown)
inline constexpr std::uint8_t kMarkerEffect = 13u;       // a the emitter's effect identity ("Effect/<particle name>")
// aux 1: the draws up to the next one (aux 0) are the traced scene's alone, never drawn to the picture (the player's
// body in the first-person view, MarioActor::drawTracedBody).
inline constexpr std::uint8_t kMarkerTraceOnly = 14u;
// The game's bloom through the renderer (galaxy/gx/game_bloom.h; NEBULA_OPTION_BLOOM), aux the phase: kBloomKeep the
// scene before the bloom works over it (BloomEffect::preDraw; BloomEffectSimple::draw's start with its own chain);
// kBloomNormal BloomEffect::postDraw drawn by the renderer, a the threshold, the two blurs' tap weights and the
// intensity (8 bits each, low first), b the rings' horizontal radius per unit (float bits: 1 / the camera's aspect);
// kBloomSimple BloomEffectSimple::draw drawn by the renderer, a the threshold, the channel (0 intensity, 1-3 red,
// green, blue), the tap weight and the intensity, b the ring's vertical radius per unit; kBloomRestore the game's own
// chain drawing its kept scene back (the kept one instead).
inline constexpr std::uint8_t kMarkerBloom = 15u;
inline constexpr std::uint16_t kBloomKeep = 1u;
inline constexpr std::uint16_t kBloomNormal = 2u;
inline constexpr std::uint16_t kBloomSimple = 3u;
inline constexpr std::uint16_t kBloomRestore = 4u;

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

// Particle continuity: a JPA particle's age advances by one per calculated
// frame (0 while its emitter is paused). A pooled node holding a new particle
// restarts at age 0, so a key match whose age went down or jumped is a new life.
[[nodiscard]] constexpr bool particle_age_continues(std::uint32_t age_before, std::uint32_t age_now) noexcept {
    return age_now >= age_before && age_now - age_before <= 1u;
}

}  // namespace galaxy::motion
