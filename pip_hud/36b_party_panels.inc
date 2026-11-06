// =================================================================================
// PARTY HEALTH / ATB PANEL PER PLAYER (the author 2026-10-23: "Copy the game's widget so each player sees the party
// panel. This should be an option ... a drop-down box in settings. The version we currently use is called shared
// health/ATB. The new one will be called individual health/ATB"; "Each player should see their individual line not the
// entire thing. Or actually make that an option as well ... they can see all or just their own.")
//  [PictureInPicture] PartyPanel = Shared (the game's one panel at the bottom right of the screen, as before) / Individual
//  [PictureInPicture] PartyPanelRows = All / Own / OwnAndAI (Individual only: every party member, just that player's character,
//    or that player's character + the AI-played teammates)
// Individual, during a battle while the split screen shows:
//  - players 2 / 3: their own command system already has its own party panel (bcm2+0x2F8, Status_C - kept Collapsed
//    since v16; passes/12). It is shown (the main panel's visibility) and moved to the bottom right of their window
//    (SetRenderTranslation, like their command menu).
//  - player 1: the game's panel moves to the bottom right of player 1's part of the screen.
//  - Own / OwnAndAI: Status_C's rows Status_00 / 01 / 02 (+0x380 / 388 / 390, Gauge_Cell_C). f1.134: the panel's lowest
//    row is its owner's (f1.133's name match via the game's enemy-name function never worked - no names for party
//    members); Txt_Name (+0x918, EndTextBlock Text +0x118) tells the other players' rows apart (OwnAndAI). Rows not
//    wanted are collapsed; the owner's row stays at the bottom. Owner's row not known (no screen positions) = all rows.
// Everything goes back (translation 0, rows visible) when the mode is off / the split screen hides / out of battle.
// PARTYHUD log lines. (Unknown until tested: whether a player 2 / 3 panel keeps its numbers up to date - the own command
// system runs every frame in battle, so it should.)
// =================================================================================
static void* MainSide() { unsigned char* b = MainBcm(); return b ? ResolveWeak(*(int*)(b + 0x2F8), *(int*)(b + 0x2FC)) : nullptr; }
static int g_PartyLogs = 0;
#define PARTYLOG(...) do { if (g_PartyLogs < 60) { ++g_PartyLogs; CoopLog(__VA_ARGS__); } } while (0)
// player 2 / 3 (state loaded): their own panel should show now
static bool PartyIndivNow()
{
    return g_Cfg.partyIndiv && g_Player->num >= 2 && g_Player->num <= 3 && PipShowing() && !g_InCutscene && !g_ScriptedShot && !g_GamePaused
        && g_Player->ownBcmOn && g_Player->ownBcm && *(int*)(g_Player->ownBcm + BCM_UI_STATE) >= BCM_UI_BATTLE;
}
// the visibility player 2 / 3's own panel should have (passes/12, every update of their command system): the main panel's
// when Individual is on now, else Collapsed (as before)
// (ownPanelShown in each player's record - we showed / moved this player's own panel; PartyOwnTidy undoes it)
static unsigned char PartyOwnVis()
{
    if (!PartyIndivNow()) return 1;
    void* m = MainSide();
    unsigned char v = m ? ((unsigned char*)m)[0xB7] : 4;
    if (v != 1) g_Player->ownPanelShown = true;
    return v;
}
// the rows WE collapsed (only those are shown again - a row the game hides itself, e.g. the empty third row of a two-person
// party, stays as the game has it)
struct PartyHidden { int idx = -1, ser = 0; };
static PartyHidden g_PartyHid[12];
static bool PartyHidMark(void* w, bool add)   // add: remember; !add: forget - returns whether it was remembered
{
    int i, s; if (!MakeWeak(w, &i, &s)) return false;
    for (auto& h : g_PartyHid) if (h.idx == i && h.ser == s) { if (!add) h.idx = -1; return true; }
    if (add) for (auto& h : g_PartyHid) if (h.idx < 0 || !ResolveWeak(h.idx, h.ser)) { h.idx = i; h.ser = s; break; }
    return false;
}
// (the author: "Needs to have three options - character only, character and AI characters, whole party"): is this
// party member played by a person right now? (player 1's, the loaded player's if they're in, the other player's if in)
static bool PartyTypeHuman(int t)
{
    unsigned char* pm = PartyMgr(); if (!pm || t < 0) return false;
    if (t == LeaderType(pm)) return true;
    if (g_Player->joined && t == g_Player->charType) return true;
    return g_OtherType >= 0 && t == g_OtherType;
}
// (f1.133 test, screenshot 23:19:31: every panel showed the whole party with Own and OwnAndAI; log: "no row named
// \"?\"" - the game's enemy-name function gives party members no name). Rows are now told apart by the panel itself: each
// panel's BOTTOM row (the big one with HP / MP / ATB) is its owner's character (player 1's in the game's panel, player 2's /
// 3's in theirs - f1.121 made their own character "the leader" there). Each panel's bottom-row name is remembered with the
// owner's character type, so the other human players' rows are found by name (Txt_Name, the text the row shows).
struct PanelOwner { int type = -1; wchar_t name[32] = L""; };
static PanelOwner g_PanelOwner[4];   // [0] player 1's (the game's panel), [2] / [3] players 2 / 3
static bool RowName(void* row, wchar_t* out, int cap)
{
    out[0] = 0;
    if (!row || !Readable((unsigned char*)row + 0x918, 8)) return false;
    unsigned char* tn = *(unsigned char**)((unsigned char*)row + 0x918);   // Gauge_Cell_C Txt_Name (EndTextBlock)
    if (!tn || !Readable(tn + 0x118, 0x10)) return false;
    const wchar_t* t = *(const wchar_t**)(tn + 0x118); int n = *(int*)(tn + 0x120);   // EndTextBlock Text (FString)
    if (!t || n <= 1 || n > 64 || !Readable(t, (size_t)n * 2)) return false;
    int k = n - 1 < cap - 1 ? n - 1 : cap - 1; memcpy(out, t, k * 2); out[k] = 0;
    return out[0] != 0;
}
// f1.243 (the author, Reno & Rude 19:32:42 screenshot: player 2's bar said "Aerith" - not in the party any more - with Tifa's
// numbers; "Maybe because I loaded from the Eligor save area, it kept it in memory?"): players 2 / 3's own command system
// (and its party panel) lives for the whole game session; its rows' names are set from the party it was made with, and a
// save load with another party doesn't renew them (the game renews its own panel). The rows' names are text keys
// ("$menu_partyname_0003_0002" = type 3, Aerith); the numbers come from the live characters. The log (f1.144 - f1.242):
// their panel's rows are the party order turned so their own is at the bottom - Status_00 / 01 / 02 = order slots
// own+1 / own+2 / own (3 people). A row whose key names another character gets the right number (UEndTextBlock SetText).
static void RowFixName(void* row, int wantType, const char* who, int idx)
{
    wchar_t cur[64];
    if (wantType < 0 || wantType > 99 || !RowName(row, cur, 64)) return;
    static const wchar_t pfx[] = L"$menu_partyname_";
    const size_t pl = sizeof(pfx) / sizeof(wchar_t) - 1;
    if (wcsncmp(cur, pfx, pl) || wcslen(cur) < pl + 4) return;
    int have = 0;
    for (size_t k = 0; k < 4; ++k) { wchar_t c = cur[pl + k]; if (c < L'0' || c > L'9') return; have = have * 10 + (c - L'0'); }
    if (have == wantType) return;
    wchar_t nw[64]; wcscpy(nw, cur);
    nw[pl] = L'0'; nw[pl + 1] = L'0'; nw[pl + 2] = (wchar_t)(L'0' + wantType / 10); nw[pl + 3] = (wchar_t)(L'0' + wantType % 10);
    unsigned char* tn = *(unsigned char**)((unsigned char*)row + 0x918);   // Gauge_Cell_C Txt_Name (EndTextBlock)
    static void* fn = nullptr; static int st = 0, pIn = -1;
    if (st == 0)
    {
        st = -1; fn = tn ? FindFunc(tn, "SetText") : nullptr; pIn = fn ? ParamOff(fn, "InText") : -1;
        if (fn && pIn >= 0 && pIn + 16 <= 32) st = 1;
        CoopLog("PARTYHUD: EndTextBlock SetText %p, InText +%d%s", fn, pIn, st == 1 ? "" : " - NOT usable (a stale name on players 2 / 3's bars stays)");
    }
    if (st != 1) return;
    struct { wchar_t* d; int n; int m; } fs = {nw, (int)wcslen(nw) + 1, (int)wcslen(nw) + 1};
    alignas(16) unsigned char b[32] = {};
    memcpy(b + pIn, &fs, 16);
    Orig_ProcessEvent(tn, fn, b);   // (the game copies the string - ours stays ours)
    static LogRate rate;
    char a[64], c2[64]; WideCharToMultiByte(CP_UTF8, 0, cur, -1, a, 64, nullptr, nullptr); WideCharToMultiByte(CP_UTF8, 0, nw, -1, c2, 64, nullptr, nullptr);
    if (LogOk(rate, 20)) CoopLog("PARTYHUD: %s panel - row Status_0%d said \"%s\", its character is type %d -> \"%s\" (a stale name - the party changed since their command system was made)", who, idx, a, wantType, c2);
}
// a row's see-through-ness (the root user widget's ColorAndOpacity alpha; the color itself is kept)
static void RowAlpha(void* row, float a)
{
    if (!row || !ColFind(row)) return;
    const float* c = (const float*)((unsigned char*)row + 0x100);   // UUserWidget ColorAndOpacity
    if (!Readable(c, 16) || fabsf(c[3] - a) < 0.001f) return;
    float n[4] = {c[0], c[1], c[2], a};
    SetWidgetColor(row, n);
}
// Closing the gap a hidden MIDDLE row leaves (f1.139 - f1.141; the author 2026-10-25: always on, no setting): a row we keep
// that has see-through rows between it and the owner's row moves down by one row step per hidden row.
//  - f1.139 (render translation of the row) did NOT show; f1.140 try 2 (render translation of the row's root widget) did NOT
//    move either. RE: the rows sit in an EndCanvasPanel (Status_C WidgetTree.All_Status, one EndCanvasPanelSlot per row with
//    the game's own XAngle / YAngle tilt) - apparently it ignores render transforms below it.
//  - f1.140 try 1 WORKS (test 06:20:22: "row Status_00 moved 74 px on screen (wanted 67)"; the author: "This seemed to work
//    correctly"): the row's EndCanvasPanelSlot position (UWidget Slot +0x28; LayoutData.Offsets Left / Top +0x38 / +0x3C;
//    SetPosition 0x1356480 writes them and the Slate slot) - Top + the shift.
//  - f1.141 fixes: (1) the rows' unmoved places are kept RELATIVE TO THE OWNER'S ROW (never moved by us) - f1.140 kept screen
//    positions, which broke when the whole panel moved (06:20:25: player 3 left, player 2's window got the whole half ->
//    "moved 639 px (wanted 89)", the row went 89 px instead of 67); (2) slot units -> pixels with the PANEL's scale
//    (the row's own scale gave 74 px for 67), then calibrated from the measured movement; (3) a row we moved can't be
//    picked as the owner's row.
// A few frames after a move the real movement is measured and logged ("row Status_0N moved X px (wanted Y) - gap closed /
// NOT closed"), then re-checked every ~3 s (logged when the result changes). When the game itself changes the slot's Top,
// its new value is the base. Not needed: put back to the base.
struct GapRow
{
    void* row = nullptr; void* slot = nullptr;  // the row; its EndCanvasPanelSlot
    bool on = false;                            // our move is applied
    float orig = 0.f, set = 0.f;                // the game's slot Top, the Top we set
    float rel = -1e9f;                          // the row's bottom minus the owner's row's bottom, without our move (pixels / panel scale)
    float wantPx = 0.f; int checkIn = 0;        // the move asked for; frames until the on-screen check
    int recheck = 0, verdict = -1;              // frames until the next check (every ~3 s while on); the last result logged
    int settle = 0;                             // frames after putting it back while the screen positions still show the move
};
static GapRow g_Gap[4][3];
static float g_GapCal[4] = {1.f, 1.f, 1.f, 1.f};   // measured pixels per unit / the panel's scale (a ratio - the panel's size changes)
static int g_GapLogs = 0;
#define GAPLOG(...) do { if (g_GapLogs < 80) { ++g_GapLogs; CoopLog(__VA_ARGS__); } } while (0)
static void* g_SlotPosFn = nullptr; static int g_SlotPosP = -1, g_SlotPosState = 0;
static bool GapSlotPos(void* slot, float x, float y)
{
    if (g_SlotPosState == 0 && slot)
    {
        void* f = FindFunc(slot, "SetPosition"); int pp = f ? ParamOff(f, "InPosition") : -1;
        g_SlotPosState = (f && pp >= 0 && ParmsSize(f) <= 32) ? 1 : -1;
        if (g_SlotPosState == 1) { g_SlotPosFn = f; g_SlotPosP = pp; }
        GAPLOG("PARTYHUD: party row slot SetPosition %s", g_SlotPosState == 1 ? "found (gaps can be closed)" : "NOT found - a hidden middle row leaves a gap");
    }
    if (g_SlotPosState != 1) return false;
    alignas(16) unsigned char b[32] = {}; float v[2] = {x, y}; memcpy(b + g_SlotPosP, v, 8);
    Orig_ProcessEvent(slot, g_SlotPosFn, b);
    return true;
}
static float* GapTop(GapRow& g) { return (float*)((unsigned char*)g.slot + 0x3C); }   // LayoutData.Offsets.Top
static void GapSet(GapRow& g, float y) { GapSlotPos(g.slot, ((float*)((unsigned char*)g.slot + 0x38))[0], y); }
static void GapUndo(GapRow& g)
{
    if (!g.on) return;
    g.on = false; g.wantPx = 0.f; g.checkIn = 0; g.settle = 3;   // (the screen positions lag a frame or two)
    if (!g.slot || !Readable(g.slot, 0x48)) return;
    if (fabsf(*GapTop(g) - g.set) < 0.01f) GapSet(g, g.orig);   // (the game's own newer value stays)
}
// the row's EndCanvasPanelSlot, or null (logged once per row widget when it isn't one)
static void* GapSlot(void* row, const char* who, int i)
{
    unsigned char* r = (unsigned char*)row; void* t = Readable(r + 0x28, 8) ? *(void**)(r + 0x28) : nullptr;   // UWidget Slot
    char cn[96] = "?";
    if (t && Readable(t, 0x70)) ObjClassName(t, cn, sizeof(cn)); else t = nullptr;
    if (t && strcmp(cn, "EndCanvasPanelSlot")) t = nullptr;   // only the game's canvas slot (SetPosition is looked up on it once)
    if (!t)
    {
        static void* logged[12] = {}; static int nl = 0; bool seen = false;
        for (int k = 0; k < nl; ++k) if (logged[k] == row) seen = true;
        if (!seen) { if (nl < 12) logged[nl++] = row; GAPLOG("PARTYHUD: %s panel - row Status_0%d's slot is %s, not an EndCanvasPanelSlot - its gap stays", who, i, cn); }
    }
    return t;
}
// pscale = the panel's pixels per unit (HcRect), 0 if unknown
static void GapFix(void** rows, int slot, const float* bot, float pscale, const bool* hideRow, int own, bool filter, const char* who)
{
    int si = slot & 3;
    float ownBot = own >= 0 ? bot[own] : -1e9f;
    float base[3], step = 0.f;   // the rows' bottoms without our move, relative to the owner's row
    for (int i = 0; i < 3; ++i)
    {
        GapRow& g = g_Gap[si][i];
        if (g.row != rows[i]) { g = GapRow(); g.row = rows[i]; }   // another widget now - nothing of ours on it
        if (g.settle > 0) --g.settle;
        else if (!g.on) g.rel = (bot[i] > -1e8f && ownBot > -1e8f && pscale > 0.f) ? (bot[i] - ownBot) / pscale : -1e9f;   // moved rows keep their unmoved place
        base[i] = g.rel > -1e8f && pscale > 0.f ? g.rel * pscale : -1e9f;   // (kept per unit of the panel's scale - its size can change)
    }
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        if (i != j && base[i] > -1e8f && base[j] > -1e8f && base[j] > base[i] + 1.f && (step <= 0.f || base[j] - base[i] < step)) step = base[j] - base[i];
    float ppu = pscale * g_GapCal[si];
    for (int i = 0; i < 3; ++i)
    {
        GapRow& g = g_Gap[si][i];
        if (!rows[i]) continue;
        float px = 0.f;
        if (filter && !hideRow[i] && i != own && own >= 0 && base[i] > -1e8f && base[own] > -1e8f && step > 1.f && ppu > 0.f)
        {
            int n = 0;
            for (int j = 0; j < 3; ++j) if (rows[j] && hideRow[j] && base[j] > base[i] + 1.f && base[j] < base[own] - 1.f) ++n;
            px = n * step;
        }
        if (px < 0.5f) { if (g.on) { GapUndo(g); GAPLOG("PARTYHUD: %s panel - row Status_0%d back in its place (no gap to close)", who, i); } continue; }
        if (!g.on)
        {
            g.slot = GapSlot(rows[i], who, i);
            if (!g.slot) continue;
            g.orig = g.set = *GapTop(g);
        }
        else if (!Readable(g.slot, 0x48)) { g = GapRow(); g.row = rows[i]; continue; }
        else if (fabsf(*GapTop(g) - g.set) > 0.01f)
        {   // the game set its own Top meanwhile - that's the new base (the row's unmoved place moved with it)
            float nv = *GapTop(g);
            if (g.rel > -1e8f) g.rel += (nv - g.orig) * g_GapCal[si];
            static int logs = 0; if (logs < 10) { ++logs; GAPLOG("PARTYHUD: %s panel - row Status_0%d: the game moved the row itself (Top %.1f -> %.1f) - kept as the new base", who, i, g.orig, nv); }
            g.orig = nv;
        }
        float y = g.orig + px / ppu;
        if (!g.on || fabsf(*GapTop(g) - y) > 0.01f) { GapSet(g, y); g.set = y; }
        g.on = true;
        if (fabsf(px - g.wantPx) > 0.5f)
        {
            GAPLOG("PARTYHUD: %s panel - row Status_0%d: moving it %.0f px down over the hidden row(s) (Top %.1f -> %.1f)", who, i, px, g.orig, y);
            g.wantPx = px; g.checkIn = 4;
        }
        bool first = g.checkIn > 0 && --g.checkIn == 0;
        if (first || (g.checkIn == 0 && --g.recheck <= 0))
        {   // the real movement (last frame's geometry, relative to the owner's row); again every ~3 s, logged when it changes
            g.recheck = 180;
            bool ok = bot[i] > -1e8f && ownBot > -1e8f && base[i] > -1e8f;
            float moved = ok ? (bot[i] - ownBot) - base[i] : 0.f;
            int v = !ok ? 3 : fabsf(moved - px) < 0.15f * px + 2.f ? 0 : fabsf(moved) < 2.f ? 1 : 2;
            if (first || v != g.verdict)
            {
                if (ok) GAPLOG("PARTYHUD: %s panel - row Status_0%d moved %.0f px (wanted %.0f) - %s", who, i, moved, px,
                    v == 0 ? "gap closed" : v == 1 ? "did NOT move - gap NOT closed" : "moved by a different amount - recalibrating");
                else GAPLOG("PARTYHUD: %s panel - row Status_0%d: its movement can't be measured", who, i);
            }
            if (ok && fabsf(moved) > 5.f && fabsf(g.set - g.orig) > 1.f)
            {   // calibrate the pixels per unit from what really happened (kept within 0.5x - 2x of the panel's scale)
                float c = moved / (g.set - g.orig);
                if (pscale > 0.f && c > 0.5f * pscale && c < 2.f * pscale)
                {
                    if (v == 2) GAPLOG("PARTYHUD: %s panel - pixels per unit %.3f -> %.3f (measured)", who, ppu, c);
                    g_GapCal[si] = c / pscale;
                    if (v == 2) g.wantPx = -1.f;   // re-place it with the measured value next frame
                }
            }
            g.verdict = v;
        }
    }
}
// rows (mode 0 = all, 1 = only the owner's, 2 = the owner's + the AI-played teammates'): collapse the rows not wanted, or
// show them all again (mode 0). slot = 0 (player 1's panel) / 2 / 3; ownerType = that player's character type. Returns the
// vertical shift (pixels, + = down) that puts the lowest remaining row at the bottom of the panel, 0 if none.
static float g_PanelScaleNow[4] = {1.f, 1.f, 1.f, 1.f};   // [0] the game's panel, [2] / [3] players 2 / 3's
static float g_P1RowLiftPx = 0.f;   // f1.326 (see SummonGaugeFollow): player 1's top shown row below the panel's top row, screen px
static float PartyRows(void* panel, int slot, int ownerType, int mode, const char* who)
{
    {   // f1.243: the PARTYHUD lines (60 per session) start over each battle
        static bool wasB = false; bool nb = InBattleNow();
        if (nb && !wasB) g_PartyLogs = 0;
        wasB = nb;
    }
    unsigned char* p = (unsigned char*)panel;
    if (!p || !Readable(p + 0x380, 0x18)) return 0.f;
    void* rows[3]; wchar_t nm[3][32]; float bot[3] = {-1e9f, -1e9f, -1e9f}; int own = -1;
    bool hc = g_Hc.state == 1;
    for (int i = 0; i < 3; ++i)
    {
        rows[i] = *(void**)(p + 0x380 + i * 8);   // Status_C Status_00 / 01 / 02 (Gauge_Cell_C)
        if (!rows[i] || !Readable((unsigned char*)rows[i] + 0x918, 8)) { rows[i] = nullptr; nm[i][0] = 0; continue; }
        RowName(rows[i], nm[i], 32);
        float rx, ry, rw, rh, sc;
        if (hc && ((unsigned char*)rows[i])[0xB7] != 1 && HcRect(rows[i], &rx, &ry, &rw, &rh, &sc) && rh > 1.f) bot[i] = ry + rh;
    }
    for (int pass = 0; pass < 2 && own < 0; ++pass)   // the lowest showing row = the owner's (first not counting rows we moved)
        for (int i = 0; i < 3; ++i)
        {
            const GapRow& g = g_Gap[slot & 3][i];
            if (pass == 0 && g.row == rows[i] && (g.on || g.settle > 0)) continue;
            if (bot[i] > -1e8f && (own < 0 || bot[i] > bot[own])) own = i;
        }
    int si = slot & 3;
    if ((slot == 2 || slot == 3) && ownerType >= 0 && own >= 0)
    {   // f1.243: names from the party the command system was made with -> the current party's
        unsigned char* pm = PartyMgr(); int n = PartyCount(), os = -1;
        for (int k = 0; k < n; ++k) if (OrderType(pm, k) == ownerType) os = k;
        if (os >= 0)
        {
            if (rows[own]) { RowFixName(rows[own], ownerType, who, own); RowName(rows[own], nm[own], 32); }
            if (n == 3 && own == 2) for (int i = 0; i < 2; ++i) if (rows[i]) { RowFixName(rows[i], OrderType(pm, (os + 1 + i) % 3), who, i); RowName(rows[i], nm[i], 32); }
        }
    }
    if (own >= 0 && nm[own][0] && ownerType >= 0)
    {   // remember this panel owner's name (and log once per change: the rows' order + which Status_0X is theirs)
        bool chg = g_PanelOwner[si].type != ownerType || wcscmp(g_PanelOwner[si].name, nm[own]);
        g_PanelOwner[si].type = ownerType; wcscpy(g_PanelOwner[si].name, nm[own]);
        if (chg) { char a[3][40]; for (int i = 0; i < 3; ++i) { a[i][0] = 0; if (nm[i][0]) WideCharToMultiByte(CP_UTF8, 0, nm[i], -1, a[i], 40, nullptr, nullptr); else strcpy(a[i], "-"); }
            PARTYLOG("PARTYHUD: %s panel - rows Status_00 \"%s\" / 01 \"%s\" / 02 \"%s\"; theirs (lowest) = Status_0%d (type %d)", who, a[0], a[1], a[2], own, ownerType); }
    }
    bool hideRow[3] = {};
    bool filter = mode && own >= 0;
    if (filter)
        for (int i = 0; i < 3; ++i)
        {
            if (!rows[i] || i == own) continue;
            if (mode == 1) { hideRow[i] = true; continue; }
            for (int k = 0; k < 4; ++k)   // mode 2: another human player's row
                if (k != si && g_PanelOwner[k].type >= 0 && g_PanelOwner[k].type != ownerType && g_PanelOwner[k].name[0]
                    && PartyTypeHuman(g_PanelOwner[k].type) && nm[i][0] && !_wcsicmp(nm[i], g_PanelOwner[k].name)) hideRow[i] = true;
        }
    if (mode && !filter)
    {
        static ULONGLONG last = 0; ULONGLONG now = GetTickCount64();
        if (now - last > 10000) { last = now; PARTYLOG("PARTYHUD: %s panel - its own row isn't known yet (%s) - showing all rows", who, hc ? "no row placed on screen" : "screen positions not ready"); }
    }
    for (int i = 0; i < 3; ++i)
    {   // (f1.135 test 23:37:14 screenshot, Own: every row still showed although the log said "rows shown: --2"): the
        // game sets the rows' visibility back itself, so Collapsed didn't stick. They are now made fully see-through instead
        // (UUserWidget ColorAndOpacity alpha 0, every frame) - the layout stays, the owner's row is the bottom one anyway.
        if (!rows[i]) continue;
        if (filter && hideRow[i]) { RowAlpha(rows[i], 0.f); PartyHidMark(rows[i], true); }
        else if (PartyHidMark(rows[i], false)) RowAlpha(rows[i], 1.f);   // only rows we hid
    }
    {   // close the gap a hidden middle row leaves (always on)
        float px0, py0, pw0, ph0, psc = 0.f;
        if (!(hc && HcRect(panel, &px0, &py0, &pw0, &ph0, &psc))) psc = 0.f;
        GapFix(rows, slot, bot, psc, hideRow, own, filter, who);
    }
    if (slot == 0)
    {   // f1.326: how far player 1's top shown row sits below the panel's top row (the summon gauge's anchor) - screen px
        int top = -1; for (int i = 0; i < 3; ++i) if (rows[i] && !(filter && hideRow[i])) { top = i; break; }
        g_P1RowLiftPx = (filter && top > 0 && rows[0] && bot[0] > -1e8f && bot[top] > -1e8f) ? bot[top] - bot[0] : 0.f;
        // f1.328 (code review #5): PartyPanelScale shrinks the panel toward its bottom-right corner, so its top row (the
        // gauge's anchor) sits lower than the game placed the gauge: + (panel bottom - top row's bottom) x (1/scale - 1)
        float sc0 = g_PanelScaleNow[0], ax, ay, aw, ah, asc;
        if (sc0 > 0.3f && sc0 < 0.999f && rows[0] && bot[0] > -1e8f && hc && HcRect(panel, &ax, &ay, &aw, &ah, &asc))
            g_P1RowLiftPx += (ay + ah - bot[0]) * (1.f / sc0 - 1.f);
    }
    if (!filter) return 0.f;
    {   // log the pick once per change
        static int lastSig[4] = {-1, -1, -1, -1}; int sig = mode | (hideRow[0] << 2) | (hideRow[1] << 3) | (hideRow[2] << 4) | (own << 5);
        if (sig != lastSig[si]) { lastSig[si] = sig; PARTYLOG("PARTYHUD: %s panel - rows shown: %s%s%s (%s)", who, hideRow[0] || !rows[0] ? "-" : "0", hideRow[1] || !rows[1] ? "-" : "1", hideRow[2] || !rows[2] ? "-" : "2", mode == 1 ? "Own" : "OwnAndAI"); }
    }
    // the lowest remaining row is theirs, already at the bottom - no shift needed (kept as 0; the rows above it just go)
    return 0.f;
}
// (the author: "can we have a scaling option for this in the settings? Minimum probably 50%, maximum 100"):
// [PictureInPicture] PartyPanelScale 50 - 100 (Individual only; + PartyPanelScaleThird / PartyPanelScaleQuarter). UWidget SetRenderScale + SetRenderTransformPivot (1, 1) -
// the panel shrinks toward its bottom-right corner, which stays where the translation puts it. 100 = pivot back to the
// middle (the engine's default 0.5, 0.5) and scale 1. The applied scale is remembered per panel: HcRect's pixels-per-unit
// includes it, so the translation divides it out again.
static void* g_ScaleFn = nullptr; static void* g_PivotFn = nullptr; static int g_ScaleP = -1, g_PivotP = -1, g_ScaleState = 0;
// (g_PanelScaleNow: declared above PartyRows - f1.328)
static bool PanelScaleFind(void* w)
{
    if (g_ScaleState) return g_ScaleState == 1;
    if (!w) return false;
    void* f = FindFunc(w, "SetRenderScale"); void* fp = FindFunc(w, "SetRenderTransformPivot");
    int p = f ? ParamOff(f, "Scale") : -1, pp = fp ? ParamOff(fp, "Pivot") : -1;
    g_ScaleState = (f && fp && p >= 0 && pp >= 0 && ParmsSize(f) <= 32 && ParmsSize(fp) <= 32) ? 1 : -1;
    if (g_ScaleState == 1) { g_ScaleFn = f; g_PivotFn = fp; g_ScaleP = p; g_PivotP = pp; }
    PARTYLOG("PARTYHUD: panel scaling %s", g_ScaleState == 1 ? "ready (SetRenderScale + SetRenderTransformPivot)" : "NOT available - PartyPanelScale does nothing");
    return g_ScaleState == 1;
}
static void PanelScale(void* w, int slot, float sc)   // sc 0.5 - 1
{
    if (!w || !PanelScaleFind(w)) return;
    float& now = g_PanelScaleNow[slot & 3];
    alignas(16) unsigned char b[32] = {}; float piv[2] = {sc < 0.999f ? 1.f : 0.5f, sc < 0.999f ? 1.f : 0.5f}, v[2] = {sc, sc};
    memcpy(b + g_PivotP, piv, 8); Orig_ProcessEvent(w, g_PivotFn, b);
    alignas(16) unsigned char c[32] = {}; memcpy(c + g_ScaleP, v, 8); Orig_ProcessEvent(w, g_ScaleFn, c);   // (every frame, like the translation)
    if (fabsf(now - sc) > 0.001f)
    {
        static int logs = 0; if (logs < 20) { ++logs; PARTYLOG("PARTYHUD: %s party panel at %.0f%% size (PartyPanelScale)", slot == 0 ? "the game's" : slot == 3 ? "player 3's own" : "player 2's own", sc * 100.f); }
        now = sc;
    }
}
// (the author: "the scaling option needs to be delineated between 1/2 window, 1/3 window, and 1/4 window. Same issue as we
// saw with the reticule, the HUD aspects can look larger in the 1/4 window"): frac = the view's share of the screen (its width x
// height / the screen's). Like the reticle (marker/19): under 0.3 = a quarter, under 0.42 = a third, else a half.
static float PartyScaleWanted(float frac)
{
    int p = frac < 0.3f ? g_Cfg.partyScale4 : frac < 0.42f ? g_Cfg.partyScale3 : g_Cfg.partyScale;
    if (p < 50) p = 50;
    if (p > 100) p = 100;
    return p / 100.f;
}
// player 2 / 3 (state loaded), from PipDraw: their own panel to the bottom right of their window
static void PartyPanelIntoWindow(void* canvas, float clipX, float clipY)
{
    if (!PartyIndivNow()) return;
    void* side = Bcm2Side(); if (!side) return;
    if (g_Hc.state == 0 && !HcResolve(canvas)) return;
    if (!SetTrFind(side)) return;
    float wx, wy, ww, wh; PipRect(clipX, clipY, &wx, &wy, &ww, &wh);
    float s = clipY / 1080.f, x, y, rw, rh, sc;
    if (g_Hc.state == 1 && HcRect(side, &x, &y, &rw, &rh, &sc) && sc > 0.f) s = sc / g_PanelScaleNow[g_Player->num & 3];   // without our scale
    PanelScale(side, g_Player->num, PartyScaleWanted(clipX * clipY > 1.f ? ww * wh / (clipX * clipY) : 1.f));   // f1.135; by the window's size
    float shift = PartyRows(side, g_Player->num, g_Player->charType, g_Cfg.partyRows, g_Player->num == 3 ? "player 3's" : "player 2's");
    float nud = SplitThirds() ? g_Cfg.thirdsPanelNudge * clipY / 1080.f : 0.f;   // ThirdsPanelNudge (a little to the right)
    float tx = (wx + ww - clipX + nud) / s, ty = (wy + wh - clipY + shift) / s;
    if (g_HudSpanOk && g_GameHwnd && g_HudSpanFit > 0.01f)
    {   // f1.305 (the author, f1.304 test 13:34: "Tifa's health/ATB is in P1's screen, and P2 doesn't have one at all"):
        // over all monitors the HUD's 1920x1080 units ARE player 1's monitor (31b) - the panel's bottom right goes from
        // there to their window's bottom right (window px / fit; the panel scale as for a whole screen)
        RECT cr{}; GetClientRect(g_GameHwnd, &cr);
        float kx = cr.right > 0 ? (float)cr.right / clipX : 1.f, ky = cr.bottom > 0 ? (float)cr.bottom / clipY : 1.f;
        PanelScale(side, g_Player->num, PartyScaleWanted(1.f));
        tx = ((wx + ww) * kx - (g_HudSpanX + g_HudSpanW)) / g_HudSpanFit;
        ty = ((wy + wh) * ky - (g_HudSpanY + g_HudSpanH) + shift) / g_HudSpanFit;
    }
    static float lastT[4][2] = {{1e9f, 1e9f}, {1e9f, 1e9f}, {1e9f, 1e9f}, {1e9f, 1e9f}};
    int pi = g_Player->num & 3;
    if (fabsf(lastT[pi][0] - tx) > 0.5f || fabsf(lastT[pi][1] - ty) > 0.5f)
    {
        lastT[pi][0] = tx; lastT[pi][1] = ty;
        PARTYLOG("PARTYHUD: player %d's own party panel to their window (%.0f, %.0f px%s) - visibility %d", g_Player->num, wx + ww - clipX, wy + wh - clipY + shift,
            g_Cfg.partyRows == 1 ? ", their row only" : g_Cfg.partyRows == 2 ? ", their row + AI teammates'" : "", (int)((unsigned char*)side)[0xB7]);
    }
    SetRenderTr(side, tx, ty);   // (every frame: the game may set it back)
    g_Player->ownPanelShown = true;
}
// (the author, f1.125 test 22:20:29 screenshot: after the fight player 2's own panel stayed up at the top right and
// player 3's - left at 22:19:34 - stayed over the game's panel at the bottom right, rows drawn over each other): their
// panel was only hidden again from inside their own command system's update (passes/12 PartyOwnVis), which stops running
// when the battle ends / the player leaves. Now once a frame for players 2 and 3 (state loaded; pip_hud/37 PipDrawUnderHud,
// whether or not their window or command system runs): shown / moved by us but not wanted now -> Collapsed, translation 0,
// the rows we hid shown again.
static void PartyOwnTidy()
{
    int pi = g_Player->num & 3;
    if (!g_Player->ownPanelShown || PartyIndivNow()) return;
    g_Player->ownPanelShown = false;
    void* side = Bcm2Side();
    if (!side || !Readable(side, 0xB8)) { PARTYLOG("PARTYHUD: player %d's own party panel - gone already (nothing to hide)", g_Player->num); return; }
    PartyRows(side, g_Player->num, -1, 0, g_Player->num == 3 ? "player 3's" : "player 2's");
    if (SetTrFind(side)) SetRenderTr(side, 0.f, 0.f);
    if (g_PanelScaleNow[pi] != 1.f) PanelScale(side, g_Player->num, 1.f);
    if (((unsigned char*)side)[0xB7] != 1) WidgetSetVis(side, 1);
    PARTYLOG("PARTYHUD: player %d's own party panel hidden again (%s)", g_Player->num, !g_Cfg.partyIndiv ? "Shared" : !g_Player->ownBcmOn ? "their command system is off - battle over / left"
        : !PipShowing() ? "split screen hidden" : (g_InCutscene || g_ScriptedShot) ? "cutscene" : g_GamePaused ? "paused" : "not in battle");
}
// once a frame (player 2's state loaded), from PipDrawUnderHud: the game's panel into player 1's part of the screen, or back
// f1.322 (the author: "Double check for any other parts of the HUD that could also have that issue please"): RE of the game's
// widget names shows two anchors in each party-panel row (Status_C rows, Gauge_Cell_C): Anchor_SummonIcon and
// Anchor_HPBuff, and separately made widgets that sit at such anchors (SummonGaugeIcontWidget, SummonChantGaugeIcontWidget,
// StatusSummonIconWidget, ReverseDrainIconWidget). Anything placed that way stays behind when player 1's panel is moved.
// HUDSURVEY: once per battle (5 s after player 1's panel was moved, at most 8 battles) every visible user widget is
// listed whose box is in the area the panel came from (the bottom-right quarter of the HUD) but not in player 1's part -
// i.e. left behind. Also every widget with "Summon" in its name or class (that's how the gauge is found below).
static int g_SgIdx = -1, g_SgSer = 0;   // the summon gauge widget (weak), from the survey
// f1.326 (f1.325 run 20:01: HUDSURVEY found SummonTimeGauge_C_0 and SummonChantGauge_C_0 - both SCREEN-SIZED widgets the
// game puts at the anchor with its OWN render translation (1499,1220 here); the party rows are hidden by alpha 0, layout
// kept, so the gauge floats above two invisible rows): [0] time gauge, [1] chant gauge; g_P1RowLiftPx = how far player 1's
// top shown row is below the panel's top row (screen px, from PartyRows); g_SgLive[k] = widget + its translation now
// (for 36h: the tilt of the gauge's 3D panel stays centred on its layout spot, like the game's).
static int g_SgIdx2[2] = {-1, -1}, g_SgSer2[2] = {0, 0};
static void* g_SgLive[2] = {}; static int g_SgORT = -1;
// f1.328 (code review #1 / #3 / #4: with the default PartyPanel=Shared nothing looked for the summon gauges, so with
// the render-position tilt patch on (any split screen) their 3D panels were tilted wrong again; the survey only ran in the
// first battles and could use up its runs before a summon fight): every 3 s while split windows show and a gauge isn't
// known, the gauges' classes (SummonTimeGauge_C / SummonChantGauge_C, found by name once loaded) are matched against the
// object array by class pointer (cheap) -> weak references; each tick g_SgLive = the live widgets (null when gone).
static void SummonGaugeTrack()
{
    // f1.337 (pre-release review): the classes are kept as weak references (were raw pointers kept forever - an unloaded
    // class's address taken by another class would have made its objects "gauges")
    static int clsIdx[2] = {-1, -1}, clsSer[2] = {0, 0}; void* cls[2] = {}; static ULONGLONG next = 0; static int logs = 0;
    static const char* names[2] = {"SummonTimeGauge_C", "SummonChantGauge_C"};
    bool need = false;
    for (int k = 0; k < 2; ++k)
    {
        void* g = g_SgIdx2[k] >= 0 ? ResolveWeak(g_SgIdx2[k], g_SgSer2[k]) : nullptr;
        if (!g) { g_SgIdx2[k] = -1; need = true; }
        g_SgLive[k] = g;
        if (g && g_SgORT < 0) g_SgORT = PropOff(*(void**)((unsigned char*)g + 0x10), "RenderTransform");
    }
    if (!need || !AnyWindowShowing()) return;
    ULONGLONG now = GetTickCount64(); if (now < next) return; next = now + 3000;
    for (int k = 0; k < 2; ++k)
    {
        cls[k] = clsIdx[k] >= 0 ? ResolveWeak(clsIdx[k], clsSer[k]) : nullptr;
        if (!cls[k]) { clsIdx[k] = -1; cls[k] = FindObj(names[k], "WidgetBlueprintGeneratedClass"); if (cls[k] && !MakeWeak(cls[k], &clsIdx[k], &clsSer[k])) clsIdx[k] = -1; }
    }
    if (!cls[0] && !cls[1]) return;   // not loaded yet (no summon in this area)
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    if (!items || num <= 0) return;
    for (int i = 0; i < num; ++i)
    {
        unsigned char* it = items + (size_t)i * 24;
        unsigned char* o = *(unsigned char**)it;
        if (!o || (*(DWORD*)(it + 8) & 0x30000000) || (*(DWORD*)(o + 8) & 0x30)) continue;   // gone / class default object / archetype (f1.329: the BP class's WidgetArchetype template was taken first)
        void* c = *(void**)(o + 0x10);
        for (int k = 0; k < 2; ++k)
            if (c && c == cls[k] && g_SgIdx2[k] < 0)
            {
                int wi = -1, ws = 0;
                if (!MakeWeak(o, &wi, &ws)) continue;
                g_SgIdx2[k] = wi; g_SgSer2[k] = ws; if (k == 0) { g_SgIdx = wi; g_SgSer = ws; }
                g_SgLive[k] = o;
                if (g_SgORT < 0) g_SgORT = PropOff(c, "RenderTransform");
                if (logs < 10) { ++logs; char nm[96] = {}; NameOf(o, nm, sizeof nm); CoopLog("SUMMONGAUGE: tracking %s (its 3D panel keeps the game's own tilt)", nm); }
            }
    }
}
static void HudSurvey(float p1x, float p1y, float p1w, float p1h, float zx0, float zy0, float zx1, float zy1, bool listAll = false)
{   // listAll (f1.324): the panel only moved DOWN (all monitors, rows hidden) - list everything in its area
    // f1.325 (19:54 run: the survey ran 5 s into the battle - the summon gauge only shows up later, after the boss's phase
    // change): again every 10 s in a battle until the gauge is found (40 runs a session); each widget logged once
    static ULONGLONG due = 0; static int runs = 0; static bool wasB = false;
    static unsigned int done[128] = {}; static int doneN = 0;
    bool b = InBattleNow();
    if (b && !wasB) due = GetTickCount64() + 5000;
    wasB = b;
    if (!b || !due || GetTickCount64() < due || runs >= 12 || g_Hc.state != 1 || !g_Hc.uwCls) return;
    due = g_SgIdx2[0] < 0 ? GetTickCount64() + 10000 : 0; ++runs;   // f1.328: a log survey only (12 runs) - SummonGaugeTrack finds the gauges
    static int oVis = -2; if (oVis == -2) { void* wc = FindObj("Widget", "Class"); oVis = wc ? PropOff(wc, "Visibility") : -1; }
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    if (!items || num <= 0) return;
    int seen = 0, logged = 0, behind = 0;
    for (int i = 0; i < num && logged < 30; ++i)
    {
        unsigned char* it = items + (size_t)i * 24;
        unsigned char* o = *(unsigned char**)it;
        if (!o || (*(DWORD*)(it + 8) & 0x30000000)) continue;
        if (*(DWORD*)(o + 8) & 0x10) continue;   // class default object
        void* cls = *(void**)(o + 0x10); bool uw = false; int d = 0;
        for (void* c = cls; c && d < 16; c = *(void**)((unsigned char*)c + 0x30), ++d) if (c == g_Hc.uwCls) { uw = true; break; }
        if (!uw) continue;
        if (oVis >= 0) { unsigned char v = o[oVis]; if (v == 1 || v == 2) continue; }   // collapsed / hidden
        float x, y, w, h, sc;
        if (!HcRect(o, &x, &y, &w, &h, &sc) || w < 2.f || h < 2.f) continue;
        ++seen;
        char nm[96] = {}, cn[96] = {}; NameOf(o, nm, sizeof nm); ObjClassName(o, cn, sizeof cn);
        bool summon = strstr(nm, "Summon") || strstr(cn, "Summon");
        float mx = x + w * 0.5f, my = y + h * 0.5f;
        bool inZone = mx >= zx0 && mx < zx1 && my >= zy0 && my < zy1 && w < (zx1 - zx0) * 1.5f;
        bool inP1 = mx >= p1x && mx < p1x + p1w && my >= p1y && my < p1y + p1h;
        if (summon && (strstr(nm, "Gauge") || strstr(cn, "Gauge")) && !strstr(cn, "Effect"))
        {
            int k = (strstr(cn, "Chant") || strstr(nm, "Chant")) ? 1 : 0;
            int wi = -1, ws = 0;   // f1.328 (review #4): a real weak reference (the item's serial can still be 0)
            if (MakeWeak(o, &wi, &ws)) { g_SgIdx2[k] = wi; g_SgSer2[k] = ws; if (k == 0) { g_SgIdx = wi; g_SgSer = ws; } }
        }
        if (!(summon || (inZone && (!inP1 || listAll)))) continue;
        if (inZone && !inP1) ++behind;
        unsigned int hsh = 2166136261u; for (const char* q = nm; *q; ++q) hsh = (hsh ^ (unsigned char)*q) * 16777619u;
        bool dup = false; for (int k = 0; k < doneN; ++k) if (done[k] == hsh) { dup = true; break; }
        if (dup) continue;
        if (doneN < 128) done[doneN++] = hsh;
        char on[96] = {}; void* outer = *(void**)(o + 0x20); if (outer) NameOf(outer, on, sizeof on);
        ++logged;
        CoopLog("HUDSURVEY: %s%s (%s, in %s) at %.0f,%.0f %.0fx%.0f scale %.2f", inZone && !inP1 ? "LEFT BEHIND " : inZone ? "IN PANEL AREA " : "", nm, cn, on, x, y, w, h, sc);
    }
    CoopLog("HUDSURVEY: run %d - %d visible user widgets (summon gauge %s), %d left behind in the panel's old area (%.0f,%.0f - %.0f,%.0f) outside player 1's part (%.0f,%.0f %.0fx%.0f)",
        runs, seen, g_SgIdx >= 0 ? "found" : "not seen yet", behind, zx0, zy0, zx1, zy1, p1x, p1y, p1w, p1h);
}
// f1.322 (the author, thirds on one wide screen, boss fight: the summon gauge "is not in the center of P2's screen and it
// is slanted and all kinds of not right! Should probably show above each players health bar?"): the summon gauge is its
// own widget (UEndBattleSummonGaugeIcon - "StatusSummonIconWidget" / "SummonGaugeIcontWidget", placed at the party
// panel's Anchor_SummonIcon by LAYOUT), so moving the panel with a render translation left it behind. It now gets the
// same translation as player 1's party panel, on top of whatever translation the game gives it itself (re-read each
// frame). SUMMONGAUGE log lines (which property / class it was found through).
static void* SummonGauge(void* panel)
{
    if (g_SgIdx >= 0) { void* g = ResolveWeak(g_SgIdx, g_SgSer); if (g) return g; g_SgIdx = -1; }   // found by the survey
    static int tries = 0;
    void* owners[2] = {panel, BcmCmdC(MainBcm())};   // the party panel, player 1's Command_C (both UObjects - the command system itself is not)
    static const char* props[3] = {"StatusSummonIconWidget", "SummonGaugeIcontWidget", "SummonChantGaugeIcontWidget"};
    for (int o = 0; o < 2; ++o)
    {
        unsigned char* ob = (unsigned char*)owners[o];
        if (!ob || !Readable(ob + 0x10, 8)) continue;
        void* cls = *(void**)(ob + 0x10);
        static void* cCls[2] = {}; static int cOff[2][2] = {{-1, -1}, {-1, -1}};   // offsets per owner class (no field walk each frame)
        if (cCls[o] != cls) { cCls[o] = cls; for (int k = 0; k < 2; ++k) cOff[o][k] = PropOff(cls, props[k]); }
        for (int k = 0; k < 2; ++k)
        {
            int off = cOff[o][k];
            if (off < 0 || !Readable(ob + off, 8)) continue;
            void* g = *(void**)(ob + off);
            if (!g || !Readable((unsigned char*)g + 0x10, 8)) continue;
            char cn[96] = {}; ObjClassName(g, cn, sizeof cn);
            if (!strstr(cn, "Summon") && !strstr(cn, "summon"))
            {
                if (tries < 4) { ++tries; CoopLog("SUMMONGAUGE: %s +0x%X (%s) holds a %s - not the gauge", o ? "player 1's Command_C" : "the party panel", off, props[k], cn); }
                continue;
            }
            static void* said = nullptr;
            if (said != g) { said = g; char nm[96] = {}; NameOf(g, nm, sizeof nm); CoopLog("SUMMONGAUGE: found %s (%s) through %s +0x%X (%s)", nm, cn, o ? "player 1's Command_C" : "the party panel", off, props[k]); }
            return g;
        }
    }
    if (tries < 4) { ++tries; char c0[96] = {}, c1[96] = {}; if (panel) ObjClassName(panel, c0, sizeof c0); void* cc = BcmCmdC(MainBcm()); if (cc) ObjClassName(cc, c1, sizeof c1);
        CoopLog("SUMMONGAUGE: not found on the party panel (%s) or player 1's Command_C (%s)", c0, c1); }
    return nullptr;
}
static void SummonGaugeFollow(void* panel, float tx, float ty)
{
    // f1.326: both gauges (time + chant, found by the survey - the property lookup above is only the fallback for [0]),
    // each: the game's own translation (re-read each frame) + player 1's panel translation + the hidden rows' lift
    static void* last[2] = {}; static float setX[2] = {}, setY[2] = {}, baseX[2] = {}, baseY[2] = {}; static bool on[2] = {};
    static float lastLift = -1e9f;
    for (int k = 0; k < 2; ++k)
    {
        void* g = nullptr;
        if (g_SgIdx2[k] >= 0) { g = ResolveWeak(g_SgIdx2[k], g_SgSer2[k]); if (!g) g_SgIdx2[k] = -1; }
        if (!g && k == 0 && panel) g = SummonGauge(panel);
        if (!g) { last[k] = nullptr; on[k] = false; g_SgLive[k] = nullptr; continue; }
        if (g_SgORT < 0) g_SgORT = PropOff(*(void**)((unsigned char*)g + 0x10), "RenderTransform");
        if (g_SgORT < 0 || !Readable((unsigned char*)g + g_SgORT, 8)) continue;
        float* cur = (float*)((unsigned char*)g + g_SgORT);   // FWidgetTransform: Translation first
        if (g != last[k]) { last[k] = g; baseX[k] = cur[0]; baseY[k] = cur[1]; on[k] = false; }
        else if (fabsf(cur[0] - setX[k]) > 0.01f || fabsf(cur[1] - setY[k]) > 0.01f) { baseX[k] = cur[0]; baseY[k] = cur[1]; }   // the game moved it itself
        g_SgLive[k] = g;
        float lift = 0.f;
        if (panel && g_P1RowLiftPx > 0.5f)
        {   // the rows above player 1's are see-through (layout kept) - down by that much, in the gauge's own units
            float x, y, w, h, sc = 0.f;
            if (g_Hc.state == 1 && HcRect(g, &x, &y, &w, &h, &sc) && sc > 0.01f) lift = g_P1RowLiftPx / sc;
        }
        bool want = fabsf(tx) > 0.01f || fabsf(ty + lift) > 0.01f;
        if (!want && !on[k]) continue;
        setX[k] = baseX[k] + tx; setY[k] = baseY[k] + ty + lift;
        SetRenderTr(g, setX[k], setY[k]);
        if (want != on[k] || (k == 0 && fabsf(lift - lastLift) > 2.f))
        {
            on[k] = want; if (k == 0) lastLift = lift;
            char nm[96] = {}; NameOf(g, nm, sizeof nm);
            CoopLog("SUMMONGAUGE: %s %s (%.0f, %.0f HUD units + %.0f for the hidden rows; the game's own %.0f, %.0f)", nm,
                want ? "moved with player 1's party panel" : "back where the game puts it", tx, ty, lift, baseX[k], baseY[k]);
        }
    }
}
static void PartyPanelP1Place(void* canvas)
{
    void* m = MainSide(); if (!m) return;
    static bool moved = false, fieldNow = false;
    // f1.318 (the author, thirds on one wide screen, field: "I think you should probably fix that for the field in this
    // scenario"): over all monitors / one wide screen the field's party list (shown with player 1's field menu) sat at the
    // HUD box's bottom right = in player 2's column - now moved into player 1's part outside battle too (all rows, no rescale).
    bool inB = InBattleNow();
    bool on = g_Cfg.partyIndiv && AnyWindowShowing() && !g_InCutscene && !g_ScriptedShot && !g_GamePaused && (inB || g_HudSpanOk);
    if (!SetTrFind(m)) return;
    if (!on)
    {
        if (moved) { moved = false; fieldNow = false; SetRenderTr(m, 0.f, 0.f); g_P1RowLiftPx = 0.f; SummonGaugeFollow(m, 0.f, 0.f); if (g_PanelScaleNow[0] != 1.f) PanelScale(m, 0, 1.f); PartyRows(m, 0, -1, 0, "player 1's"); PARTYLOG("PARTYHUD: the game's party panel back to the bottom right of the screen"); }
        return;
    }
    float cx = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), cy = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (cx < 200.f || cy < 100.f) return;
    if (g_Hc.state == 0 && !HcResolve(canvas)) return;
    if (!inB)
    {   // f1.318: field, spanning only - the box's bottom right moved to player 1's part's right edge; all rows, scale 1
        if (g_PanelScaleNow[0] != 1.f) PanelScale(m, 0, 1.f);
        if (!fieldNow) { fieldNow = true; if (moved) PartyRows(m, 0, -1, 0, "player 1's"); }   // battle -> field: every row back
        float tx = 0.f;
        if (g_GameHwnd && g_HudSpanFit > 0.01f)
        {
            RECT cr{}; GetClientRect(g_GameHwnd, &cr);
            float partR = (g_MonRegF[0][0] + g_MonRegF[0][2]) * (float)cr.right, boxR = g_HudSpanX + g_HudSpanW;
            if (partR < boxR - 1.f) tx = (partR - boxR) / g_HudSpanFit;
        }
        g_P1RowLiftPx = 0.f; SetRenderTr(m, tx, 0.f); SummonGaugeFollow(m, tx, 0.f);   // (field: every row shown)
        static float lastTx = -1e9f;
        if (fabsf(lastTx - tx) > 0.5f) { lastTx = tx; PARTYLOG("PARTYHUD: field - the game's party list moved %.0f HUD units into player 1's part (all monitors / one wide screen)", tx); }
        moved = true;
        return;
    }
    fieldNow = false;
    float x, y, w, h; PipRect(cx, cy, &x, &y, &w, &h);   // player 2's window (its half also while only player 3's shows)
    float px = 0.f, py = 0.f, pw = cx, ph = cy;           // player 1's part (as DangerP1)
    if (SplitThirds()) { pw = cx / 3.f; }
    else if (SplitSide() >= 2) { ph = cy * 0.5f; py = y < cy * 0.25f ? cy * 0.5f : 0.f; }
    else { pw = cx * 0.5f; px = x < cx * 0.25f ? cx * 0.5f : 0.f; }
    float s = cy / 1080.f, rx, ry, rw, rh, sc;
    if (g_Hc.state == 1 && HcRect(m, &rx, &ry, &rw, &rh, &sc) && sc > 0.f) s = sc / g_PanelScaleNow[0];   // without our scale
    PanelScale(m, 0, PartyScaleWanted(pw * ph / (cx * cy)));   // f1.135; by player 1's part's size
    unsigned char* pmP = PartyMgr();
    float shift = PartyRows(m, 0, pmP ? LeaderType(pmP) : -1, g_Cfg.partyRows, "player 1's");
    float nud = SplitThirds() ? g_Cfg.thirdsPanelNudge * cy / 1080.f : 0.f;   // ThirdsPanelNudge (a little to the right)
    if (g_HudSpanOk)
    {   // f1.305: over all monitors the HUD is already on player 1's monitor - the panel stays at its own bottom right
        // (f1.315: unless player 1's part is narrower than the HUD box - one wide screen in thirds: moved left into the part)
        PanelScale(m, 0, PartyScaleWanted(1.f));
        float tx = 0.f;
        if (g_GameHwnd && g_HudSpanFit > 0.01f)
        {
            RECT cr{}; GetClientRect(g_GameHwnd, &cr);
            float partR = (g_MonRegF[0][0] + g_MonRegF[0][2]) * (float)cr.right, boxR = g_HudSpanX + g_HudSpanW;
            if (partR < boxR - 1.f) tx = (partR - boxR) / g_HudSpanFit;
        }
        SetRenderTr(m, tx, shift / s); SummonGaugeFollow(m, tx, shift / s);
        {
            RECT cr{}; GetClientRect(g_GameHwnd, &cr);
            HudSurvey(g_MonRegF[0][0] * cr.right, g_MonRegF[0][1] * cr.bottom, g_MonRegF[0][2] * cr.right, g_MonRegF[0][3] * cr.bottom,
                      g_HudSpanX + g_HudSpanW * 0.5f, g_HudSpanY + g_HudSpanH * 0.5f, g_HudSpanX + g_HudSpanW, g_HudSpanY + g_HudSpanH, fabsf(tx) < 1.f);
        }
        if (!moved) { moved = true; PARTYLOG("PARTYHUD: the game's party panel stays at the bottom right of player 1's monitor (all monitors%s)", g_Cfg.partyRows == 1 ? ", their row only" : g_Cfg.partyRows == 2 ? ", their row + AI teammates'" : ""); }
        return;
    }
    SetRenderTr(m, (px + pw - cx + nud) / s, (py + ph - cy + shift) / s); SummonGaugeFollow(m, (px + pw - cx + nud) / s, (py + ph - cy + shift) / s);
    HudSurvey(px, py, pw, ph, cx * 0.5f, cy * 0.5f, cx, cy);
    if (!moved) { moved = true; PARTYLOG("PARTYHUD: the game's party panel into player 1's part of the screen (%.0f, %.0f px%s)", px + pw - cx, py + ph - cy, g_Cfg.partyRows == 1 ? ", their row only" : g_Cfg.partyRows == 2 ? ", their row + AI teammates'" : ""); }
}
