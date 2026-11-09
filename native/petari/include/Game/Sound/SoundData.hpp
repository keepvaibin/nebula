#pragma once

#include <nebula_types.h>

// The store's cooked sound data (crates/nebula-import/src/cook/sound.rs writes it): host order, fixed records, read in place.

// A sequence program: the authored program's bytes (its tables, envelopes and filters are read from them as authored, big-endian)
// and its operations decoded, in byte order, branch targets as operation indices. A file holds one program with one or more
// entry points (a sound effect collection: groups of entries).
struct SeqFile {
    static const u32 cMagic = 0x5145534e;  // "NSEQ"
    static const u32 cVersion = 2;

    u32 mMagic;
    u32 mVersion;
    u32 mOpCount;
    u32 mDataBytes;
    u32 mGroupCount;
    u32 mEntryCount;
    // Bounds over every entry, for the memory a player reserves: tracks alive at once, note slots of a track, the call and
    // loop entries a track holds at once, and the points of the longest envelope an operation sets.
    u32 mTrackBound;
    u32 mNoteSlots;
    u32 mStackBound;
    u32 mEnvelopePoints;
    u32 mReserved[2];
    // Then: u32 groupFirst[mGroupCount + 1] (into the entries), u32 entries[mEntryCount] (operation indices), u32 offsets[mOpCount]
    // (each operation's byte offset, ascending), SeqOp ops[mOpCount], u8 data[mDataBytes].
};
static_assert(sizeof(SeqFile) == 48, "NSEQ header");

struct SeqPoint {
    s16 mMode;
    s16 mTime;
    s16 mValue;
};
static_assert(sizeof(SeqPoint) == 6, "NSEQ point");

// Bit i of mRegisters: argument i names a register to read when the operation runs. A branch target read from a register is a
// byte offset; an immediate one an operation index.
struct SeqOp {
    static const u16 cRegisterForm = 0x8000;  // written with the register prefix

    u16 mCode;
    u16 mRegisters;
    u32 mArgs[5];
};
static_assert(sizeof(SeqOp) == 24, "NSEQ operation");

enum SeqCode : u16 {
    SEQ_NOTE_ON,        // slot, key, velocity
    SEQ_NOTE_GATE,      // flags (0x40 hold for the next, 0x80 sweep), key, velocity, length (0: until the note ends)
    SEQ_NOTE_OFF,       // slot
    SEQ_LAST_NOTE,      // key
    SEQ_PARAM_RAW,      // param, s16 value over 32767, time
    SEQ_PARAM_BYTE,     // param, byte value, time
    SEQ_OPEN_TRACK,     // child, target
    SEQ_CLOSE_TRACK,    // child
    SEQ_CALL,           // target
    SEQ_CALL_IF,        // condition, target
    SEQ_RETURN,
    SEQ_RETURN_IF,      // condition
    SEQ_JUMP,           // target
    SEQ_JUMP_IF,        // condition, target
    SEQ_JUMP_TABLE,     // index, table (byte offset of 24-bit byte offsets)
    SEQ_CALL_TABLE,     // index, table
    SEQ_LOOP_START,     // count
    SEQ_LOOP_END,
    SEQ_READ_PORT,      // port, register
    SEQ_WRITE_PORT,     // port, value
    SEQ_CHECK_IMPORT,   // port
    SEQ_CHECK_EXPORT,   // port
    SEQ_PARENT_WRITE_PORT,  // port, value
    SEQ_CHILD_WRITE_PORT,   // child << 4 | port, value
    SEQ_PARENT_READ_PORT,   // port, register
    SEQ_CHILD_READ_PORT,    // child << 4 | port, register
    SEQ_REG_LOAD,       // register, value
    SEQ_REG,            // operation, register, value
    SEQ_REG_UNARY,      // operation, register
    SEQ_REG_TABLE,      // mode, register, table, index
    SEQ_TEMPO,          // tempo
    SEQ_BANK_PROGRAM,   // bank << 8 | program
    SEQ_BANK,           // bank
    SEQ_PROGRAM,        // program
    SEQ_ENV_SCALE,      // oscillator, scale (over 16383)
    SEQ_ENV_SET,        // oscillator, points (byte offset)
    SEQ_SIMPLE_ADSR,    // attack, decay, sustain, release time, direct release
    SEQ_BUS_CONNECT,    // send, bus
    SEQ_IIR_CUTOFF,     // cutoff
    SEQ_IIR_SET,        // four coefficients
    SEQ_FIR_SET,        // coefficients (byte offset)
    SEQ_WAIT,           // ticks
    SEQ_INTERRUPT_TABLE,    // table (byte offset)
    SEQ_INTERRUPT_ON,   // mask
    SEQ_INTERRUPT_OFF,  // mask
    SEQ_INTERRUPT_RETURN,
    SEQ_INTERRUPT_CLEAR,
    SEQ_INTERRUPT_TIMER,    // count, period
    SEQ_SYNC,           // value
    SEQ_NOP,
    SEQ_FINISH,
    SEQ_CODES
};

