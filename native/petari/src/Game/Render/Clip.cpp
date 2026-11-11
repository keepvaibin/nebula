#include "Game/Render/Clip.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_geometry.hpp>
#include <nebula_module.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_value_f1, "value.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_colorByte_f1, "colorByte.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPass_f1, "checkPass.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPass_f2, "checkPass.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.001f);

Clip* Clip::load(const void* pFile) {
    if (pFile == nullptr) {
        return nullptr;
    }
    const auto* pHeader = static_cast< const NebulaClipHeader* >(pFile);
    if (pHeader->magic != NEBULA_CLIP_MAGIC || pHeader->version != NEBULA_CLIP_VERSION) {
        return nullptr;
    }
    return new Clip(pHeader);
}

// The bindings live as long as the clip (in its holder's arena): a clip is bound again for each scene's player model.
Clip::Clip(const NebulaClipHeader* pFile) : mFile(pFile), mBound(nullptr) {
    mFrame = 0.0f;
    mFrameMax = pFile->frames;
    mAttribute = pFile->loop_mode;
    const u32 count = targetCount();
    if (count != 0) {
        mBound = new u32[count];
    }
}

u32 Clip::targetCount() const {
    return (mFile->values_at - mFile->targets_at) / sizeof(NebulaClipTarget);
}

void Clip::bind(const ModelData* pModelData) {
    const u32 count = targetCount();
    const NebulaClipTarget* pTargets = targets();
    for (u32 i = 0; i < count; i++) {
        const char* pName = reinterpret_cast< const char* >(mFile) + pTargets[i].name_at;
        const s32 index = pModelData->findMaterial(pName);
        if (index < 0) {
            // A target the model does not have is skipped (the original engine binds by name and skips the same),
            // but never silently: a model posed or placed wrong names its broken binding here.
            nebula_log("Clip: target \"%s\" is not in the model; its tracks stay still\n", pName);
        }
        mBound[i] = index >= 0 ? static_cast< u32 >(index) : NEBULA_NONE32;
    }
}

namespace {
    // Keys are time, value, out tangent, then either in tangent of the next (stride 3: one tangent a key) or its own
    // pair (stride 4). A whole-number track rounds differently only in the formula's order of operations.
    f32 integerHermite(f32 frame, f32 a, f32 b, f32 c, f32 d, f32 e, f32 f) {
        const f32 duration = d - a;
        const f32 t = (frame - a) / duration;
        const f32 delta = (e - b) - duration * c;
        const f32 squared = t * t;
        f32 value = ((f * duration + b) - e) - delta;
        const f32 end = squared * value;
        value = duration * c + end;
        value = value * t + b;
        value = delta * squared + value;
        return value - end;
    }

    f32 evaluateKeys(const NebulaClipTrack& rTrack, const f32* pKeys, f32 frame) {
        if (frame < pKeys[0]) {
            return pKeys[1];
        }
        const u32 stride = rTrack.stride;
        const u32 last = (rTrack.count - 1u) * stride;
        if (pKeys[last] <= frame) {
            return pKeys[last + 1];
        }
        u32 span = rTrack.count;
        while (span > 1) {
            const u32 half = span >> 1;
            const u32 at = half * stride;
            if (frame >= pKeys[at]) {
                pKeys += at;
                span -= half;
            } else {
                span = half;
            }
        }
        const f32 tangent = stride == 3 ? pKeys[2] : pKeys[3];
        const f32 nextTime = pKeys[stride];
        const f32 nextValue = pKeys[stride + 1];
        const f32 nextTangent = pKeys[stride + 2];
        if (rTrack.integer_keys != 0) {
            return integerHermite(frame, pKeys[0], pKeys[1], tangent, nextTime, nextValue, nextTangent);
        }
        return hermiteInterpolation(frame, pKeys[0], pKeys[1], tangent, nextTime, nextValue, nextTangent);
    }
}  // namespace

f32 Clip::value(const NebulaClipTrack& rTrack, f32 rest) const {
    if (rTrack.count == 0) {
        return rest;
    }
    const f32* pValues = values() + rTrack.at;
    if (rTrack.count == 1) {
        return pValues[0];
    }
    switch (rTrack.stride) {
    case NEBULA_TRACK_STEP:
        if (mFrame < 0.0f) {
            return pValues[0];
        }
        return mFrame >= rTrack.count ? pValues[rTrack.count - 1] : pValues[static_cast< int >(mFrame)];
    case NEBULA_TRACK_STEP_NEAREST: {
        if (mFrame < 0.0f) {
            return pValues[0];
        }
        const u32 frame = static_cast< u32 >(mFrame + cNum_value_f1);
        return frame >= rTrack.count ? pValues[rTrack.count - 1] : pValues[frame];
    }
    default:
        return evaluateKeys(rTrack, pValues, mFrame);
    }
}

