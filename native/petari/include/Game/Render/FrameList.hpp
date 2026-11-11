#pragma once

#include <NebulaGpu.h>
#include <nebula_frame_list.h>
#include <nebula_game_api.h>
#include <nebula_math.h>
#include <nebula_model.h>

class ModelData;

// A draw's fixed state as the game writes it at a site: one value, whole, with the widths its lines and points take.
struct PassState {
    u8 blend_op, blend_src, blend_dst;
    u8 depth_test, depth_func, depth_write;
    u8 cull;
    u8 color_write, alpha_write;  // alpha_write 2: the pass's
    u8 stencil_func, stencil_op;  // stencil_func 0xFF: the pass's
    f32 line_width, point_size;   // lines and points in lines of a 456-line picture
    bool spread_lines, spread_points;  // a widened line's or point's texture spreads by a texel across it
    bool pixel_shading;  // the dynamic alpha test is proved uniformly passing; MSAA still covers geometry

    u32 word() const;
};

namespace PassStates {
    // Replace, depth off, no culling, colour and the pass's alpha.
    PassState plain();
    // Blending by these factors (one, zero: replace), depth as told, culling, the channels written.
    PassState use(u8 src, u8 dst, bool depthTest, u8 depthFunc, bool depthWrite, u8 cull, bool colorWrite, u8 alphaWrite);
    PassState fromWord(u32 state);
    void setDepth(PassState& rState, bool test, u8 func, bool write);
    void setBlend(PassState& rState, u8 src, u8 dst);
    void setNoBlend(PassState& rState);
}  // namespace PassStates

enum LightFalloff {
    LIGHT_FALLOFF_NONE = 0,
    LIGHT_FALLOFF_GENTLE,
    LIGHT_FALLOFF_MEDIUM,
    LIGHT_FALLOFF_STEEP,
};

// A light's values from what the game knows of it.
namespace LightValues {
    void color(NebulaLight* pLight, NebulaColor8 color);
    void pos(NebulaLight* pLight, f32 x, f32 y, f32 z);
    void dir(NebulaLight* pLight, f32 nx, f32 ny, f32 nz);
    // A specular light: its direction as a light at infinity, with the half-angle vector (for a viewer down -Z, or given).
    void specularDir(NebulaLight* pLight, f32 nx, f32 ny, f32 nz);
    void specularDirHA(NebulaLight* pLight, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz);
    void attn(NebulaLight* pLight, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2);
    void noSpot(NebulaLight* pLight);
    void distAttn(NebulaLight* pLight, f32 refDistance, f32 refBrightness, LightFalloff falloff);
}  // namespace LightValues

// The list of what this frame draws, being written. Everything here is the game thread's; `end` hands it to the renderer
// and the next frame writes another.
namespace Frame {
    // What the first frame starts from.
    void initDefaults();
    u32 number();
    void begin();
    void end();

    // Memory for this frame's list, kept until the renderer lets the list go.
    void* arena(u32 bytes, u32 align);
    template < class T >
    inline T* alloc(u32 count) {
        return static_cast< T* >(arena(count * static_cast< u32 >(sizeof(T)), alignof(T)));
    }

    // The next item of the draw pass open, or of a new one. Its fields are the caller's to fill, all of them.
    NebulaFrameItem& addItem();
    NebulaFrameItem& itemAt(u32 index);
    u32 itemCount();
    u32 addConstants(u32 slot, u32 vertexRows, u32 pixelRows, const f32 (*pRows)[4], const u32* pCoordRows, u32 coordCount);
    u32 addBlock(const NebulaFrameBlock& rBlock);
    u32 camera(const f32 (*pView)[4], u32 role);

    // How draws project and where they land now.
    u32 currentView();
    void setPerspective(const f32 (*pMtx)[4]);
    void setOrthographic(const f32 (*pMtx)[4]);
    void setProjectionv(const f32* pValues);
    void getProjectionv(f32* pValues);
    void setViewport(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ);
    void getViewportv(f32* pValues);
    void setZScaleOffset(f32 scale, f32 offset);
    void setScissor(f32 left, f32 top, f32 width, f32 height);
    void getScissor(f32* pLeft, f32* pTop, f32* pWidth, f32* pHeight);
    // A projection and viewport as the renderer's values (six terms, six viewport values).
    void hostView(const f32 (*pMtx)[4], f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ, f32* pOut);

