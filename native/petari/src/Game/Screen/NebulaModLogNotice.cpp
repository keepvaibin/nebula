#include "Game/Screen/NebulaModLogNotice.hpp"
#include <nebula_module.h>

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/Tunable.hpp"
#include <Game/Ui/Layout.hpp>
#include <cstring>
#include <nebula_format.h>

using namespace NebulaUi;

// Contract mod-log 4.1. The lines on screen are kept here (file scope), not in the scene's actor, so a scene change
// does not lose one already taken from the log: the next scene's actor shows them on.

namespace {
    // Screen units (NebulaUi::readScreen): the text's size, the window's padding around it, the space between lines,
    // the distance from the screen's left edge and from its bottom (above the game's bottom row of HUD), and the
    // widest a line may be, as a share of the screen's width.
    NEBULA_TUNABLE(f32, cSize, 17.0f);
    NEBULA_TUNABLE(f32, cPadX, 12.0f);
    NEBULA_TUNABLE(f32, cPadY, 7.0f);
    NEBULA_TUNABLE(f32, cGap, 6.0f);
    NEBULA_TUNABLE(f32, cLeft, 18.0f);
    // 88: the lowest line clears the lives counter (Mario and his count, bottom left), whose top stands about 76 units
    // up (run J4 3124 at 1280x720: 64 put the third line over Mario's face).
    NEBULA_TUNABLE(f32, cBottom, 88.0f);
    NEBULA_TUNABLE(f32, cMaxShare, 0.55f);
    NEBULA_TUNABLE(u32, cFill, 0x141A2CD8);
    NEBULA_TUNABLE(u32, cRing, 0xD9534FFF);
    NEBULA_TUNABLE(u32, cText, 0xFFFFFFFF);
    // Frames at 60 a second: in, held (6 s, contract 4.1), out (0.3 s); and the share of the way a line moves to its
    // place each frame when the lines below it go.
    NEBULA_TUNABLE(s32, cInFrames, 8);
    NEBULA_TUNABLE(s32, cHoldFrames, 360);
    NEBULA_TUNABLE(s32, cOutFrames, 18);
    NEBULA_TUNABLE(f32, cFollow, 0.25f);

    const s32 cLines = 3;
    const char16_t cEllipsis = 0x2026;  // the line cut short
    const u32 cTextRoom = 256;  // units a line's text box holds; longer lines are cut before they are measured

    enum { KindNone = 0, KindEntry, KindSummary, KindMore };

    struct Slot {
        s32 kind;
        const luma::modlog::Entry* entry;  // KindEntry
        u32 count;                         // KindSummary, KindMore
        s32 frame;                         // frames shown
        f32 y;                             // its place now, from the bottom line's (0) up
        bool shown;                        // the text is in its pane
        bool told;                         // its modlog.notice fact is written (once, though it may move pane)
    };

    // File scope, zero at start (no run-time initialisation in the module).
    Slot sSlots[cLines] = {};
    u32 sSeenRevision = 0;
    bool sSummaryTaken = false;
    bool sBacklog = false;  // Errors wait behind three lines on screen

    f32 clamp01(f32 t) {
        return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    }

    s32 activeCount() {
        s32 n = 0;
        for (s32 i = 0; i < cLines; i++) {
            n += sSlots[i].kind != KindNone ? 1 : 0;
        }
        return n;
    }

    // A slot taken in order (the oldest first); the lines are kept packed from index 0.
    void push(s32 kind, const luma::modlog::Entry* pEntry, u32 count) {
        const s32 at = activeCount();
        if (at >= cLines) {
            return;
        }
        Slot& rSlot = sSlots[at];
        rSlot.kind = kind;
        rSlot.entry = pEntry;
        rSlot.count = count;
        rSlot.frame = 0;
        // A new line comes in at the bottom; the ones above it rise to make room (their own y follows).
        rSlot.y = 0.0f;
        rSlot.shown = false;
        rSlot.told = false;
    }

