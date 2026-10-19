#ifndef JPA_EMITTER_INLINE_HPP
#define JPA_EMITTER_INLINE_HPP

inline JPABaseEmitter::JPABaseEmitter() : mLink(this), mRndm(0) {
#ifdef TARGET_PC
    mMotionEpoch = 0;
#endif
}

inline JPABaseEmitter::~JPABaseEmitter() {
}

#endif
