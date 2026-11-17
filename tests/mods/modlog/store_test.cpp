// The mod log's store without the game (contract mod-log 8, last paragraph; 2 and 6): LumaModLog.cpp compiled into
// this program with the module's few outside calls stubbed. F builds and runs it (J writes only):
//
//   clang++ -std=gnu++17 -O1 -DNEBULA_GAME_MODULE=1 -Wall -Wextra -Werror -Wno-unused-parameter -include nebula_prelude.h ^
//       -I <tree>/native/include -I <tree>/native/petari/include -I <tree>/native/math/include ^
//       agents/luma/j/tests/modlog/store_test.cpp -o store_test.exe && store_test.exe
//
// (on Linux and Android the same with -pthread). Exit 0: every case passed; otherwise each failed case is printed.

// Found through -I <tree>/native/petari/include.
#include "../src/Game/System/LumaModLog.cpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// The module's outside calls.
static std::vector< std::string > gLog;
static std::atomic< int > gLogLock{0};

void nebula_log(const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    while (gLogLock.exchange(1) != 0) {
    }
    gLog.push_back(line);
    gLogLock.store(0);
}

int nebula_snprintf(char* pDst, size_t length, const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    const int n = vsnprintf(pDst, length, pFormat, args);
    va_end(args);
    return n;
}

void nebula_panic(const char* file, int line, const char* format, ...) {
    std::fprintf(stderr, "panic at %s:%d\n", file, line);
    std::abort();
}

// NebulaText: three rows; the played language is "French" here (its rows marked FR:), English the table's.
namespace {
    struct Row {
        const char* id;
        const char16_t* played;
        const char16_t* english;
    };
    const Row cRows[] = {
        {"Hook.NoResult", u"FR {mod}: crochet {found}", u"{mod}: a hook on {found} gave no result"},
        {"Script.S0901", u"FR {0} hors du fil", u"{0} was called off the game thread"},
        {"ModLog.ArgsCut", u"FR {mod}: plus de 8", u"{mod}: a message had more than 8 values"},
        {"ModLog.NoText", u"FR codes", u"Mod messages are shown as codes"},
    };
    const Row* rowOf(const char* pId) {
        for (const Row& rRow : cRows) {
            if (std::strcmp(rRow.id, pId) == 0) {
                return &rRow;
            }
        }
        return nullptr;
    }
}  // namespace

namespace NebulaText {
    bool has(const char* pId) { return rowOf(pId) != nullptr; }
    s32 length(const char* pId) {
        const Row* p = rowOf(pId);
        s32 n = 0;
        while (p != nullptr && p->played[n] != 0) {
            n++;
        }
        return n;
    }
    s32 get(char16_t* pDst, s32 capacity, const char* pId) {
        const Row* p = rowOf(pId);
        s32 n = 0;
        while (p != nullptr && p->played[n] != 0 && n + 1 < capacity) {
            pDst[n] = p->played[n];
            n++;
        }
        pDst[n] = 0;
        return n;
    }
    const char16_t* english(const char* pId) {
        const Row* p = rowOf(pId);
        return p != nullptr ? p->english : nullptr;
    }
}  // namespace NebulaText

// The settings menu's host-text translation: "FR " and the sentence, cut as the real one cuts.
namespace NebulaSettingsMenu {
    s32 translateHost(char16_t* pDst, s32 capacity, const char* pText) {
        const std::string text = std::string("FR ") + pText;
        s32 n = 0;
        while (n < static_cast< s32 >(text.size()) && n + 1 < capacity) {
            pDst[n] = static_cast< char16_t >(static_cast< unsigned char >(text[n]));
            n++;
        }
        if (capacity > 0) {
            pDst[n] = 0;
        }
        return n;
    }
}  // namespace NebulaSettingsMenu

// The cases.
static int gFailed = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            gFailed++;                                                           \
        }                                                                        \
    } while (0)

static std::vector< std::string > gFacts;

static void factSink(const char* key, const char* json) {
    while (gLogLock.exchange(1) != 0) {
    }
    gFacts.push_back(std::string(key) + " " + json);
    gLogLock.store(0);
}