    // What the log has for the notice, while a line is free (contract 4.1). Free lines show the oldest at once. Lines
    // that waited behind full ones: when more than three wait as a line frees, the oldest fold into one "{count} more"
    // line, so the newest two still show.
    void intake() {
        while (activeCount() < cLines) {
            const u32 waiting = luma::modlog::noticesWaiting();
            if (waiting == 0) {
                sBacklog = false;
                return;
            }
            if (sBacklog && waiting > static_cast< u32 >(cLines)) {
                u32 folded = 0;
                for (u32 i = 0; i + 2 < waiting; i++) {
                    folded += luma::modlog::takeNotice() != nullptr ? 1u : 0u;
                }
                push(KindMore, nullptr, folded);
                continue;
            }
            push(KindEntry, luma::modlog::takeNotice(), 0);
        }
        sBacklog = luma::modlog::noticesWaiting() != 0;
    }

    // Each line one frame on; a line that has gone out leaves its place to the ones after it.
    bool advance() {
        bool freed = false;
        for (s32 i = 0; i < cLines; i++) {
            if (sSlots[i].kind != KindNone && ++sSlots[i].frame >= cInFrames + cHoldFrames + cOutFrames) {
                sSlots[i].kind = KindNone;
                freed = true;
            }
        }
        if (freed) {
            s32 to = 0;
            for (s32 from = 0; from < cLines; from++) {
                if (sSlots[from].kind != KindNone) {
                    if (to != from) {
                        sSlots[to] = sSlots[from];
                        sSlots[to].shown = false;  // its text moves to another pane
                        sSlots[from].kind = KindNone;
                    }
                    to++;
                }
            }
            for (s32 i = to; i < cLines; i++) {
                sSlots[i].kind = KindNone;
                sSlots[i].shown = false;
            }
        }
        return freed;
    }

    u32 length16(const char16_t* p) {
        u32 n = 0;
        while (p[n] != 0) {
            n++;
        }
        return n;
    }

    // `pText` with {count} written out, into pDst (room units).
    void withCount(char16_t* pDst, u32 room, const char16_t* pText, u32 count) {
        u32 at = 0;
        for (const char16_t* p = pText; *p != 0 && at + 1 < room; p++) {
            if (p[0] == u'{' && p[1] == u'c' && p[2] == u'o' && p[3] == u'u' && p[4] == u'n' && p[5] == u't' && p[6] == u'}') {
                char digits[12];
                nebula_snprintf(digits, sizeof(digits), "%u", count);
                for (const char* d = digits; *d != 0 && at + 1 < room; d++) {
                    pDst[at++] = static_cast< char16_t >(*d);
                }
                p += 6;
                continue;
            }
            pDst[at++] = *p;
        }
        pDst[at] = 0;
    }

    // The line's words: "<mod>: <text>" for an entry (the text alone when it already begins with the mod's id, as the
    // content rows do), the summary, or the folded count.
    void lineOf(const Slot& rSlot, char16_t* pDst, u32 room) {
        pDst[0] = 0;
        if (rSlot.kind == KindEntry && rSlot.entry != nullptr) {
            const luma::modlog::Entry* const pEntry = rSlot.entry;
            u32 at = 0;
            const char* pMod = pEntry->mod;
            bool begins = true;
            for (u32 i = 0; pMod[i] != 0; i++) {
                if (static_cast< unsigned char >(pMod[i]) >= 0x80u || pEntry->text[i] != static_cast< char16_t >(pMod[i])) {
                    begins = false;
                    break;
                }
            }
            if (!begins) {
                // A mod id is ASCII (luma-format); a file name standing in for one may not be: each byte as it is
                // would be wrong, so a non-ASCII byte shows as '?'.
                for (; *pMod != 0 && at + 3 < room; pMod++) {
                    pDst[at++] = static_cast< unsigned char >(*pMod) < 0x80u ? static_cast< char16_t >(*pMod) : u'?';
                }
                pDst[at++] = u':';
                pDst[at++] = u' ';
            }
            for (const char16_t* p = pEntry->text; *p != 0 && at + 1 < room; p++) {
                pDst[at++] = *p;
            }
            pDst[at] = 0;
            return;
        }
        MR::Text16 row;
        getText(row, rSlot.kind == KindSummary ? "ModLog.Summary" : "ModLog.More");
        withCount(pDst, room, row.c_str(), rSlot.count);
    }

