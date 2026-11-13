#pragma once

#include <NebulaGpu.h>
#include <nebula_clip.h>
#include <nebula_math.h>

class ModelData;

// A joint's place in its parent.
struct TransformInfo {
    Vec mScale;
    Vec mRotation;
    Vec mTranslate;
};

// A texture matrix's animated part.
struct TextureTransform {
    f32 mScaleX;
    f32 mScaleY;
    f32 mRotation;
    f32 mTranslationX;
    f32 mTranslationY;
};

// An animation from the store (nebula_clip.h), and where its player has it. The file stays where it was loaded; one Clip
// a file. Material clips name materials by name, bound to a model's numbers with `bind`.
class Clip {
public:
    static Clip* load(const void* pFile);

    Clip(const NebulaClipHeader* pFile);

    u8 getAttribute() const {
        return mAttribute;
    }
    s32 getFrameMax() const {
        return mFrameMax;
    }
    f32 getFrame() const {
        return mFrame;
    }
    void setFrame(f32 frame) {
        mFrame = frame;
    }
    u8 getKind() const {
        return mFile->kind;
    }
    void bind(const ModelData* pModelData);

    // Joint clips. A joint past the clip's last has no tracks (an extra joint of a model that replaces a game model
    // plays no game animation, models contract 4): false, and *pOut is left as it was (it then takes the joint's rest).
    bool getTransform(u32 joint, TransformInfo* pOut) const;

    // Texture matrix clips, entry by entry.
    u32 getEntryCount() const {
        return static_cast< u32 >(mFile->first_count);
    }
    void getTextureTransform(u32 entry, TextureTransform* pOut) const;
    u8 getCoordMode() const {
        return mFile->coord_mode;
    }
    const f32* getCenter(u32 entry) const {
        return targets()[entry].center;
    }

    // Texture matrix (its number), colour register or texture map the entry changes.
    u8 getSlot(u32 entry) const {
        return targets()[entry].slot;
    }

    // Colour clips.
    void getColor(u32 entry, NebulaColor8* pOut) const;

    // Register clips: entries of the colour registers, then those of the constant ones.
    u32 getRegisterCount() const {
        return static_cast< u32 >(mFile->first_count);
    }
    u32 getConstantCount() const {
        return static_cast< u32 >(mFile->second_count);
    }
    void getRegister(u32 entry, NebulaColor16* pOut) const;
    void getConstant(u32 entry, NebulaColor8* pOut) const;
    u32 getConstantMaterial(u32 entry) const {
        return mBound[mFile->first_count + entry];
    }
    u8 getConstantSlot(u32 entry) const {
        return targets()[mFile->first_count + entry].slot;
    }

    // Pattern clips.
    void getTexture(u32 entry, u32* pOut) const;

    // Visibility clips: one per mesh.
    void getVisibility(u32 entry, u8* pOut) const;

    // The material of a material clip's entry in the model bound, NEBULA_NONE32 for none.
    u32 getMaterial(u32 entry) const {
        return mBound[entry];
    }
    bool hasMaterial(u32 entry) const {
        return mBound[entry] != NEBULA_NONE32;
    }

    // The joints a transform clip holds tracks for (nine a joint).
    u32 jointCapacity() const {
        return (mFile->targets_at - mFile->tracks_at) / (sizeof(NebulaClipTrack) * 9u);
    }

    // Where a player has it.
    f32 mFrame;
    s32 mFrameMax;
    u8 mAttribute;

private:
    const NebulaClipTrack* tracks() const {
        return reinterpret_cast< const NebulaClipTrack* >(reinterpret_cast< const u8* >(mFile) + mFile->tracks_at);
    }
    const NebulaClipTarget* targets() const {
        return reinterpret_cast< const NebulaClipTarget* >(reinterpret_cast< const u8* >(mFile) + mFile->targets_at);
    }
    const f32* values() const {
        return reinterpret_cast< const f32* >(reinterpret_cast< const u8* >(mFile) + mFile->values_at);
    }
    u32 targetCount() const;
    f32 value(const NebulaClipTrack& rTrack, f32 rest) const;
    u8 colorByte(const NebulaClipTrack& rTrack) const;
    f32 rotation(const NebulaClipTrack& rTrack) const;

    const NebulaClipHeader* mFile;
    u32* mBound;
};

// A player's frame: how fast, between which frames, how it ends.
class FrameCtrl {
public:
    enum Attribute {
        EMode_NONE,
        EMode_RESET,
        EMode_LOOP,
        EMode_REVERSE,
        EMode_LOOP_REVERSE,
    };

    FrameCtrl() {
        init(0);
    }
    FrameCtrl(s32 endFrame) {
        init(endFrame);
    }
    void init(s32 endFrame);
    BOOL checkPass(f32 passFrame);
    void update();

    u8 getAttribute() const {
        return mAttribute;
    }
    void setAttribute(u8 attribute) {
        mAttribute = attribute;
    }
    u8 getState() const {
        return mState;
    }
    bool checkState(u8 state) const {
        return (mState & state) != 0;
    }
    s32 getStart() const {
        return mStart;
    }
    void setStart(s32 start) {
        mStart = start;
        mFrame = start;
    }
    s32 getEnd() const {
        return mEnd;
    }
    void setEnd(s32 end) {
        mEnd = end;
    }
    s32 getLoop() const {
        return mLoop;
    }
    void setLoop(s32 loop) {
        mLoop = loop;
    }
    f32 getRate() const {
        return mRate;
    }
    void setRate(f32 rate) {
        mRate = rate;
    }
    f32 getFrame() const {
        return mFrame;
    }
    void setFrame(f32 frame) {
        mFrame = frame;
    }
    void reset() {
        mFrame = mStart;
        mRate = 1.0f;
        mState = 0;
    }

    u8 mAttribute;
    u8 mState;
    s32 mStart;
    s32 mEnd;
    s32 mLoop;
    f32 mRate;
    f32 mFrame;
};