// A fresh session: the store forgotten (leaked; a test program), the notice cursor with it.
static void fresh() {
    luma::modlog::sStore = nullptr;
    luma::modlog::sModsOn = 0;
    luma::modlog::sFact = nullptr;
    gLog.clear();
    gFacts.clear();
}

static LumaModLogEntry entry(const char* mod, u32 source, u32 code, const char* file, u32 line) {
    LumaModLogEntry e = {};
    e.size = sizeof(e);
    e.source = source;
    e.level = LUMA_MODLOG_ERROR;
    e.code = code;
    e.mod = mod;
    e.file = file;
    e.line = line;
    return e;
}

static const luma::modlog::Entry* find(const char* mod, u32 code, u32 line) {
    for (const luma::modlog::Entry* p = luma::modlog::first(); p != nullptr; p = luma::modlog::nextOf(p)) {
        if (std::strcmp(p->mod, mod) == 0 && p->code == code && p->line == line) {
            return p;
        }
    }
    return nullptr;
}

static const luma::modlog::Mod* findMod(const char* id) {
    for (const luma::modlog::Mod* p = luma::modlog::firstMod(); p != nullptr; p = luma::modlog::nextOf(p)) {
        if (std::strcmp(p->id, id) == 0) {
            return p;
        }
    }
    return nullptr;
}

