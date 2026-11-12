#include "Game/System/LumaDrawView.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaDraw.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <luma_draw_view.h>
#include <luma_modlog.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_math.h>
#include <cstring>

// The draw.view service (plan 2.4 layer 7; contract draw). Game thread throughout: the calls, the provider, render
// (GameScene::draw3D, before the scene) and released (NebulaArena::destroy).
//
// A view is a slot of one table across mods: its size, clear colour, camera (fixed, or following an actor) and a target
// texture of the draw service (LumaDraw adoptTarget: the mod's texture, drawn with draw.image and draw.billboard). Each
// frame, before the scene begins, a live view with a camera is drawn into its target: the frame's target pass, the
// view's projection, and the scene's model lists (SceneFunction's draw-buffer lists, opaque then translucent) with
// ModelSys::mViewOverride its camera, so every scene pose makes a block under the view's camera (Model::poseBlock) and
// the game's lights load in the view's eye space (the camera context's view is the view's while it draws). Everything is
// put back after: the camera context's view, ModelSys's view and role, projection, viewport and scissor.
//
// No cost with no mod: render() and released() are one load each while no view lives.

namespace luma {
    namespace draw {
        namespace view {
            namespace {
                const u32 kMost = LUMA_DRAW_VIEWS_ALL_MOST;
                const u32 kFactFrames = 120;   // frames a view is told as a fact (harness)
                const u32 cMissingCode = 101;  // Service.Argument names what is wrong (no rows of its own)

                struct View {
                    LumaHandle handle;  // 0: a free slot
                    const LumaMod* mod;
                    LumaHandle texture;  // the draw service's target texture
                    u32 width;
                    u32 height;
                    LumaColor clear;
                    bool on;
                    bool hasCamera;
                    Mtx view;            // eye from world
                    f32 fovY;
                    LiveActor* follow;   // the rig's actor; null: a fixed camera
                    u32 rig;             // with an actor: LUMA_DRAW_VIEW_RIG_FOLLOW, _JOINT or _WORLD
                    Vec3f offset;        // FOLLOW: the eye from the actor in its axes (x side, y up, z front); WORLD: world axes
                    char joint[32];      // JOINT: the joint's name
                    f32 nearZ;           // the near plane; 0: the game's (MR::getRenderNearZ)
                    u32 drawn;           // frames drawn
                    u32 facts;
                };

                const LumaServiceKernel* sKernel = nullptr;
                View sViews[kMost];
                u32 sHigh = 0;
                u32 sLive = 0;
                LumaHandle sNextHandle = 0;

                void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0, const char* pArg1) {
                    const char* args[2] = {pArg0, pArg1};
                    u32 count = 0;
                    while (count < 2 && args[count] != nullptr) {
                        count++;
                    }
                    sKernel->report(pMod, pWhere, code, args, count);
                }

                s32 badArgument(const LumaMod* pMod, const char* pWhere, const char* pWhat) {
                    report(pMod, pWhere, cMissingCode, pWhat, nullptr);
                    return LUMA_E_ARGUMENT;
                }

                s32 enter(const LumaMod* pMod, const char* pWhere) {
                    if (pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                        sKernel->unknown_mod(pWhere);
                        return LUMA_E_ARGUMENT;
                    }
                    if (sKernel->mod_live(pMod) == 0) {
                        return LUMA_E_CANCELLED;
                    }
                    if (sKernel->game_thread() == 0) {
                        report(pMod, pWhere, 100u, nullptr, nullptr);
                        return LUMA_E_THREAD;
                    }
                    return LUMA_OK;
                }

                View* viewOf(const LumaMod* pMod, LumaHandle handle) {
                    if (handle == 0) {
                        return nullptr;
                    }
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sViews[i].handle == handle) {
                            return sViews[i].mod == pMod ? &sViews[i] : nullptr;
                        }
                    }
                    return nullptr;
                }