f32 Clip::rotation(const NebulaClipTrack& rTrack) const {
    return value(rTrack, 0.0f);
}

u8 Clip::colorByte(const NebulaClipTrack& rTrack) const {
    const f32 color = value(rTrack, 0.0f);
    if (rTrack.count < 2 || rTrack.stride < NEBULA_TRACK_KEYS) {
        return static_cast< u8 >(static_cast< int >(color));
    }
    if (color < 0.0f) {
        return 0;
    }
    return color > cNum_colorByte_f1 ? 255 : nebula_f32_to_u8(color);
}

void Clip::getTransform(u32 joint, TransformInfo* pOut) const {
    const NebulaClipTrack* p = tracks() + joint * 9u;
    pOut->mScale.x = value(p[0], 1.0f);
    pOut->mRotation.x = rotation(p[1]);
    pOut->mTranslate.x = value(p[2], 0.0f);
    pOut->mScale.y = value(p[3], 1.0f);
    pOut->mRotation.y = rotation(p[4]);
    pOut->mTranslate.y = value(p[5], 0.0f);
    pOut->mScale.z = value(p[6], 1.0f);
    pOut->mRotation.z = rotation(p[7]);
    pOut->mTranslate.z = value(p[8], 0.0f);
}

void Clip::getTextureTransform(u32 entry, TextureTransform* pOut) const {
    const NebulaClipTrack* p = tracks() + entry * 5u;
    pOut->mScaleX = value(p[0], 1.0f);
    pOut->mScaleY = value(p[1], 1.0f);
    pOut->mRotation = rotation(p[2]);
    pOut->mTranslationX = value(p[3], 0.0f);
    pOut->mTranslationY = value(p[4], 0.0f);
}

void Clip::getColor(u32 entry, NebulaColor8* pOut) const {
    const NebulaClipTrack* p = tracks() + entry * 4u;
    pOut->r = colorByte(p[0]);
    pOut->g = colorByte(p[1]);
    pOut->b = colorByte(p[2]);
    pOut->a = colorByte(p[3]);
}

void Clip::getRegister(u32 entry, NebulaColor16* pOut) const {
    const NebulaClipTrack* p = tracks() + entry * 4u;
    s16* pChannels[4] = {&pOut->r, &pOut->g, &pOut->b, &pOut->a};
    for (u32 c = 0; c < 4; c++) {
        const f32 color = value(p[c], 0.0f);
        if (p[c].count < 2) {
            *pChannels[c] = static_cast< s16 >(static_cast< int >(color));
        } else if (color < -0x400) {
            *pChannels[c] = -0x400;
        } else if (color > 0x3FF) {
            *pChannels[c] = 0x3FF;
        } else {
            *pChannels[c] = nebula_f32_to_s16(color);
        }
    }
}

void Clip::getConstant(u32 entry, NebulaColor8* pOut) const {
    const NebulaClipTrack* p = tracks() + (mFile->first_count + entry) * 4u;
    pOut->r = colorByte(p[0]);
    pOut->g = colorByte(p[1]);
    pOut->b = colorByte(p[2]);
    pOut->a = colorByte(p[3]);
}

void Clip::getTexture(u32 entry, u32* pOut) const {
    *pOut = nebula_clip_pattern_value(mFile, tracks() + entry, mFrame);
}

void Clip::getVisibility(u32 entry, u8* pOut) const {
    *pOut = static_cast< u8 >(value(tracks()[entry], 0.0f));
}

void FrameCtrl::init(s32 endFrame) {
    mAttribute = EMode_LOOP;
    mState = 0;
    mStart = 0;
    mEnd = endFrame;
    mLoop = 0;
    mRate = 1.0f;
    mFrame = 0.0f;
}