static bool logHas(const char* part) {
    for (const std::string& rLine : gLog) {
        if (rLine.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static void callerMistakes() {
    // Section 6: each is a log line naming the mistake, and nothing is kept (not even the store).
    fresh();
    luma_modlog_add(nullptr);
    CHECK(logHas("luma_modlog_add(null)"));
    LumaModLogEntry e = entry("m", LUMA_MODLOG_CODE, 901, "f", 1);
    e.size = 8;
    luma_modlog_add(&e);
    e = entry(nullptr, LUMA_MODLOG_CODE, 901, "f", 1);
    luma_modlog_add(&e);
    e = entry("m", 99, 901, "f", 1);
    luma_modlog_add(&e);
    e = entry("m", LUMA_MODLOG_CODE, 901, "f", 1);
    e.level = 7;
    luma_modlog_add(&e);
    CHECK(gLog.size() == 5);
    CHECK(!luma::modlog::allocated());
    CHECK(luma::modlog::kept() == 0);
}

static void keepAndRepeat() {
    fresh();
    luma::modlog::session(1, factSink);
    LumaModLogEntry e = entry("m", LUMA_MODLOG_CODE, 2, "Mario::update", 0);
    e.names[0] = "found";
    e.values[0] = "MR::getPlayerLeft";
    e.count = 1;
    luma::modlog::add(e, "Hook.NoResult");
    luma::modlog::add(e, "Hook.NoResult");
    const luma::modlog::Entry* p = find("m", 2, 0);
    CHECK(p != nullptr);
    CHECK(p != nullptr && luma::modlog::countOf(p) == 2);
    CHECK(luma::modlog::kept() == 1 && luma::modlog::errors() == 1);
    CHECK(p != nullptr && std::strcmp(p->english, "m: a hook on MR::getPlayerLeft gave no result") == 0);
    CHECK(p != nullptr && std::strcmp(p->textUtf8, "FR m: crochet MR::getPlayerLeft") == 0);
    // CODE: no log line from the mod log (the kernel's record writes it).
    CHECK(gLog.empty());
    // Facts: the keep and the repeat, at counts 1 and 2.
    CHECK(gFacts.size() == 2);
    CHECK(gFacts.size() == 2 && gFacts[1].find("\"count\":2") != std::string::npos);
    // The same key from another mod, another line, another source: each its own.
    luma::modlog::add(entry("n", LUMA_MODLOG_CODE, 2, "Mario::update", 0), "Hook.NoResult");
    luma::modlog::add(entry("m", LUMA_MODLOG_CODE, 2, "Mario::update", 1), "Hook.NoResult");
    luma::modlog::add(entry("m", LUMA_MODLOG_CONTENT, 2, "Mario::update", 0), "Hook.NoResult");
    CHECK(luma::modlog::kept() == 4);
    // add gives the entry it kept or repeated; `counted` sets the caller's own count, never lowers it.
    const luma::modlog::Entry* pHeld = luma::modlog::add(e, "Hook.NoResult");
    CHECK(pHeld == p && luma::modlog::countOf(p) == 3);
    const size_t factsBefore = gFacts.size();
    luma::modlog::counted(pHeld, 40);
    CHECK(luma::modlog::countOf(p) == 40);
    CHECK(gFacts.size() == factsBefore + 1 && gFacts.back().find("\"count\":40") != std::string::npos);
    luma::modlog::counted(pHeld, 39);
    CHECK(luma::modlog::countOf(p) == 40 && gFacts.size() == factsBefore + 1);
    luma::modlog::counted(nullptr, 5);
    CHECK(luma::modlog::kept() == 4);
}

static void words() {
    fresh();
    // Positional names and a {name} the entry does not give: stays as written.
    LumaModLogEntry e = entry("m", LUMA_MODLOG_CODE, 901, "a.lumi", 3);
    e.names[0] = "0";
    e.values[0] = "lt.example";
    e.count = 1;
    luma::modlog::add(e, "Script.S0901");
    const luma::modlog::Entry* p = find("m", 901, 3);
    CHECK(p != nullptr && std::strcmp(p->english, "lt.example was called off the game thread") == 0);
    luma::modlog::add(entry("m", LUMA_MODLOG_CODE, 2, "x", 9), "Hook.NoResult");
    p = find("m", 2, 9);
    CHECK(p != nullptr && std::strcmp(p->english, "m: a hook on {found} gave no result") == 0);
    // No row: the row id and the values, nothing lost; a module source says no NoText.
    e = entry("m", LUMA_MODLOG_CODE, 77, "a.lumi", 4);
    e.names[0] = "0";
    e.values[0] = "x";
    e.count = 1;
    luma::modlog::add(e, nullptr);
    p = find("m", 77, 4);
    CHECK(p != nullptr && std::strcmp(p->english, "code 77 0=x") == 0);
    CHECK(find("nebula", 0, 0) == nullptr);
    // The host's text given: kept as given, in both.
    e = entry("m", LUMA_MODLOG_LOADER, 505, "luma.json", 0);
    e.text = "le code ne démarre pas";
    e.english = "its code did not start";
    luma::modlog::add(e, nullptr);
    p = find("m", 505, 0);
    CHECK(p != nullptr && std::strcmp(p->textUtf8, "le code ne démarre pas") == 0 && std::strcmp(p->english, "its code did not start") == 0);
    CHECK(p != nullptr && p->text[12] == static_cast< char16_t >(0xE9));
    // The host's without words (the importer missing): the code and values as given, and one NoText (an Error of
    // "nebula"), said once however many.
    e = entry("m", LUMA_MODLOG_VALIDATOR, 505, "luma.json", 2);
    e.names[0] = "kind";
    e.values[0] = "P:Name.Function";
    e.count = 1;
    luma::modlog::add(e, nullptr);
    p = find("m", 505, 2);
    CHECK(p != nullptr && std::strcmp(p->english, "L505 kind=P:Name.Function") == 0);
    e.line = 3;
    luma::modlog::add(e, nullptr);
    const luma::modlog::Entry* pNoText = find("nebula", 0, 0);
    CHECK(pNoText != nullptr && pNoText->level == LUMA_MODLOG_ERROR && luma::modlog::countOf(pNoText) == 2);
    CHECK(pNoText != nullptr && std::strcmp(pNoText->english, "Mod messages are shown as codes") == 0);
    // More than 8 values: the first 8 used, one ArgsCut Note for the mod.
    e = entry("m", LUMA_MODLOG_CODE, 901, "b.lumi", 1);
    for (u32 i = 0; i < LUMA_MODLOG_ARGS; i++) {
        e.names[i] = "0";
        e.values[i] = "v";
    }
    e.count = 9;
    luma::modlog::add(e, "Script.S0901");
    bool cut = false;
    for (const luma::modlog::Entry* q = luma::modlog::first(); q != nullptr; q = luma::modlog::nextOf(q)) {
        cut = cut || (std::strcmp(q->mod, "m") == 0 && q->code == 0 && std::strcmp(q->file, "ModLog.ArgsCut") == 0 &&
                      q->level == LUMA_MODLOG_NOTE);
    }
    CHECK(cut);
    // A CODE entry given as a Note: kept as an Error, the caller named in the log.
    e = entry("m", LUMA_MODLOG_CODE, 901, "c.lumi", 5);
    e.level = LUMA_MODLOG_NOTE;
    luma::modlog::add(e, "Script.S0901");
    p = find("m", 901, 5);
    CHECK(p != nullptr && p->level == LUMA_MODLOG_ERROR);
    CHECK(logHas("given as a Note"));
}

static void limits() {
    fresh();
    for (u32 line = 1; line <= 300; line++) {
        luma::modlog::add(entry("a", LUMA_MODLOG_CODE, 901, "f", line), "Script.S0901");
    }
    luma::modlog::add(entry("b", LUMA_MODLOG_CODE, 901, "f", 1), "Script.S0901");
    CHECK(findMod("a") != nullptr && findMod("a")->kept == 256 && luma::modlog::pastOf(findMod("a")) == 44);
    CHECK(find("b", 901, 1) != nullptr);
    CHECK(luma::modlog::kept() == 257);
    fresh();
    const char* const cMods[5] = {"m1", "m2", "m3", "m4", "m5"};
    for (const char* pMod : cMods) {
        for (u32 line = 1; line <= 300; line++) {
            luma::modlog::add(entry(pMod, LUMA_MODLOG_CODE, 901, "f", line), "Script.S0901");
        }
    }
    CHECK(luma::modlog::kept() == 1024 + 16);
    for (int i = 0; i < 4; i++) {
        CHECK(findMod(cMods[i])->kept == 256 && luma::modlog::pastOf(findMod(cMods[i])) == 44);
    }
    CHECK(findMod("m5")->kept == 16 && luma::modlog::pastOf(findMod("m5")) == 284);
    CHECK(find("m5", 901, 16) != nullptr && find("m5", 901, 17) == nullptr);
    // A repeat of a kept entry past the limits is still counted on it.
    luma::modlog::add(entry("m5", LUMA_MODLOG_CODE, 901, "f", 16), "Script.S0901");
    CHECK(luma::modlog::countOf(find("m5", 901, 16)) == 2);
}

static void notices() {
    fresh();
    luma::modlog::add(entry("a", LUMA_MODLOG_CODE, 901, "f", 1), "Script.S0901");
    LumaModLogEntry note = entry("a", LUMA_MODLOG_COOK, 404, "f", 2);
    note.level = LUMA_MODLOG_NOTE;
    note.text = "overlap";
    luma::modlog::add(note, nullptr);
    luma::modlog::add(entry("b", LUMA_MODLOG_CODE, 901, "f", 1), "Script.S0901");
    // Before the first notice: the Errors so far are one summary (2: the Note is not one).
    CHECK(luma::modlog::takeSummary() == 2);
    CHECK(luma::modlog::takeSummary() == 0);
    CHECK(luma::modlog::noticesWaiting() == 0 && luma::modlog::takeNotice() == nullptr);
    luma::modlog::add(entry("c", LUMA_MODLOG_CODE, 901, "f", 1), "Script.S0901");
    luma::modlog::add(note, nullptr);  // a repeat: not a notice
    CHECK(luma::modlog::noticesWaiting() == 1);
    const luma::modlog::Entry* p = luma::modlog::takeNotice();
    CHECK(p != nullptr && std::strcmp(p->mod, "c") == 0);
    CHECK(luma::modlog::takeNotice() == nullptr);
}

static void threads() {
    // The first add of a session from eight threads at once: one store, every entry in it; then one key from all of
    // them: one entry, counted once per add.
    for (int round = 0; round < 50; round++) {
        fresh();
        std::atomic< int > ready{0};
        std::vector< std::thread > workers;
        const int cThreads = 8;
        for (int t = 0; t < cThreads; t++) {
            workers.emplace_back([t, &ready]() {
                ready.fetch_add(1);
                while (ready.load() < cThreads) {
                }
                luma::modlog::add(entry("t", LUMA_MODLOG_CODE, 901, "f", 100 + t), "Script.S0901");
                for (int i = 0; i < 100; i++) {
                    luma::modlog::add(entry("t", LUMA_MODLOG_CODE, 901, "same", 7), "Script.S0901");
                }
            });
        }
        for (std::thread& rWorker : workers) {
            rWorker.join();
        }
        CHECK(luma::modlog::kept() == cThreads + 1);
        u32 listed = 0;
        for (const luma::modlog::Entry* p = luma::modlog::first(); p != nullptr; p = luma::modlog::nextOf(p)) {
            listed++;
        }
        CHECK(listed == cThreads + 1);
        CHECK(findMod("t") != nullptr && findMod("t")->kept == cThreads + 1 && luma::modlog::firstMod() == findMod("t") &&
              luma::modlog::nextOf(findMod("t")) == nullptr);
        CHECK(find("t", 901, 7) != nullptr && luma::modlog::countOf(find("t", 901, 7)) == cThreads * 100);
    }
}

static void json() {
    char out[12];
    luma::modlog::jsonString(out, sizeof(out), "a\"b\\c\n");  // too long for 12: cut, and still a string
    char big[64];
    luma::modlog::jsonString(big, sizeof(big), "a\"b\\c\n");
    CHECK(std::strcmp(big, "\"a\\\"b\\\\c\\u000a\"") == 0);
    CHECK(out[0] == '"' && out[std::strlen(out) - 1] == '"');
    // A cut never leaves half a character: "é" is two bytes; room for the quote, "aa" and one byte of it.
    char six[7];
    luma::modlog::jsonString(six, sizeof(six), "aaa\xc3\xa9");
    CHECK(std::strcmp(six, "\"aaa\"") == 0);
    char five[6];
    luma::modlog::jsonString(five, sizeof(five), "aa\xc3\xa9z");
    CHECK(std::strcmp(five, "\"aa\"") == 0);
}

static const luma::modlog::Entry* findFile(const char* file) {
    for (const luma::modlog::Entry* p = luma::modlog::first(); p != nullptr; p = luma::modlog::nextOf(p)) {
        if (std::strcmp(p->file, file) == 0) {
            return p;
        }
    }
    return nullptr;
}

// A host sentence (the Mods page's, code 0): translated as host text, English as given; given text wins; an entry
// whose size stops before `host` never has it read.
static void hostWords() {
    fresh();
    LumaModLogEntry e = entry("m", LUMA_MODLOG_LOADER, 0, "code/m.dll", 0);
    e.host = "ITS CODE DID NOT START";
    luma::modlog::add(e, nullptr);
    const luma::modlog::Entry* p = findFile("code/m.dll");
    CHECK(p != nullptr && std::strcmp(p->english, "ITS CODE DID NOT START") == 0);
    CHECK(p != nullptr && std::strcmp(p->textUtf8, "FR ITS CODE DID NOT START") == 0);
    CHECK(p != nullptr && p->text[0] == u'F' && p->text[3] == u'I');
    e = entry("m", LUMA_MODLOG_LOADER, 0, "code/m2.dll", 0);
    e.host = "ITS CODE DID NOT START";
    e.text = "given";
    e.english = "given";
    luma::modlog::add(e, nullptr);
    p = findFile("code/m2.dll");
    CHECK(p != nullptr && std::strcmp(p->english, "given") == 0);
    LumaModLogEntry old = entry("m", LUMA_MODLOG_LOADER, 0, "code/m3.dll", 0);
    old.size = LUMA_MODLOG_ENTRY_SIZE;
    old.host = "ITS CODE DID NOT START";
    luma::modlog::add(old, nullptr);
    p = findFile("code/m3.dll");
    CHECK(p != nullptr && std::strstr(p->english, "ITS CODE") == nullptr);
}

int main() {
    callerMistakes();
    keepAndRepeat();
    words();
    hostWords();
    limits();
    notices();
    threads();
    json();
    if (gFailed == 0) {
        std::printf("store_test: all passed\n");
    }
    return gFailed == 0 ? 0 : 1;
}