                s32 enterView(const LumaMod* pMod, const char* pWhere, LumaHandle handle, View** ppView) {
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    *ppView = viewOf(pMod, handle);
                    if (*ppView == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    return LUMA_OK;
                }

                bool finite(f32 value) {
                    return value == value && value - value == 0.0f;
                }

                bool finite3(const LumaDrawVec3* pPoint) {
                    return pPoint != nullptr && finite(pPoint->x) && finite(pPoint->y) && finite(pPoint->z);
                }

                bool fovOk(f32 fovY) {
                    return finite(fovY) && fovY > LUMA_DRAW_VIEW_FOV_LEAST && fovY < LUMA_DRAW_VIEW_FOV_MOST;
                }

                // The camera at `eye` looking at `at`, `up` its up: false when the look is zero or along up.
                bool lookAt(Mtx out, const Vec3f& rEye, const Vec3f& rAt, const Vec3f& rUp) {
                    const f32 lx = rAt.x - rEye.x, ly = rAt.y - rEye.y, lz = rAt.z - rEye.z;
                    const f32 look = lx * lx + ly * ly + lz * lz;
                    const f32 up = rUp.x * rUp.x + rUp.y * rUp.y + rUp.z * rUp.z;
                    if (!(look > 1e-6f) || !(up > 1e-12f)) {
                        return false;
                    }
                    // |look x up|^2 against |look|^2 |up|^2: under a thousandth of a degree apart is along it.
                    const f32 cx = ly * rUp.z - lz * rUp.y, cy = lz * rUp.x - lx * rUp.z, cz = lx * rUp.y - ly * rUp.x;
                    if (!(cx * cx + cy * cy + cz * cz > 1e-10f * look * up)) {
                        return false;
                    }
                    const Point3d eye = {rEye.x, rEye.y, rEye.z};
                    const Point3d target = {rAt.x, rAt.y, rAt.z};
                    const Vec upVec = {rUp.x, rUp.y, rUp.z};
                    NebulaMtxLookAt(out, &eye, &upVec, &target);
                    return true;
                }

                // Whether `pName` is a joint of the actor's model (a model it has).
                bool hasJoint(const LiveActor* pActor, const char* pName) {
                    return pActor->mModelManager != nullptr && MR::isExistJoint(pActor, pName);
                }

                // A rig's eye on an actor this frame (FOLLOW, JOINT, WORLD): false for a joint the model lacks.
                bool actorEye(const LiveActor* pActor, u32 rig, const Vec3f& rOffset, const char* pJoint, Vec3f* pEye,
                              Vec3f* pAt, Vec3f* pUp) {
                    Vec3f side, front;
                    MR::calcSideVec(&side, pActor);
                    MR::calcUpVec(pUp, pActor);
                    MR::calcFrontVec(&front, pActor);
                    const Vec3f& rPlace = pActor->mPosition;
                    if (rig == LUMA_DRAW_VIEW_RIG_JOINT) {
                        if (!hasJoint(pActor, pJoint)) {
                            return false;
                        }
                        const MtxPtr pJointMtx = MR::getJointMtx(pActor, pJoint);
                        pEye->set(pJointMtx[0][3], pJointMtx[1][3], pJointMtx[2][3]);
                        pAt->set(pEye->x + front.x * 100.0f, pEye->y + front.y * 100.0f, pEye->z + front.z * 100.0f);
                        return true;
                    }
                    if (rig == LUMA_DRAW_VIEW_RIG_WORLD) {
                        pEye->set(rPlace.x + rOffset.x, rPlace.y + rOffset.y, rPlace.z + rOffset.z);
                    } else {
                        pEye->set(rPlace.x + side.x * rOffset.x + pUp->x * rOffset.y + front.x * rOffset.z,
                                  rPlace.y + side.y * rOffset.x + pUp->y * rOffset.y + front.y * rOffset.z,
                                  rPlace.z + side.z * rOffset.x + pUp->z * rOffset.y + front.z * rOffset.z);
                    }
                    pAt->set(rPlace);
                    return true;
                }

                // A followed actor's camera this frame: false once it is dead (the camera stays as it was).
                bool followNow(View& rView) {
                    LiveActor* const pActor = rView.follow;
                    if (MR::isDead(pActor)) {
                        rView.follow = nullptr;
                        return false;
                    }
                    Vec3f eye, at, up;
                    if (!actorEye(pActor, rView.rig, rView.offset, rView.joint, &eye, &at, &up)) {
                        return rView.hasCamera;  // the joint gone with a changed model: last frame's camera
                    }
                    Mtx view;
                    if (!lookAt(view, eye, at, up)) {
                        return rView.hasCamera;  // the eye straight over its up this frame: last frame's camera
                    }
                    NebulaMtxCopy(view, rView.view);
                    rView.hasCamera = true;
                    return true;
                }

                void viewFact(const View& rView, bool drawn) {
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "draw.view.%llu", static_cast< unsigned long long >(rView.handle));
                    char mod[80];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(rView.mod));
                    char json[200];
                    nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"w\":%u,\"h\":%u,\"drawn\":%u,\"follow\":%u,\"frames\":%u}",
                                    mod, rView.width, rView.height, drawn ? 1u : 0u, rView.follow != nullptr ? 1u : 0u,
                                    rView.drawn);
                    luma::modlog::fact(key, json);
                }

                void end(View& rView, const char* pWhy) {
                    luma::draw::dropTarget(rView.mod, rView.texture);
                    if (luma::modlog::facts()) {
                        char key[48];
                        nebula_snprintf(key, sizeof(key), "draw.view.end.%llu", static_cast< unsigned long long >(rView.handle));
                        char json[48];
                        nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\"}", pWhy);
                        luma::modlog::fact(key, json);
                    }
                    rView = View();
                    sLive--;
                    while (sHigh > 0 && sViews[sHigh - 1].handle == 0) {
                        sHigh--;
                    }
                }

                // One view into its target: the scene's model lists under its camera, everything put back after.
                void drawView(View& rView, GameTexture* pTarget) {
                    f32 sx, sy, sw, sh;
                    f32 viewport[6];
                    f32 projection[7];
                    Frame::getScissor(&sx, &sy, &sw, &sh);
                    Frame::getViewportv(viewport);
                    Frame::getProjectionv(projection);
                    Mtx sceneView;
                    NebulaMtxCopy(ModelSys::mViewMtx, sceneView);
                    const u32 sceneRole = ModelSys::mViewRole;
                    Pos3f contextView;
                    contextView.set(MR::getCameraViewMtx());

                    Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
                    Frame::targetBeginPixels(pTarget->image(), rView.width, rView.height, rView.clear);
                    Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
                    Mtx44 perspective;
                    NebulaMtxPerspective(perspective, rView.fovY,
                                         static_cast< f32 >(rView.width) / static_cast< f32 >(rView.height),
                                         rView.nearZ > 0.0f ? rView.nearZ : MR::getRenderNearZ(), MR::getFarZ());
                    Frame::setPerspective(perspective);
                    NebulaMtxCopy(rView.view, ModelSys::mViewMtx);
                    ModelSys::mViewRole = NEBULA_CAMERA_OTHER;
                    ModelSys::mViewOverride = rView.view;
                    Pos3f viewPos;
                    viewPos.setInline(rView.view);
                    MR::setCameraViewMtx(viewPos);  // the lights load in the view's eye space (LightFunction)
                    Frame::setPassAlpha(true);

                    CategoryList::drawOpa(MR::DrawBufferType_ClippedMapParts);
                    SceneFunction::executeDrawBufferListNormalOpaBeforeVolumeShadow();
                    SceneFunction::executeDrawBufferListNormalOpaBeforeSilhouette();
                    Frame::setPassAlpha(false);  // as GameScene::draw3D from its main opaque lists on
                    SceneFunction::executeDrawBufferListNormalOpa();
                    SceneFunction::executeDrawBufferListNormalXlu();

                    ModelSys::mViewOverride = nullptr;
                    MR::setCameraViewMtx(contextView);
                    NebulaMtxCopy(sceneView, ModelSys::mViewMtx);
                    ModelSys::mViewRole = sceneRole;
                    Frame::targetEnd(pTarget->image(), NEBULA_TARGET_RGBA);
                    Frame::setProjectionv(projection);
                    Frame::setViewport(viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]);
                    Frame::setScissor(sx, sy, sw, sh);
                }

                // ---- Calls ----

                int32_t svcCreate(const LumaMod* pMod, uint32_t w, uint32_t h, LumaColor clear, LumaDrawView* pOut) {
                    const char* const pWhere = "draw.view.create";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    if (w == 0 || w > LUMA_DRAW_VIEW_SIDE_MOST) {
                        return badArgument(pMod, pWhere, "w");
                    }
                    if (h == 0 || h > LUMA_DRAW_VIEW_SIDE_MOST) {
                        return badArgument(pMod, pWhere, "h");
                    }
                    if (pOut == nullptr) {
                        return badArgument(pMod, pWhere, "out");
                    }
                    u32 own = 0;
                    u32 slot = kMost;
                    for (u32 i = 0; i < kMost; i++) {
                        if (sViews[i].handle == 0) {
                            if (slot == kMost) {
                                slot = i;
                            }
                        } else if (sViews[i].mod == pMod) {
                            own++;
                        }
                    }
                    if (own >= LUMA_DRAW_VIEWS_MOST || slot == kMost) {
                        char most[16];
                        nebula_snprintf(most, sizeof(most), "%u", own >= LUMA_DRAW_VIEWS_MOST ? LUMA_DRAW_VIEWS_MOST : kMost);
                        report(pMod, pWhere, 104u, most, nullptr);
                        return LUMA_E_LIMIT;
                    }
                    LumaHandle texture = 0;
                    GameTexture* const pTarget = GameTexture::createPixelTarget(w, h);
                    if (const s32 status = luma::draw::adoptTarget(pMod, pWhere, pTarget, w, h, &texture); status != LUMA_OK) {
                        return status;  // reported, the target deleted
                    }
                    View& rView = sViews[slot];
                    rView = View();
                    rView.handle = ++sNextHandle;
                    rView.mod = pMod;
                    rView.texture = texture;
                    rView.width = w;
                    rView.height = h;
                    rView.clear = clear;
                    rView.on = true;
                    rView.fovY = 45.0f;
                    if (slot >= sHigh) {
                        sHigh = slot + 1;
                    }
                    sLive++;
                    *pOut = rView.handle;
                    return LUMA_OK;
                }

                int32_t svcCamera(const LumaMod* pMod, LumaDrawView view, const LumaDrawVec3* pEye, const LumaDrawVec3* pAt,
                                  const LumaDrawVec3* pUp, float fovY) {
                    const char* const pWhere = "draw.view.camera";
                    View* pView = nullptr;
                    if (const s32 status = enterView(pMod, pWhere, view, &pView); status != LUMA_OK) {
                        return status;
                    }
                    if (!finite3(pEye) || !finite3(pAt) || !finite3(pUp)) {
                        return badArgument(pMod, pWhere, !finite3(pEye) ? "eye" : !finite3(pAt) ? "at" : "up");
                    }
                    if (!fovOk(fovY)) {
                        return badArgument(pMod, pWhere, "fov_y");
                    }
                    Mtx camera;
                    if (!lookAt(camera, Vec3f(pEye->x, pEye->y, pEye->z), Vec3f(pAt->x, pAt->y, pAt->z),
                                Vec3f(pUp->x, pUp->y, pUp->z))) {
                        return badArgument(pMod, pWhere, "up");
                    }
                    NebulaMtxCopy(camera, pView->view);
                    pView->fovY = fovY;
                    pView->follow = nullptr;
                    pView->nearZ = 0.0f;
                    pView->hasCamera = true;
                    return LUMA_OK;
                }

                int32_t svcFollow(const LumaMod* pMod, LumaDrawView view, LumaActor actor, const LumaDrawVec3* pOffset,
                                  float fovY) {
                    const char* const pWhere = "draw.view.follow";
                    View* pView = nullptr;
                    if (const s32 status = enterView(pMod, pWhere, view, &pView); status != LUMA_OK) {
                        return status;
                    }
                    LiveActor* const pActor = static_cast< LiveActor* >(actor);
                    if (pActor == nullptr || !lumacore::isA(lumacore::classOf(pActor), lumacore::kClassId< LiveActor >) ||
                        MR::isDead(pActor)) {
                        return badArgument(pMod, pWhere, "actor");
                    }
                    if (!finite3(pOffset) || !(pOffset->x * pOffset->x + pOffset->y * pOffset->y + pOffset->z * pOffset->z > 1e-6f)) {
                        return badArgument(pMod, pWhere, "offset");
                    }
                    if (!fovOk(fovY)) {
                        return badArgument(pMod, pWhere, "fov_y");
                    }
                    pView->follow = pActor;
                    pView->rig = LUMA_DRAW_VIEW_RIG_FOLLOW;
                    pView->offset = Vec3f(pOffset->x, pOffset->y, pOffset->z);
                    pView->joint[0] = '\0';
                    pView->nearZ = 0.0f;
                    pView->fovY = fovY;
                    (void)followNow(*pView);  // the camera from now (an eye over its up: the next frame's)
                    return LUMA_OK;
                }

                bool finiteArray(const float* pValues) {
                    return finite(pValues[0]) && finite(pValues[1]) && finite(pValues[2]);
                }

                // A rig checked: null, or the name of the field out of range (reported by the caller). A dead actor is
                // `*pDead` (not a mistake: it ended), checked last.
                const char* rigFault(const LumaDrawViewCamera* pCamera, bool* pDead) {
                    *pDead = false;
                    if (pCamera == nullptr) {
                        return "camera";
                    }
                    if (pCamera->rig < LUMA_DRAW_VIEW_RIG_FIXED || pCamera->rig > LUMA_DRAW_VIEW_RIG_WORLD) {
                        return "rig";
                    }
                    if (!fovOk(pCamera->fov_y)) {
                        return "fov_y";
                    }
                    if (pCamera->rig == LUMA_DRAW_VIEW_RIG_FIXED) {
                        if (!finiteArray(pCamera->eye) || !finiteArray(pCamera->at) || !finiteArray(pCamera->up)) {
                            return !finiteArray(pCamera->eye) ? "eye" : !finiteArray(pCamera->at) ? "at" : "up";
                        }
                        Mtx camera;
                        if (!lookAt(camera, Vec3f(pCamera->eye[0], pCamera->eye[1], pCamera->eye[2]),
                                    Vec3f(pCamera->at[0], pCamera->at[1], pCamera->at[2]),
                                    Vec3f(pCamera->up[0], pCamera->up[1], pCamera->up[2]))) {
                            return "up";
                        }
                        return nullptr;
                    }
                    LiveActor* const pActor = static_cast< LiveActor* >(pCamera->actor);
                    if (pActor == nullptr || !lumacore::isA(lumacore::classOf(pActor), lumacore::kClassId< LiveActor >)) {
                        return "actor";
                    }
                    if (pCamera->rig == LUMA_DRAW_VIEW_RIG_JOINT) {
                        if (memchr(pCamera->joint, '\0', sizeof(pCamera->joint)) == nullptr || pCamera->joint[0] == '\0') {
                            return "joint";
                        }
                        if (!finite(pCamera->near_z) || !(pCamera->near_z > 0.0f)) {
                            return "near_z";
                        }
                    } else {
                        const float* const pOffset = pCamera->offset;
                        if (!finiteArray(pOffset) ||
                            !(pOffset[0] * pOffset[0] + pOffset[1] * pOffset[1] + pOffset[2] * pOffset[2] > 1e-6f)) {
                            return "offset";
                        }
                    }
                    if (MR::isDead(pActor)) {
                        *pDead = true;
                        return nullptr;
                    }
                    if (pCamera->rig == LUMA_DRAW_VIEW_RIG_JOINT && !hasJoint(pActor, pCamera->joint)) {
                        return "joint";
                    }
                    return nullptr;
                }

                int32_t svcRig(const LumaMod* pMod, LumaDrawView view, const LumaDrawViewCamera* pCamera) {
                    const char* const pWhere = "draw.view.rig";
                    View* pView = nullptr;
                    if (const s32 status = enterView(pMod, pWhere, view, &pView); status != LUMA_OK) {
                        return status;
                    }
                    bool dead = false;
                    if (const char* const pFault = rigFault(pCamera, &dead); pFault != nullptr) {
                        return badArgument(pMod, pWhere, pFault);
                    }
                    if (dead) {
                        return badArgument(pMod, pWhere, "actor");  // as follow: a view never starts on a dead actor
                    }
                    pView->fovY = pCamera->fov_y;
                    if (pCamera->rig == LUMA_DRAW_VIEW_RIG_FIXED) {
                        (void)lookAt(pView->view, Vec3f(pCamera->eye[0], pCamera->eye[1], pCamera->eye[2]),
                                     Vec3f(pCamera->at[0], pCamera->at[1], pCamera->at[2]),
                                     Vec3f(pCamera->up[0], pCamera->up[1], pCamera->up[2]));  // checked in rigFault
                        pView->follow = nullptr;
                        pView->nearZ = 0.0f;
                        pView->hasCamera = true;
                        return LUMA_OK;
                    }
                    pView->follow = static_cast< LiveActor* >(pCamera->actor);
                    pView->rig = static_cast< u32 >(pCamera->rig);
                    pView->offset = Vec3f(pCamera->offset[0], pCamera->offset[1], pCamera->offset[2]);
                    memcpy(pView->joint, pCamera->joint, sizeof(pView->joint));  // NUL inside, checked
                    pView->nearZ = pCamera->rig == LUMA_DRAW_VIEW_RIG_JOINT ? pCamera->near_z : 0.0f;
                    (void)followNow(*pView);
                    return LUMA_OK;
                }

                int32_t svcEyeOf(const LumaMod* pMod, const LumaDrawViewCamera* pCamera, LumaDrawViewEye* pOut) {
                    const char* const pWhere = "draw.view.eye_of";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    if (pOut == nullptr) {
                        return badArgument(pMod, pWhere, "out");
                    }
                    bool dead = false;
                    if (const char* const pFault = rigFault(pCamera, &dead); pFault != nullptr) {
                        return badArgument(pMod, pWhere, pFault);
                    }
                    if (dead) {
                        return LUMA_E_STATE;
                    }
                    Vec3f eye, at, up;
                    if (pCamera->rig == LUMA_DRAW_VIEW_RIG_FIXED) {
                        eye.set(pCamera->eye[0], pCamera->eye[1], pCamera->eye[2]);
                        at.set(pCamera->at[0], pCamera->at[1], pCamera->at[2]);
                        up.set(pCamera->up[0], pCamera->up[1], pCamera->up[2]);
                    } else {
                        (void)actorEye(static_cast< LiveActor* >(pCamera->actor), static_cast< u32 >(pCamera->rig),
                                       Vec3f(pCamera->offset[0], pCamera->offset[1], pCamera->offset[2]), pCamera->joint, &eye,
                                       &at, &up);  // the joint checked in rigFault
                    }
                    pOut->eye[0] = eye.x;
                    pOut->eye[1] = eye.y;
                    pOut->eye[2] = eye.z;
                    pOut->at[0] = at.x;
                    pOut->at[1] = at.y;
                    pOut->at[2] = at.z;
                    pOut->up[0] = up.x;
                    pOut->up[1] = up.y;
                    pOut->up[2] = up.z;
                    pOut->fov_y = pCamera->fov_y;
                    return LUMA_OK;
                }

                int32_t svcTexture(const LumaMod* pMod, LumaDrawView view, LumaDrawTexture* pOut) {
                    const char* const pWhere = "draw.view.texture";
                    View* pView = nullptr;
                    if (const s32 status = enterView(pMod, pWhere, view, &pView); status != LUMA_OK) {
                        return status;
                    }
                    if (pOut == nullptr) {
                        return badArgument(pMod, pWhere, "out");
                    }
                    *pOut = pView->texture;
                    return LUMA_OK;
                }

                int32_t svcSetOn(const LumaMod* pMod, LumaDrawView view, int32_t on) {
                    View* pView = nullptr;
                    if (const s32 status = enterView(pMod, "draw.view.set_on", view, &pView); status != LUMA_OK) {
                        return status;
                    }
                    pView->on = on != 0;
                    return LUMA_OK;
                }

                int32_t svcRelease(const LumaMod* pMod, LumaDrawView view) {
                    View* pView = nullptr;
                    if (const s32 status = enterView(pMod, "draw.view.release", view, &pView); status != LUMA_OK) {
                        return status;
                    }
                    end(*pView, "released");
                    return LUMA_OK;
                }

                const LumaDrawViewService cTable = {
                    {sizeof(LumaDrawViewService), LUMA_DRAW_VIEW_MAJOR, LUMA_DRAW_VIEW_MINOR},
                    svcCreate,
                    svcCamera,
                    svcFollow,
                    svcTexture,
                    svcSetOn,
                    svcRelease,
                    svcRig,
                    svcEyeOf,
                };

                // ---- The provider ----

                void modGone(void* pUser, const LumaMod* pMod) {
                    (void)pUser;
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sViews[i].handle != 0 && sViews[i].mod == pMod) {
                            end(sViews[i], "mod");
                        }
                    }
                }

                void registered(void* pUser, const LumaServiceKernel* pKernel) {
                    (void)pUser;
                    sKernel = pKernel;
                }

                // No rows of its own: the registry's (101 to 104) say everything it refuses.
                const LumaServiceProvider cProvider = {
                    sizeof(LumaServiceProvider),
                    LUMA_DRAW_VIEW_NAME,
                    &cTable.base,
                    nullptr,
                    nullptr,
                    0,
                    nullptr,
                    nullptr,
                    modGone,
                    nullptr,
                    registered,
                };

                bool inRange(const void* p, const void* pStart, const void* pEnd) {
                    return p >= pStart && p < pEnd;
                }
            }  // namespace

            void render() {
                if (sLive == 0) {
                    return;
                }
                const bool facts = luma::modlog::facts();
                for (u32 i = 0; i < sHigh; i++) {
                    View& rView = sViews[i];
                    if (rView.handle == 0) {
                        continue;
                    }
                    if (rView.follow != nullptr) {
                        (void)followNow(rView);
                    }
                    GameTexture* const pTarget = luma::draw::targetOf(rView.mod, rView.texture);
                    const bool drawn = rView.on && rView.hasCamera && pTarget != nullptr && sKernel->mod_live(rView.mod) != 0;
                    if (drawn) {
                        drawView(rView, pTarget);
                        rView.drawn++;
                    }
                    if (facts && rView.facts < kFactFrames) {
                        rView.facts++;
                        viewFact(rView, drawn);
                    }
                }
            }

            void released(const void* pStart, const void* pEnd) {
                if (sLive == 0) {
                    return;
                }
                for (u32 i = 0; i < sHigh; i++) {
                    if (sViews[i].handle != 0 && sViews[i].follow != nullptr && inRange(sViews[i].follow, pStart, pEnd)) {
                        sViews[i].follow = nullptr;
                    }
                }
            }

            const LumaServiceProvider* provider() {
                return &cProvider;
            }
        }  // namespace view
    }  // namespace draw
}  // namespace luma
