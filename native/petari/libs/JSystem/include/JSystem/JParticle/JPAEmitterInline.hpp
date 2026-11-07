#ifndef JPA_EMITTER_INLINE_HPP
#define JPA_EMITTER_INLINE_HPP

inline EmitterCore::EmitterCore() : mLink(this), mpChunks(NULL), mpLastChunk(NULL), mRndm(0) {
    mDrawSlot = 0;
    mMotionEpoch = 0;
}

inline EmitterCore::~EmitterCore() {
}

#endif