    // noticeFact's text, its JSON form and the fact: over a page together, so not on the stack (the module has no stack
    // probe: nebula_runtime_win.c). The notice runs on the game thread only.
    char sFactShown[4 * cTextRoom];
    char sFactText[6 * sizeof(sFactShown) + 3];
    char sFactJson[sizeof(sFactText) + 256 + 96];

    // As the screen shows it: modlog.notice {"mod", "code", "text"} (contract 4.3), the text as cut.
    void noticeFact(const Slot& rSlot, const char16_t* pShown) {
        if (!luma::modlog::facts()) {
            return;
        }
        char* const shown = sFactShown;
        luma::modlog::toUtf8(shown, sizeof(sFactShown), pShown);
        char* const text = sFactText;
        luma::modlog::jsonString(text, sizeof(sFactText), shown);
        char mod[256];
        const bool entry = rSlot.kind == KindEntry && rSlot.entry != nullptr;
        luma::modlog::jsonString(mod, sizeof(mod), entry ? rSlot.entry->mod : "");
        char* const json = sFactJson;
        if (entry) {
            nebula_snprintf(json, sizeof(sFactJson), "{\"mod\":%s,\"code\":%u,\"text\":%s}", mod, rSlot.entry->code, text);
        } else {
            nebula_snprintf(json, sizeof(sFactJson), "{\"mod\":%s,\"row\":\"%s\",\"count\":%u,\"text\":%s}", mod,
                            rSlot.kind == KindSummary ? "ModLog.Summary" : "ModLog.More", rSlot.count, text);
        }
        luma::modlog::fact("modlog.notice", json);
    }
};  // namespace

class NebulaModLogNoticeScreen : public LayoutActor {
public:
    NebulaModLogNoticeScreen() : LayoutActor("NebulaModLogNotice", true), mBuilt(false), mRoot(nullptr), mAny(false) {
        mKits.mText = &mTextKit;
        mKits.mWindow = &mWindowKit;
        mKits.mButton = nullptr;
        for (s32 i = 0; i < cLines; i++) {
            mLine[i] = nullptr;
            mWindow[i] = nullptr;
            mText[i] = nullptr;
            mWidth[i] = 0.0f;
        }
    }

    virtual void init(const TableIter& /*rIter*/) {
        initLayoutManager("InformationWindow", 0);
        LayoutManager* pManager = getLayoutManager();
        static const char cLayoutName[] = "NebulaModLogNotice";
        delete[] pManager->mLayoutName;
        pManager->mLayoutName = new char[sizeof(cLayoutName)];
        memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));
        mBuilt = build();
        if (!mBuilt) {
            // The log keeps every entry; the page still shows them.
            nebula_log("NebulaModLogNotice: a layout or pane is missing; mod problems show on the Mods > Log page only\n");
            return;
        }
        LayoutActor::appear();
    }

    void step() {
        if (!mBuilt) {
            return;
        }
        // Nothing changed and nothing on screen: one read of the revision (contract 5).
        const u32 revision = luma::modlog::revision();
        const bool any = activeCount() != 0;
        if (!sSummaryTaken) {
            // The first notice of the session: what was said before it is one line (contract 4.1).
            sSummaryTaken = true;
            const u32 summary = luma::modlog::takeSummary();
            if (summary != 0) {
                push(KindSummary, nullptr, summary);
            }
        }
        bool freed = false;
        if (any) {
            freed = advance();
        }
        if (revision != sSeenRevision || freed) {
            sSeenRevision = revision;
            intake();
        }
        mAny = activeCount() != 0;
        if (!mAny && !any) {
            return;
        }
        place();
        movement();
        calcAnim();
    }

    void drawScreen() const {
        if (mBuilt && mAny && !getLayoutManager()->mIsScreenHidden) {
            draw();
        }
    }

