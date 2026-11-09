#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/Util/ByteView.hpp"

AudChordInfo::AudChordInfo()
    : SoundGlobal< AudChordInfo >(true), mArchive(nullptr), mAvailable(false), mChordIndex(0), mScaleIndex(0), mSerial(0) {
}

void AudChordInfo::init(AssetFolder* pArchive) {
    mArchive = pArchive;
}

s32 AudChordInfo::chordIndex() const {
    return SoundEngine::get()->applied() >= mSerial ? static_cast< s32 >(SoundEngine::get()->rhythm().mChordIndex) : mChordIndex;
}

s32 AudChordInfo::scaleIndex() const {
    return SoundEngine::get()->applied() >= mSerial ? static_cast< s32 >(SoundEngine::get()->rhythm().mScaleIndex) : mScaleIndex;
}

// What ChordInfo::load will find, worked out here so the game sees it at once: a chord and scale only when the table is one and the
// numbers it starts at are in it; a number not in it stays as it was.
bool AudChordInfo::loadChordInfo(s16 id, bool keepCurrent) {
    const u8* pFile = id >= 0 ? static_cast< const u8* >(mArchive->getResource(static_cast< u16 >(id))) : nullptr;
    const bool valid = pFile != nullptr && pFile[4] == 'C' && pFile[5] == 'I' && pFile[6] == 'T' && pFile[7] == 'S';

    s32 chord = chordIndex();
    s32 scale = scaleIndex();
    bool available = false;
    if (valid) {
        const u16 chords = readBytes< u16 >(pFile + 12);
        const u16 scales = readBytes< u16 >(pFile + 14);
        const s32 wantChord = keepCurrent ? chord : 0;
        const s32 wantScale = keepCurrent ? scale : 0;
        const bool hasChord = wantChord < chords;
        const bool hasScale = wantScale < scales;
        if (hasChord) {
            chord = wantChord;
        }
        if (hasScale) {
            scale = wantScale;
        }
        available = hasChord && hasScale;
    }

    SoundRing& rRing = SoundEngine::get()->ring();
    SoundCommand& rCommand = rRing.next(SOUND_CHORD_LOAD, nullptr);
    rCommand.mArg = keepCurrent ? 1 : 0;
    rCommand.payload< const void* >() = valid ? pFile : nullptr;

    mSerial = rRing.send();
    mAvailable = available;
    mChordIndex = chord;
    mScaleIndex = scale;
    return valid;
}

void AudChordInfo::invalidate() {
    const s32 chord = chordIndex();
    const s32 scale = scaleIndex();

    SoundRing& rRing = SoundEngine::get()->ring();
    rRing.next(SOUND_CHORD_INVALIDATE, nullptr);

    mSerial = rRing.send();
    mAvailable = false;
    mChordIndex = chord;
    mScaleIndex = scale;
}

bool AudChordInfo::isAvailable() const {
    return SoundEngine::get()->applied() >= mSerial ? static_cast< bool >(SoundEngine::get()->rhythm().mChordAvailable) : mAvailable;
}