// Register numbers as the operations name them: 0 to 13 the track's registers (3 the flag), 32 to 41 pairs of them, 64 to
// 79 ports, 96 and up the track's own fields.
enum SeqRegister : s32 {
    SEQ_REG_FLAG = 3,
    SEQ_REG_PORTS = 0x40,
    SEQ_REG_FIELDS = 0x60,
};

// An instrument bank: instruments by program, each with key and velocity regions naming waves of its wave bank.
struct BankFile {
    static const u32 cMagic = 0x4b4e424e;  // "NBNK"
    static const u32 cVersion = 1;

    u32 mMagic;
    u32 mVersion;
    u32 mBankNumber;
    u32 mWaveBank;
    u32 mProgramCount;
    u32 mKeyCount;
    u32 mVeloCount;
    u32 mPercCount;
    u32 mEffectCount;
    u32 mOscCount;
    u32 mPointBytes;
    u32 mReserved;
    // Then: BankProgram programs[mProgramCount], BankKey keys[mKeyCount], BankVelo velos[mVeloCount], BankPerc percs[mPercCount],
    // BankEffect effects[mEffectCount], BankOsc oscs[mOscCount], u8 points[mPointBytes] (SeqPoint records the oscillators name by
    // byte offset).
};
static_assert(sizeof(BankFile) == 48, "NBNK header");

struct BankProgram {
    enum Kind : u32 { NONE, INSTRUMENT, DRUMS };
    u32 mKind;
    f32 mVolume;
    f32 mPitch;
    s32 mOsc[2];        // index into the oscillators, -1 none
    u32 mEffectFirst;
    u32 mEffectCount;
    u32 mFirst;         // keys of an instrument; percussion of drums (key 0 first)
    u32 mCount;
};
static_assert(sizeof(BankProgram) == 36, "NBNK program");

struct BankKey {
    s32 mHighKey;
    u32 mVeloFirst;
    u32 mVeloCount;
};

struct BankVelo {
    s32 mHighVelocity;
    s32 mWave;
    f32 mVolume;
    f32 mPitch;
};

struct BankPerc {
    u32 mPresent;
    f32 mVolume;
    f32 mPitch;
    f32 mPan;
    u32 mRelease;
    u32 mEffectFirst;
    u32 mEffectCount;
    u32 mVeloFirst;
    u32 mVeloCount;
};
static_assert(sizeof(BankPerc) == 36, "NBNK percussion");

struct BankEffect {
    enum Kind : u32 { RANDOM, SENSE };
    u32 mKind;
    u32 mTarget;        // volume, pitch, pan, fxmix, dolby
    f32 mBase;          // random: base; sense: start level
    f32 mRange;         // random: range; sense: end level
    u32 mSenseType;     // 0 none, 1 velocity, 2 key
    u32 mMaxPoint;      // 0 to 255
};

struct BankOsc {
    u32 mTarget;
    f32 mRate;
    s32 mTable;         // byte offset into the points, -1 none
    s32 mRelease;       // byte offset into the points, -1 none
    f32 mScale;
    f32 mOffset;
};