private:
    bool build() {
        LayoutManager* pManager = getLayoutManager();
        Pane* pRoot = pManager->mLayout->mpRootPane;
        if (pRoot == nullptr || !mTextKit.init(pManager->mLayoutHolder, "InformationWindow", pManager->mLayout)) {
            return false;
        }
        // The system window's look when it is there; else the information window's own (NebulaUi::makeWindow).
        if (!mWindowKit.init("SysInfoWindowMini", pManager->mLayout)) {
            mKits.mWindow = &mTextKit;
        }
        for (Ui::Pane* pChild : pRoot->GetChildList()) {
            pChild->SetVisible(false);
        }
        mRoot = makeGroup(pRoot, "MlRoot");
        if (mRoot == nullptr) {
            return false;
        }
        for (s32 i = 0; i < cLines; i++) {
            char name[16];
            nebula_snprintf(name, sizeof(name), "MlLine%d", i);
            mLine[i] = makeGroup(mRoot, name);
            nebula_snprintf(name, sizeof(name), "MlWin%d", i);
            mWindow[i] = mLine[i] != nullptr ? makeWindow(mKits, mLine[i], name) : nullptr;
            nebula_snprintf(name, sizeof(name), "MlText%d", i);
            mText[i] = mLine[i] != nullptr ? makeText(mKits, mLine[i], name, cTextRoom) : nullptr;
            if (mWindow[i] == nullptr || mText[i] == nullptr) {
                return false;
            }
            paint(mWindow[i], cFill, cRing);
            setTextStyle(mText[i], cSize, cText, cText, 3);
            mLine[i]->SetVisible(false);
        }
        setInfluencedAlphaRecursive(mRoot, 0);
        return true;
    }

    // Line i's words into its pane, cut to `limit` wide at a character and ended with "…" (the page has it whole).
    void fill(s32 i, f32 limit) {
        char16_t line[cTextRoom];
        lineOf(sSlots[i], line, cTextRoom);
        setText(mText[i], line);
        f32 width;
        f32 height;
        measure(mText[i], 4000.0f, &width, &height);
        if (width > limit) {
            // The longest start that fits with the ellipsis: binary search on whole characters. The whole line does not
            // fit, so at most length - 1 units stay (and the ellipsis and terminator fit in the room).
            const u32 length = length16(line);
            char16_t cut[cTextRoom];
            u32 lo = 0;
            u32 hi = length > 0 ? length - 1 : 0;
            while (lo < hi) {
                u32 mid = (lo + hi + 1) / 2;
                if (mid < length && line[mid] >= 0xDC00u && line[mid] < 0xE000u) {
                    mid--;  // never half a pair
                }
                if (mid <= lo) {
                    break;
                }
                for (u32 k = 0; k < mid; k++) {
                    cut[k] = line[k];
                }
                cut[mid] = cEllipsis;
                cut[mid + 1] = 0;
                setText(mText[i], cut);
                measure(mText[i], 4000.0f, &width, &height);
                if (width <= limit) {
                    lo = mid;
                } else {
                    hi = mid - 1;
                }
            }
            u32 keep = lo;
            while (keep > 0 && line[keep - 1] == u' ') {
                keep--;
            }
            line[keep] = cEllipsis;
            line[keep + 1] = 0;
            setText(mText[i], line);
            measure(mText[i], 4000.0f, &width, &height);
        }
        mWidth[i] = width;
        if (!sSlots[i].told) {
            sSlots[i].told = true;
            noticeFact(sSlots[i], line);
        }
    }

    void place() {
        f32 scaleX;
        f32 screenWidth;
        f32 screenHeight;
        readScreen(&scaleX, &screenWidth, &screenHeight);
        mRoot->mScale = Vec2f(scaleX, 1.0f);
        const f32 limit = screenWidth * cMaxShare - cPadX * 2.0f;
        const f32 lineHeight = cSize * cCapHeight + cPadY * 2.0f + cSize * 0.4f;
        const f32 left = -screenWidth * 0.5f + cLeft;
        const f32 bottom = -screenHeight * 0.5f + cBottom + lineHeight * 0.5f;
        const s32 count = activeCount();
        for (s32 i = 0; i < cLines; i++) {
            Slot& rSlot = sSlots[i];
            if (rSlot.kind == KindNone) {
                mLine[i]->SetVisible(false);
                continue;
            }
            if (!rSlot.shown) {
                fill(i, limit);
                rSlot.shown = true;
            }
            // The newest at the bottom: line i of `count` sits (count - 1 - i) lines up.
            const f32 target = static_cast< f32 >(count - 1 - i) * (lineHeight + cGap);
            rSlot.y += (target - rSlot.y) * cFollow;
            if (rSlot.y > target - 0.25f && rSlot.y < target + 0.25f) {
                rSlot.y = target;
            }
            const f32 boxWidth = mWidth[i] + cPadX * 2.0f;
            const f32 y = bottom + rSlot.y;
            shape(mWindow[i], left + boxWidth * 0.5f, y, boxWidth, lineHeight, lineHeight * 0.5f);
            mText[i]->mTranslate = Vec3f(left + cPadX, textCentre(y, cSize), 0.0f);
            f32 alpha = clamp01(static_cast< f32 >(rSlot.frame) / cInFrames);
            const s32 out = rSlot.frame - (cInFrames + cHoldFrames);
            if (out > 0) {
                alpha *= 1.0f - clamp01(static_cast< f32 >(out) / cOutFrames);
            }
            mLine[i]->mAlpha = toAlpha(alpha);
            mLine[i]->SetVisible(true);
        }
    }

    bool mBuilt;
    NebulaPaneKit mTextKit;    // InformationWindow: the message font
    NebulaPaneKit mWindowKit;  // SysInfoWindowMini: the window with a ring
    Kits mKits;
    Pane* mRoot;
    Pane* mLine[cLines];
    Window* mWindow[cLines];
    TextBox* mText[cLines];
    f32 mWidth[cLines];
    bool mAny;
};