    // The lights draws take now.
    u32 currentLightSet(u32 block);
    void setLight(u32 index, const NebulaLight& rLight);
    void setLightFollowsCamera(u32 mask);
    void setLightFollows(u32 index, bool follows);
    // A light of the world, into this frame's list (NebulaFrameWorldLight): one entry an id, the newest values standing.
    void worldLight(const NebulaFrameWorldLight& rLight);
    // Whose lights the sets made from now on hold: the key and fill of an area's light record (ids pKey->id and
    // pKey->id + 1) with its ambient, or none (null: lights that are no light of the world).
    void setLightOwner(const NebulaFrameWorldLight* pKey, const NebulaFrameWorldLight* pFill, const f32* pAmbient);
    // The world light that is light 4 of the sets made from now on, or null.
    void setLightPointOwner(const NebulaFrameWorldLight* pPoint);
    // The light of the stage's own surfaces this frame (key `id`, fill `id` + 1) and its ambient.
    void setStageLight(u64 id, const f32* pAmbient);
    // The stage being played (its name, or null for none): the list's stage_hash from this frame on; the host hears
    // of each change (game_stage).
    void setStage(const char* pName);
    // A drop shadow the game draws this frame (NebulaFrameShadowDrop), into this frame's list.
    void shadowDrop(const NebulaFrameShadowDrop& rDrop);
    // A point light an actor asks for this frame (NebulaFramePointLight), into this frame's list.
    void pointLight(const NebulaFramePointLight& rLight);
    // A clip area's volume as drawn this frame (NebulaFrameClipVolume), and the rule the parts it clips show by
    // (NEBULA_CLIP_RULE_*), into this frame's list: the ray tracer takes those parts where they show alone.
    void clipVolume(const NebulaFrameClipVolume& rVolume);
    void clipRule(u32 rule);

    // A state word with what the pass decides: its alpha and its stencil.
    u32 finishState(u32 state);
    void setPassAlpha(bool write);
    void setPassStencil(u8 test, u8 op);

    // Room for `count` layout inputs that lasts until the next call with the same `which` (cLayoutRoomBuild: the pipeline
    // being made; cLayoutRoomWiden: a widened draw's layout). Any count.
    enum { cLayoutRoomBuild = 0, cLayoutRoomWiden = 1, cLayoutRooms = 2 };
    NebulaLayoutInput* layoutRoom(u32 which, u32 count);

    // A pipeline of a written shader for these fixed values and this vertex layout.
    u32 sitePipeline(u32 shader, u32 state, const NebulaModelMesh* pMesh);
    // A cooked form's programs over a site's vertices: inputs the site has none of read zero.
    u32 formPipeline(u64 vertex, u64 pixel, const u8* pFormInputs, u32 formInputCount, u32 state, const NebulaLayoutInput* pInputs, u32 inputCount, u32 stride);
    u32 sitePipeline(u32 shader, u32 state, const NebulaLayoutInput* pInputs, u32 inputCount, u32 stride, bool wide);
    // The particle material over instanced particles (NEBULA_ITEM_INSTANCES).
    u32 particlePipeline(u32 state);
    // The font material over CharWriter's instanced glyphs.
    u32 glyphPipeline(u32 state);

    bool pixelLit();
    void notDrawn(const ModelData* pData, u32 material, u32 replace, u32 pass);

    // Whose drawing follows, from frame to frame.
    void setObject(u32 slot, u32 generation);
    u32 objectSlot();
    u32 objectGeneration();
    u32 nextPart();
    // The number the object's next draw takes. A drawer whose parts come and go (a strip skipped, a piece that draws only
    // some of the time) gives each part a number of its own, so a part is paired with itself from frame to frame and never
    // with the part that drew in its place.
    void setPart(u32 part);
    u32 newSlot();

    // Passes, in the order the game draws them. Keys name textures as RenderTexture does.
    void targetBegin(const void* pKey, f32 across, f32 down, u32 clearRgba);
    void targetBeginPixels(const void* pKey, u32 width, u32 height, u32 clearRgba);
    void targetBeginOverFrame(const void* pKey, f32 across, f32 down, u32 clearRgba, const void* pFrameKey,
                              NebulaTargetChannels channels, const void* pDepthKey);
    void targetEnd(const void* pKey, NebulaTargetChannels channels = NEBULA_TARGET_RGBA);
    void clear(u32 clearRgba, u32 channels);
    void clearFrame(u32 clearRgba, u32 channels);
    // Every pipeline the cook listed, for the host to prepare ahead of play.
    void registerPipelineList();
    void copyFrame(const void* pKey);
    void takeFrame(const void* pKey);
    void show();
    // A new asker of the picture's depth: it reads only answers to the probes it sent, never ones another asker on its
    // channel sent (the last scene's).
    u32 newProbeAsker();
    // `compared`: the asker only compares each value with its user_z (NEBULA_PROBE_COMPARED).
    void probeScreen(u32 channel, u32 asker, const NebulaScreenProbe* pProbes, u32 count, bool compared = false);
    u32 screenProbeResults(u32 channel, u32 asker, NebulaScreenProbe* pProbes, u32 capacity);

    void sceneBegin(const f32 (*pView)[4]);  // rows of the camera, then the player's up and 1, or zeros
    void opaqueEnd(const f32 (*pView)[4]);
    void sceneEnd();
    void scene2D(bool begin);
    void mirror(bool begin);
    void phase(u8 phase);
    // The draws from here on are of this category (NEBULA_CATEGORY_*; 0 none): what the game draws them as.
    void category(u8 category);
    // The draws from here on (until tracedOnly(false)) are the ray tracer's alone: things that stop its light, never
    // drawn (NEBULA_PASS_TRACED_DRAW). Only while ray tracing is on.
    void tracedOnly(bool on);
    void bloom(u32 kind, u32 arg0, u32 arg1);
    void cameraCut(u32 cuts);
}  // namespace Frame