// A wave bank: its waves by id, each in one of the bank's wave files (mapped PCM16). A wave id may be in several groups; the
// highest active group holds it.
struct WaveBankFile {
    static const u32 cMagic = 0x4b42574e;  // "NWBK"
    static const u32 cVersion = 1;

    u32 mMagic;
    u32 mVersion;
    u32 mBankNumber;
    u32 mGroupCount;
    u32 mWaveCount;
    u32 mIdCount;       // ids 0 to mIdCount - 1
    u32 mNameBytes;
    u32 mReserved;
    // Then: WaveGroup groups[mGroupCount], WaveEntry waves[mWaveCount], char names[mNameBytes] (store paths, each ended by 0).
};
static_assert(sizeof(WaveBankFile) == 32, "NWBK header");

struct WaveGroup {
    u32 mName;          // into the names
    u32 mFirst;         // waves of the group
    u32 mCount;
};

struct WaveEntry {
    u32 mId;
    u8 mFormat;
    u8 mBaseKey;
    u16 mReserved;
    f32 mSampleRate;
    u32 mOffset;        // bytes into the group's file
    u32 mBytes;
    u32 mLoop;
    u32 mLoopStart;
    u32 mLoopEnd;
    u32 mSampleCount;
};
static_assert(sizeof(WaveEntry) == 36, "NWBK wave");

// The sound archive (AudioRes/SMR): the sound table with its names, the effect programs, the banks and the wave banks.
// The table and the name table were two trees of the same numbering that need not agree: each level keeps both counts, and
// an entry says which of the two hold it.
struct SoundArchiveFile {
    static const u32 cMagic = 0x444e534e;  // "NSND"
    static const u32 cVersion = 1;

    u32 mMagic;
    u32 mVersion;
    u32 mSectionCount;  // the larger of the two trees'
    u32 mGroupCount;
    u32 mItemCount;
    u32 mStaticCount;
    u32 mBankCount;
    u32 mWaveBankCount;  // by wave bank number
    u32 mNameBytes;
    u32 mEffects;       // byte offset of the effect programs (an NSEQ, its groups the table's effect groups), 0 none
    u32 mEffectBytes;
    u32 mFlags;         // cHasTable, cHasNames
    // Then: SoundSection sections[mSectionCount], SoundGroup groups[mGroupCount], SoundItem items[mItemCount],
    // SoundStatic statics[mStaticCount], SoundPart banks[mBankCount], SoundPart waveBanks[mWaveBankCount],
    // char names[mNameBytes]; the files those name, each at a 16-byte boundary.
    static const u32 cHasTable = 1;
    static const u32 cHasNames = 2;
};
static_assert(sizeof(SoundArchiveFile) == 48, "NSND header");

// mIn: cInTable, cInNames, the trees that have the entry.
struct SoundSection {
    u32 mIn;
    u32 mGroupFirst;
    u32 mGroupCount;    // in the table
    u32 mNameGroupCount;
};

struct SoundGroup {
    u32 mIn;
    u32 mItemFirst;
    u32 mItemCount;     // in the table
    u32 mNameCount;
};

// The fields of the item's type: an effect (0x50) has its audible and sound switches, a sequence (0x60) its resource, chord
// resource and sound switch, a stream (0x70) its channel control and file path.
struct SoundItem {
    u8 mType;           // the table's type byte, kept for an entry the table leaves out too
    u8 mIn;
    u8 mPriority;
    u8 mVolume;
    u16 mResource;
    u16 mChordResource;
    u16 mAudibleSw;
    u16 mSoundSw;
    u16 mChannelCtrl;
    u16 mReserved;
    u32 mStreamPath;    // in the names, ~0 none
    u32 mName;          // in the names, ~0 none (the name table's entry was 0)
};
static_assert(sizeof(SoundItem) == 24, "NSND item");

static const u32 cInTable = 1;
static const u32 cInNames = 2;

// A sequence the archive holds for one sound id.
struct SoundStatic {
    u32 mSoundId;
    u32 mFile;          // byte offset of its NSEQ
    u32 mBytes;
};

struct SoundPart {
    u32 mFile;          // byte offset, 0 none
    u32 mBytes;
};