namespace {
    // This scene's notice; null when the scene has none. Every game scene sets it (make), so it never names the last
    // scene's.
    NebulaModLogNoticeScreen* sScreen = nullptr;
    // The frame the notice last stepped. Its lines count frames shown, not world steps (services-time 1, item 4: Nebula's
    // own screens are never moved by the world clock): a frame of several steps steps it once, a held world (no scene
    // update) steps it from the draw.
    u32 sSteppedFrame = 0;

    void stepOncePerFrame() {
        const u32 frame = NebulaFrameClock::count();
        if (frame != sSteppedFrame) {
            sSteppedFrame = frame;
            sScreen->step();
        }
    }
};  // namespace

namespace NebulaModLogNotice {
    void make() {
        sScreen = nullptr;
        if (luma::modlog::wanted()) {
            sScreen = new NebulaModLogNoticeScreen();
            sScreen->initWithoutIter();
        }
        // No mod, nothing said: no actor (contract 5). A harness run reads which.
        if (luma::modlog::facts()) {
            char json[64];
            nebula_snprintf(json, sizeof(json), "{\"allocated\":%s,\"notice\":%s}", luma::modlog::allocated() ? "true" : "false",
                            sScreen != nullptr ? "true" : "false");
            luma::modlog::fact("modlog.state", json);
        }
    }

    void update() {
        if (sScreen != nullptr) {
            stepOncePerFrame();
        }
    }

    void draw() {
        if (sScreen != nullptr) {
            stepOncePerFrame();
            sScreen->drawScreen();
        }
    }
};  // namespace NebulaModLogNotice