int FrameCtrl::checkPass(f32 passFrame) {
    f32 next_frame = mFrame + mRate;

    switch (mAttribute) {
    case 0:
    case 1:
        if (next_frame < mStart) {
            next_frame = mStart;
        }

        if (next_frame >= mEnd) {
            next_frame = mEnd - cNum_checkPass_f1;
        }

        if (mFrame <= next_frame) {
            if (mFrame <= passFrame && passFrame < next_frame) {
                return true;
            } else {
                return false;
            }
        }

        if (next_frame <= passFrame && passFrame < mFrame) {
            return true;
        }

        return false;
    case 2:
        if (mFrame < mStart) {
            while (next_frame < mStart) {
                if (mLoop - mStart <= 0.0f) {
                    break;
                }

                next_frame += mLoop - mStart;
            }

            if (next_frame <= passFrame && passFrame < mLoop) {
                return true;
            } else {
                return false;
            }
        } else if (mEnd <= mFrame) {
            while (next_frame >= mEnd) {
                if (mEnd - mLoop <= 0.0f) {
                    break;
                }

                next_frame -= mEnd - mLoop;
            }

            if (mLoop <= passFrame && passFrame < next_frame) {
                return true;
            } else {
                return false;
            }
        } else if (next_frame < mStart) {
            while (next_frame < mStart) {
                if (mLoop - mStart <= 0.0f) {
                    break;
                }

                next_frame += mLoop - mStart;
            }

            if ((mStart <= passFrame && passFrame < mFrame) || (next_frame <= passFrame && passFrame < mLoop)) {
                return true;
            } else {
                return false;
            }
        } else if (mEnd <= next_frame) {
            while (next_frame >= mEnd) {
                if (mEnd - mLoop <= 0.0f) {
                    break;
                }

                next_frame -= mEnd - mLoop;
            }

            if ((mFrame <= passFrame && passFrame < mEnd) || (mLoop <= passFrame && passFrame < next_frame)) {
                return true;
            } else {
                return false;
            }
        } else if (mFrame <= next_frame) {
            if (mFrame <= passFrame && passFrame < next_frame) {
                return true;
            } else {
                return false;
            }
        } else if (next_frame <= passFrame && passFrame < mFrame) {
            return true;
        }

        return false;
    case 3:
    case 4:
        if (next_frame >= mEnd) {
            next_frame = mEnd - cNum_checkPass_f2;
        }

        if (next_frame < mStart) {
            next_frame = mStart;
        }

        if (mFrame <= next_frame) {
            if (mFrame <= passFrame && passFrame < next_frame) {
                return true;
            } else {
                return false;
            }
        }

        if (next_frame <= passFrame && passFrame < mFrame) {
            return true;
        }

        return false;
    default:
        return false;
    }
}

void FrameCtrl::update() {
    mState = 0;
    mFrame += mRate;

    switch (mAttribute) {
    case EMode_NONE:
        if (mFrame < mStart) {
            mFrame = mStart;
            mRate = 0.0f;
            mState |= (u8)1;
        }

        if (mFrame >= mEnd) {
            mFrame = mEnd - cNum_update_f1;
            mRate = 0.0f;
            mState |= (u8)1;
        }

        break;
    case EMode_RESET:
        if (mFrame < mStart) {
            mFrame = mStart;
            mRate = 0.0f;
            mState |= (u8)1;
        }

        if (mFrame >= mEnd) {
            mFrame = mStart;
            mRate = 0.0f;
            mState |= (u8)1;
        }

        break;
    case EMode_LOOP:
        while (mFrame < mStart) {
            mState |= (u8)2;
            if (mLoop - mStart <= 0.0f) {
                break;
            }

            mFrame += mLoop - mStart;
        }

        while (mFrame >= mEnd) {
            mState |= (u8)2;
            if (mEnd - mLoop <= 0.0f) {
                break;
            }

            mFrame -= mEnd - mLoop;
        }

        break;
    case EMode_REVERSE:
        if (mFrame >= mEnd) {
            mFrame = mEnd - (mFrame - mEnd);
            mRate = -mRate;
        }

        if (mFrame < mStart) {
            mFrame = mStart - (mFrame - mStart);
            mRate = 0.0f;
            mState |= (u8)1;
        }

        break;
    case EMode_LOOP_REVERSE:
        if (mFrame >= mEnd - 1.0f) {
            mFrame = (mEnd - 1.0f) - (mFrame - (mEnd - 1.0f));
            mRate = -mRate;
        }

        if (mFrame < mStart) {
            mFrame = mStart - (mFrame - mStart);
            mRate = -mRate;
            mState |= (u8)2;
        }

        break;
    }
}
