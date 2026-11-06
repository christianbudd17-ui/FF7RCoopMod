// player 2's command pass on their own command system (replaces BcmAfter for player 2)
static void TsCapturePick(int p, unsigned char* bcm);   // (menus/35b)
// ---- f1.67: own menus locked to their player (the author, f1.66 test: "They should never be able to access P1's menu. Only
// their own and an AI character.") ----
// RE (exe 2026-10-21): a command system's +0x28 = its base page (the party-order index the menu opens on; MenuPage() reads
// +0x2C while open, else +0x28). The key handler 0x141C7B0 (bcm update -> 0x141813A) skips ALL keys - the menu button too -
// while the character on page +0x28 is knocked out (0x141C9F5: 0x12BD7A0(type) = unit +0x611 == 0xD). Players 2/3's own
// systems keep the leader's page there, so while player 1's character was down their own system never asked for the menu
// button (f1.26 / f1.66 tests: "last asked -1 ms ago"). And it opened on player 1's page first, then turned (the flash
// The author saw). Now (always since f1.76), their own system's +0x28 = their own page (or the AI teammate's L2 / R2 picked)
// while the menu is closed - it opens straight there and its keys work during a KO hand-off.
// LT / RT: 0x1420DC0 NextPage(bcm, cur, forward) - the next page with a living character (3 callers, all paging requests:
// 0x141E19B / 0x141E213 command menu, 0x141E5FC palette). Hooked: on a player's own system it skips pages that aren't
// theirs or an AI teammate's (player 1's, the other player's, knocked out) - returns cur if there is none (no turn).
typedef int (*NextPage_t)(unsigned char* bcm, int cur, uint8_t fwd);
static NextPage_t Orig_NextPage = nullptr;
static bool OwnPageAllowed(int page)   // loaded player (2/3): their own page or a living AI teammate's
{
    unsigned char* pm = PartyMgr(); if (!pm || page < 0) return false;
    int t = OrderType(pm, page); if (t < 0) return false;
    if (t == g_Player->charType) return true;
    if (g_OtherType >= 0 && t == g_OtherType) return false;                     // the other player's character
    if (t == LeaderType(pm)) return false;                                      // player 1's character
    void* c = CharByType(pm, t);
    return c && SurelyStanding(c);   // an AI teammate standing for sure (an unreadable HP = not allowed)
}
static int Hook_NextPage(unsigned char* bcm, int cur, uint8_t fwd)
{
    int r = Orig_NextPage(bcm, cur, fwd);
    if (!g_InActionPass2 || !bcm || bcm != g_Player->ownBcm || (g_Player->num != 2 && g_Player->num != 3)) return r;
    if (g_Player->internalTurn) { g_Player->internalTurn = false; return r; }   // the mod's own (hidden) opening / refresh turn
    if (GetTickCount64() - g_CtxConfirmT[g_Player->num & 3] < CONFIRM_TURN_MS) return r;   // right after their confirm: the game's own turn (OwnMenuCloseAfterCommand closes on it)
    int first = r, n = PartyCount();
    for (int k = 0; k < n + 1 && r != cur && !OwnPageAllowed(r); ++k) { r = Orig_NextPage(bcm, r, fwd); if (r == first) break; }
    if (r != cur && !OwnPageAllowed(r)) r = cur;
    static int logs = 0;
    if (logs < 40 && r != first) { ++logs; CoopLog("PAGES: player %d's own menu turns %s from page %d to %d (the game's next was %d - not theirs or an AI teammate's)%s", g_Player->num, fwd ? "forward" : "back", cur, r, first, r == cur ? " - stays" : ""); }
    return r;
}
static void* MakeInlineHook(uintptr_t target, void* hook, size_t stealLen, const unsigned char* expect, const char* name, void** outOrig = nullptr);   // core/28 (f1.337: outOrig)
static void InstallOwnMenuPageHook()
{
    static const unsigned char ex[14] = {0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x30,0x48,0x63,0xDA};
    void* t = MakeInlineHook(g_Base + 0x1420DC0, (void*)Hook_NextPage, 14, ex, "NextPage (own menu pages)");
    if (t) Orig_NextPage = (NextPage_t)t;
    CoopLog("PAGES: own-menu page hook %s", t ? "installed" : "NOT installed - LT / RT turn the game's way");
}
// (f1.69 - f1.142: QuietPageSetup - setting the own menu up on the player's character while closed - and its ChangePage hook
// 0x14216B0 that remembered which character each own menu showed were REMOVED in f1.143. The author: "I don't notice the
// menus being slow to open, so just remove it." Every open now makes the f1.68 hidden turn.)
// keep their own system's base page on their own page (or the L2 / R2 AI teammate's) while the menu is closed
static void OwnBasePage()
{
    if (!g_Player->ownBcm) return;
    int ui = *(int*)(g_Player->ownBcm + BCM_UI_STATE);
    int* base = (int*)(g_Player->ownBcm + BCM_BASE_PAGE);
    int wantT = g_Player->aiPageType;
    int want = (ui < 9 && wantT >= 0 && OrderIndexOf(wantT) >= 0) ? OrderIndexOf(wantT) : P2Page();
    if (want < 0) return;
    if (ui >= 9)
    {   // open: only if the base page's character went down (its keys would stop) - back to their own page
        unsigned char* pm = PartyMgr(); int t = pm ? OrderType(pm, *base) : -1; void* c = t >= 0 ? CharByType(pm, t) : nullptr;
        if (!(c && CharHP(c) == 0) || *base == P2Page()) return;
        want = P2Page(); if (want < 0) return;
    }
    if (*base == want) return;
    static int logs = 0;
    if (logs < 40) { ++logs; CoopLog("PAGES: player %d's own command system - base page %d -> %d (%s)", g_Player->num, *base, want, ui >= 9 ? "the base page's character is down" : wantT >= 0 ? "the AI teammate L2 / R2 picked" : "their own"); }
    *base = want;
}
// (f1.67 test: "P2 and P3's menus both opened straight to Cloud immediately"): with the base page on their own page the
// menu OPENED on their page (the log said page 1 / 2) but still SHOWED Cloud's commands - the page's contents are only
// rebuilt by a real page turn (0x14216B0 via the NextMember / PrevMember path), and its last turn was to Cloud's page. (The
// f1.66 "after the revive P3 opens P1's menu" was the same: the game had moved the base page, the contents stayed.) So
// every own menu now opens HIDDEN (UUserWidget::SetColorAndOpacity alpha 0), turns once away and on to their page with the
// game's own turns (unfiltered - nobody sees them), and shows when it is there. Max 1.5 s hidden.
static int g_HideState = 0;
// (review B13): UUserWidget's SetColorAndOpacity - the same game function for every user widget, looked up once and
// shared with player 1's reticle hiding (menus/35b; before, each looked it up itself)
static void* g_ColFn = nullptr; static int g_ColOff = -1;
static bool ColFind(void* w)   // w = any live user widget (may be null); true once the function is known
{
    if (g_ColFn) return true;
    if (!w) return false;
    void* f = FindFunc(w, "SetColorAndOpacity"); if (!f) return false;
    int p = ParamOff(f, "InColorAndOpacity"); if (p < 0 || p + 16 > 64 || ParmsSize(f) > 64) return false;
    g_ColOff = p; g_ColFn = f;
    return true;
}
static void SetWidgetColor(void* w, const float* c)   // r, g, b, a
{
    alignas(16) unsigned char b[64] = {};
    memcpy(b + g_ColOff, c, 16); Orig_ProcessEvent(w, g_ColFn, b);
}
static void OwnMenuHide(bool hide)   // loaded player's own Command_C
{
    void* w = Bcm2Widget();
    if (!w || !Orig_ProcessEvent) return;
    if (g_HideState == 0)
    {
        g_HideState = ColFind(w) ? 1 : -1;
        CoopLog("PAGES: own menu hiding while it turns to the player's page %s", g_HideState == 1 ? "ready" : "NOT available (SetColorAndOpacity not found) - the turn shows");
    }
    if (g_HideState != 1) return;
    float* col = g_Player->ownHideCol;
    if (hide && !g_Player->ownHidden)
    {
        const float* c = (const float*)((unsigned char*)w + 0x100);
        if (c[3] > 0.01f && c[3] <= 1.f) memcpy(col, c, 16); else { col[0] = col[1] = col[2] = col[3] = 1.f; }
        g_Player->ownHidden = true; g_Player->ownHiddenT = GetTickCount64();
    }
    if (!hide && !g_Player->ownHidden) return;
    float c2[4] = {col[0], col[1], col[2], hide ? 0.f : col[3]};
    SetWidgetColor(w, c2);
    if (!hide) g_Player->ownHidden = false;
}
static unsigned char PartyOwnVis();   // (pip_hud/36b)
// ---- BcmAfter2's steps (restructure #47: it was one ~150-line function doing all of them) ----
static void LogOwnHudSlot(int ownIdx)   // log once per player / slot what the game said vs what they get
{
    static int lastIdx[4] = {-9, -9, -9, -9}; static int ilogs = 0; int pi = g_Player->num & 3;
    if (ownIdx != lastIdx[pi] && ilogs < 30) { ++ilogs; lastIdx[pi] = ownIdx; CoopLog("OWNHUD: player %d's command system uses party slot %d as \"the leader's\" (their own character%s)", g_Player->num, ownIdx, ownIdx < 0 ? " - not known / OwnHudCharacter=0: the game's own answer" : ""); }
}
// (step C): LT / RT in their open menu = a page turn request (answered in their next update)
static void PageRequestFromPad(const BcmPlayer& bp, int ui0)
{
    if (ui0 >= 9 && !g_Player->menuTurning)
    {   // (step C): LT / RT (MenuPrevious/NextCharacter; not the shortcut-hold button) turn their open menu's page
        DWORD pp = bp.pressed & ~(g_Player->cfg->bShortcutHold | g_Player->cfg->bGuard | g_Player->cfg->bAttack | g_Player->cfg->bDodge | g_Player->cfg->bAbility | g_Player->cfg->bMenu);   // not a button that does something else
        int dir = (pp & g_Player->cfg->bMenuPrev) ? -1 : (pp & g_Player->cfg->bMenuNext) ? 1 : 0;
        if (dir)
        {
            g_Player->pageReq = dir; g_Player->lastPageDir = dir; g_Player->guardTries = 0;
            static int logs = 0; if (logs < 40) { ++logs; CoopLog("CMD2 MENU: player %d pages their own menu %s (on page %d)", g_Player->num, dir < 0 ? "back" : "forward", MenuPage(g_Player->ownBcm)); }
        }
    }
}
// (the author, f1.127 test: L2 / R2 to command an AI teammate -> "Crazy switching noise"; log 22:31:28: "base page
// 1 -> 2 (the AI teammate L2 / R2 picked)" every frame, the menu never opened). f1.129 v1 gave the game its own answer
// (Cloud's slot) while the menu was for an AI teammate - the f1.129 test (22:45:45) then showed "base page 0 -> 2" every
// frame: the game moves its base page to whatever "the leader's" slot is, whichever slot that is. RE: 0x141D820 (called
// from the bcm update at 0x14182ED): if +0x28 != 0x1420650's slot -> 0x1420ED0 (a page change) + +0x28 = that slot -
// a GUESS that this is the site (its gate, the weak object at +0x2F8, isn't understood). v2: while the base page is an AI
// teammate's (the L2 / R2 pick) "the leader's" slot IS that page, so the game has nothing to move.
static int LeaderSlotForUpdate(int ownIdx, int pIdx)
{
    int updIdx = ownIdx;
    {
        int bp0 = *(int*)(g_Player->ownBcm + BCM_BASE_PAGE);
        bool forAlly = bp0 >= 0 && bp0 < 3 && bp0 != P2Page() && OwnPageAllowed(bp0);
        if (forAlly) updIdx = bp0;
        static bool lastAlly[4] = {}; static int alogs = 0;
        if (forAlly != lastAlly[pIdx] && alogs < 60) { ++alogs; CoopLog("OWNHUD: player %d's command system %s", g_Player->num, forAlly ? "is on an AI teammate's page - that page counts as \"the leader's\" while it is" : "is back on their own page"); }
        lastAlly[pIdx] = forAlly;
    }
    return updIdx;
}
// ---- f1.232 THEIR DODGE GOES WHERE THEIR STICK POINTS (the author, S13 / S14: "Dodge doesn't seem to go directionally
// where the player is pointing" - "Yes dodging still felt wrong"). S14: no "STICK:" line at all - their command pass never
// asks for the move stick (f1.223's ActionPassAxis never ran). Their dodge / attacks are picked in this pass, and the move
// direction the game uses for them is the one player 1's controller keeps: its per-character movement state (0x710 -
// 0x74C + the world move vector 0x90C - 0x918, core/02 STATE_A / STATE_B - the same bytes the movement pass swaps in
// passes/25). After their movement pass that state is player 1's again, so their dodge went where PLAYER 1's stick pointed.
// Now their command pass runs with THEIR movement state in player 1's controller (as their movement pass does), saved back
// to their record afterwards, and player 1's put back. A guess from the code (the dodge's direction read itself isn't RE'd):
// "DODGE:" lines show the move vector each pass saw when they pressed dodge.
struct PcMoveSwap
{
    unsigned char* pc = nullptr; unsigned char p1[STATE_A_LEN + STATE_B_LEN]; bool on = false;
    PcMoveSwap()
    {
        pc = (unsigned char*)g_P1Pc;
        if (!pc || !g_Player->moveStateValid || GetCurrentThreadId() != g_GameThreadId || !Readable(pc + STATE_B_OFF, STATE_B_LEN)) { pc = nullptr; return; }
        SaveState(pc, p1); LoadState(pc, g_Player->moveState); on = true;
    }
    ~PcMoveSwap() { if (on) { SaveState(pc, g_Player->moveState); LoadState(pc, p1); } }
    void LogDodge(DWORD pressed) const
    {
        if (!(pressed & g_Player->cfg->bDodge)) return;
        static LogRate rate;
        if (!LogOk(rate, 20)) return;
        const float* mine = on ? (const float*)(pc + STATE_B_OFF) : nullptr;
        const float* p1v = on ? (const float*)(p1 + STATE_A_LEN) : nullptr;
        if (on) CoopLog("DODGE: player %d pressed dodge - their move vector (%.2f, %.2f, %.2f) is in for their command pass (player 1's: %.2f, %.2f, %.2f), stick (%.2f, %.2f)",
            g_Player->num, mine[0], mine[1], mine[2], p1v[0], p1v[1], p1v[2], g_Player->stickX, g_Player->stickY);
        else CoopLog("DODGE: player %d pressed dodge - their movement state isn't known yet (no movement pass) - player 1's is in", g_Player->num);
    }
};
// ---- f1.263 DODGE2 - LOG ONLY (the author, Abzu on f1.262: "I am not convinced dodge is working correctly still. Please
// check on that."). The f1.262 DODGE lines show the move vector their pass gets = their stick turned by their window
// camera's yaw (checked: 12:54:27 - 12:56:27 within a few degrees) - but with the stick let go it keeps the LAST direction
// (12:58:44.9 and 12:58:48.6 the same vector, stick 0, 0), and nobody knows where the dodge really goes. Now each dodge
// press is followed for 0.7 s: where their character really went (direction + metres), where their stick pointed in the
// world (window camera yaw + stick), the move vector, their facing before - one "DODGE2:" line. Remove after the answer.
static bool ActorLoc(void* actor, float out[3]);   // enemies/18
static float ActorYaw(void* actor);                 // enemies/18
static float AngleDiff(float a, float b);           // enemies/18
static float P2ControlYaw(unsigned char* pc);       // pip_hud/30
struct DodgeTrack { bool on = false; ULONGLONG at = 0; void* ch = nullptr; float p0[3] = {}, face0 = 0, stickW = 0, vecA = 0; bool stick0 = false; float maxD = 0, p1[3] = {}; };
static DodgeTrack g_DodgeTrack[4];
static void DodgeTrackStart(const float* vec)
{
    int pn = g_Player->num & 3;
    DodgeTrack& d = g_DodgeTrack[pn];
    void* ch = P2Char();
    if (!ch || !g_P1Pc || !ActorLoc(ch, d.p0)) return;
    d.on = true; d.at = GetTickCount64(); d.ch = ch; d.face0 = ActorYaw(ch); d.maxD = 0; memcpy(d.p1, d.p0, 12);
    float sx = g_Player->stickX, sy = g_Player->stickY;
    d.stick0 = sx == 0.f && sy == 0.f;
    d.stickW = P2ControlYaw((unsigned char*)g_P1Pc) + atan2f(sx, sy) * RAD_TO_DEG;
    d.vecA = vec ? atan2f(vec[1], vec[0]) * RAD_TO_DEG : 0.f;
}
static void DodgeTrackTick()
{
    int pn = g_Player->num & 3;
    DodgeTrack& d = g_DodgeTrack[pn];
    if (!d.on) return;
    ULONGLONG now = GetTickCount64();
    float p[3];
    if (P2Char() != d.ch || !ActorLoc(d.ch, p)) { d.on = false; return; }
    float dx = p[0] - d.p0[0], dy = p[1] - d.p0[1], dd = sqrtf(dx * dx + dy * dy);
    if (dd > d.maxD) { d.maxD = dd; memcpy(d.p1, p, 12); }
    if (now - d.at < 700) return;
    d.on = false;
    float mx = d.p1[0] - d.p0[0], my = d.p1[1] - d.p0[1];
    float went = atan2f(my, mx) * RAD_TO_DEG;
    static LogRate rate;
    if (!LogOk(rate, 30)) return;
    if (d.maxD < 50.f) { CoopLog("DODGE2: player %d's dodge - their character moved only %.1f m in 0.7 s (no dodge?) | stick %s, facing before %.0f deg", g_Player->num, d.maxD / 100.f, d.stick0 ? "let go" : "held", d.face0); return; }
    if (d.stick0)
        CoopLog("DODGE2: player %d's dodge went %.0f deg (%.1f m) with the stick LET GO - %.0f deg from their facing before (%.0f), %.0f deg from the kept move vector (%.0f)", g_Player->num, went, d.maxD / 100.f,
            AngleDiff(went, d.face0), d.face0, AngleDiff(went, d.vecA), d.vecA);
    else
        CoopLog("DODGE2: player %d's dodge went %.0f deg (%.1f m) - their stick pointed %.0f deg in the world (off by %.0f), move vector %.0f deg (off by %.0f), facing before %.0f deg", g_Player->num, went, d.maxD / 100.f,
            d.stickW, AngleDiff(went, d.stickW), d.vecA, AngleDiff(went, d.vecA), d.face0);
}
// ---- f1.265 INPUTDIR - THEIR DODGE / ABILITY DIRECTION (the author: "Do the research for dodge, that is a game breaking
// bug for P2/P3 right now" - "Go ahead and research"). f1.263 DODGE2 (Jenova): with their stick held, 12 of 33 dodges went
// along PLAYER 1's move direction (P1 moving); with P1 standing they followed their stick. RE (2026-11-01):
//  - the dodge animation carries UEndAnimNotifyStateBattleInputDirectionRotate (vtable 0x4B33110; NotifyBegin 0x72A480
//    sets unit +0x758 rate / +0x75C force, NotifyEnd 0x72A4E0 clears) - while it runs, the unit's tick 0x979020 turns the
//    character toward the INPUT DIRECTION;
//  - that direction comes from 0x9FF5B0 GetInputDirection(FVector* out): two analog axes read with 0x16E2800 (the key
//    value read - NOT GetAxisPair, so the mod's pass hooks never answered it: always player 1's stick), |x| and |y| < 0.1
//    -> false (no direction), else turned by the camera's yaw (0x1696BE0) into a world direction, normalised -> true.
//    It knows nothing about the character. Callers: the turn 0x9790B7, 0x977336, the ability / command code 0x89D91F,
//    0x89EF95, 0x89F285, 0x8A0795, 0x8A0D43, 0x8ADE49, 0x109D3D3. In most of them RAX holds the character (the unit's
//    vfunc +0x20 result) when it is called.
// Now 0x9FF5B0 is hooked through a two-instruction entry that keeps RAX: when RAX is player 2's / 3's character (joined,
// not the leader), the answer is THEIR stick (|x|, |y| < 0.1 -> false) turned by the yaw their stick moves by (their
// window camera while it shows - P2ControlYaw); anything else -> the game's own function. Log "INPUTDIR: ..." (first
// uses per call site). f1.264's DODGEHOLD (untested) is not in this build - it changed the controller's state, which this
// function never reads.
static constexpr uintptr_t RVA_INPUT_DIR = 0x9FF5B0;
static constexpr float INPUT_DIR_DEADZONE = 0.1f;   // the game's own (0x4F91B4C)
typedef bool (*InputDir_t)(float* out);
static InputDir_t Orig_InputDir = nullptr;
static void* LeaderChar();
extern "C" { void* g_InputDirRax = nullptr; bool Hook_InputDir_C(float* out); void Hook_InputDir_Entry(); }
asm(".text\n.globl Hook_InputDir_Entry\nHook_InputDir_Entry:\n    movq %rax, g_InputDirRax(%rip)\n    jmp Hook_InputDir_C\n");
extern "C" bool Hook_InputDir_C(float* out)
{
    void* ch = g_InputDirRax;
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    uintptr_t site = (g_Base && ret > g_Base) ? ret - g_Base : 0;
    if (ch && out && g_P1Pc && GetCurrentThreadId() == g_GameThreadId)
    {
        int who = 0;
        { WithP2 b; if (g_Player->joined && P2Char() == ch) who = 2; }
        if (!who && g_P3Enabled) { WithP3 w; if (g_Player->joined && P2Char() == ch) who = 3; }
        if (who && ch != LeaderChar())
        {
            bool r = false;
            auto run = [&]() {
                float sx = g_Player->stickX, sy = g_Player->stickY;
                float yaw = P2ControlYaw((unsigned char*)g_P1Pc);
                if (fabsf(sx) < INPUT_DIR_DEADZONE && fabsf(sy) < INPUT_DIR_DEADZONE) r = false;
                else
                {
                    float a = (yaw + atan2f(sx, sy) * RAD_TO_DEG) * DEG_TO_RAD;
                    out[0] = cosf(a); out[1] = sinf(a); out[2] = 0.f; r = true;
                }
                static struct { uintptr_t site; int n; } seen[12] = {}; static int nSeen = 0;
                int k = -1; for (int i = 0; i < nSeen; ++i) if (seen[i].site == site) k = i;
                if (k < 0 && nSeen < 12) { k = nSeen++; seen[k].site = site; seen[k].n = 0; }
                if (k >= 0 && seen[k].n < 6)
                {
                    ++seen[k].n;
                    CoopLog("INPUTDIR: player %d's character asks for its input direction (from RVA 0x%llX) - their stick (%.2f, %.2f), camera yaw %.0f -> %s%.0f deg",
                        g_Player->num, (unsigned long long)site, sx, sy, yaw, r ? "" : "none (stick let go) - ", r ? atan2f(out[1], out[0]) * RAD_TO_DEG : 0.f);
                }
            };
            if (who == 3) { WithP3 w; run(); } else { WithP2 b; run(); }
            return r;
        }
    }
    return Orig_InputDir(out);
}
static void InstallInputDirHook()
{
    static const unsigned char ex[19] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x55, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00};
    void* t = MakeInlineHook(g_Base + RVA_INPUT_DIR, (void*)Hook_InputDir_Entry, sizeof(ex), ex, "GetInputDirection (P2 / P3 dodge direction)");
    if (t) Orig_InputDir = (InputDir_t)t;
}
// their own command system's update, run in their own focus context
static void RunOwnUpdate(float dt, int updIdx, int ui0, int pIdx, DWORD pressed)
{
    DodgeTrackTick();   // f1.263 DODGE2 (log only)
    PcMoveSwap moveSwap;   // f1.232: their movement state (dodge direction) for their pass
    moveSwap.LogDodge(pressed);
    if ((pressed & g_Player->cfg->bDodge) && moveSwap.on) DodgeTrackStart((const float*)(moveSwap.pc + STATE_B_OFF));   // f1.263 DODGE2
    int baseBefore = *(int*)(g_Player->ownBcm + BCM_BASE_PAGE);
    g_InActionPass2 = true; g_OwnLeaderIdx = updIdx;   // "the leader's" slot for their update (their own, or the AI teammate page they picked)
    { int fc = EnterCtx(g_Player->num); Orig_BcmUpdate(g_Player->ownBcm, dt); TsCapturePick(g_Player->num, g_Player->ownBcm); LeaveCtx(fc); }
    {   // measuring - did the game move their base page during its own update?
        int baseAfter = *(int*)(g_Player->ownBcm + BCM_BASE_PAGE);
        static ULONGLONG lastT[4]; static int mlogs = 0; ULONGLONG nowM = GetTickCount64();
        if (baseAfter != baseBefore && mlogs < 60 && nowM - lastT[pIdx] > 1000)
        { ++mlogs; lastT[pIdx] = nowM; CoopLog("PAGES: the game moved player %d's base page %d -> %d during its update (\"the leader's\" slot given: %d, ui %d -> %d)", g_Player->num, baseBefore, baseAfter, updIdx, ui0, *(int*)(g_Player->ownBcm + BCM_UI_STATE)); }
    }
    g_OwnLeaderIdx = -1;
    g_Player->internalTurn = false;   // only for this update   // f1.20: their own command system runs in their own focus context; their pick read there too
    g_InActionPass2 = false;
}
static void DropUnaskedPageTurn()
{
    if (g_Player->pageReq)
    {   // their system didn't ask for that key this update - drop it
        static int logs = 0; if (logs < 20) { ++logs; CoopLog("CMD2 MENU: player %d's page turn (%s) wasn't asked for by their command system (ui %d, B8 %d)", g_Player->num, g_Player->pageReq < 0 ? "back" : "forward", *(int*)(g_Player->ownBcm + BCM_UI_STATE), *(int*)(g_Player->ownBcm + BCM_MODE)); }
        g_Player->pageReq = 0;
    }
}
static void LogPaletteCommand(int b8Before, const unsigned char* act0, const unsigned char* req0)   // did a palette shortcut fire? (action block / request change while the palette is open)
{
    static int logs = 0;
    if (b8Before == 1 && (memcmp(act0, g_Player->ownBcm + 0x524, 0x34) || memcmp(req0, g_Player->ownBcm + BCM_REQUEST_PTR, 0x40)) && logs < 60)
    { ++logs; CoopLog("CMD2: player %d's palette issued a command (B8 now %d, action %s, request %s)", g_Player->num, *(int*)(g_Player->ownBcm + BCM_MODE), memcmp(act0, g_Player->ownBcm + 0x524, 0x34) ? "changed" : "same", memcmp(req0, g_Player->ownBcm + BCM_REQUEST_PTR, 0x40) ? "changed" : "same"); }
}
static void OwnMenuPageTurns(int ui0, int ui1)   // player 2's own command menu opens on the leader's page - turn it to theirs (NextMember)
{
    static int triesA[4] = {}, waitA[4] = {};
    bool& turning = g_Player->menuTurning; int& tries = triesA[g_Player->num & 3]; int& wait = waitA[g_Player->num & 3];
    int& wantT = g_Player->aiPageType;   // L2 / R2 opened it for an AI teammate
    int pg = MenuPage(g_Player->ownBcm), want = (wantT >= 0 && OrderIndexOf(wantT) >= 0) ? OrderIndexOf(wantT) : P2Page();
    static bool refreshA[4] = {}; bool& refresh = refreshA[g_Player->num & 3];   // one turn away first (rebuilds the page's contents)
    if (ui0 < 9 && ui1 >= 9)
    {
        turning = true; tries = 0; wait = 3; g_Player->guardTries = 0;   // (the 3 is overwritten just below - left so this stays the same code; drop it in a build that is tested anyway)
        refresh = pg == want;   // (QuietPageSetup removed - always the hidden turn)
        OwnMenuHide(true); wait = OPEN_HIDE_WAIT_UPDATES;
        CoopLog("CMD2 MENU: player %d's own command menu opened (page %d, want %d%s, B8 %d) - hidden while it turns", g_Player->num, pg, want, wantT >= 0 ? " = an AI teammate's" : "", *(int*)(g_Player->ownBcm + BCM_MODE));
    }
    // wantT is cleared only when the menu CLOSES (or a press goes stale) - f1.35 cleared it every frame the menu was
    // still closed, i.e. in the frames between the L2 / R2 press and the menu opening, so it always opened on their own page
    if (wantT >= 0 && ui1 < 9 && ui0 < 9 && GetTickCount64() - g_Player->aiPageTick > ALLY_PICK_STALE_MS) wantT = -1;
    if (ui1 < 9) { if (ui0 >= 9) { CoopLog("CMD2 MENU: player %d's own command menu closed", g_Player->num); wantT = -1; } turning = false; refresh = false; g_Player->ownMenuTurn = false; g_Player->pageReq = 0; OwnMenuHide(false); }
    else if (turning)
    {
        static int lastPgA[4] = {-9, -9, -9, -9}; int& lastPg = lastPgA[g_Player->num & 3];
        if (refresh && tries > 0 && pg != lastPg) refresh = false;   // the turn away happened
        lastPg = pg;
        if ((pg == want && !refresh) || want < 0) { turning = false; OwnMenuHide(false); CoopLog("CMD2 MENU: on player %d's page %d after %d turns", g_Player->num, pg, tries); }
        else if (wait > 0) wait--;
        else if (tries >= OPEN_TURN_MAX_TRIES || (g_Player->ownHidden && GetTickCount64() - g_Player->ownHiddenT > HIDDEN_TURN_MAX_MS))
        {
            turning = false; OwnMenuHide(false);
            CoopLog("CMD2 MENU: couldn't turn to player %d's page (still %d, want %d%s)", g_Player->num, pg, want, refresh ? ", the refresh turn didn't move - only one page to show?" : "");
            refresh = false;
            if (!OwnPageAllowed(pg) && g_FCtxOn && !g_CtxAutoClose[g_Player->num & 3])
            {   // f1.215: left on a page that isn't theirs (player 1's, the other player's) - closed (a made Cancel), never shown there
                g_CtxAutoClose[g_Player->num & 3] = 1;
                CoopLog("CMD2 MENU: player %d's own menu is left on page %d (not theirs or an AI teammate's) - it closes (made Cancel)", g_Player->num, pg);
            }
        }
        else { g_Player->ownMenuTurn = true; tries++; wait = g_Player->ownHidden ? TURN_WAIT_HIDDEN_UPDATES : TURN_WAIT_SHOWN_UPDATES; }
    }
    else if (ui1 >= 9)
    {   // (step C): page guard - never on player 1's / the other player's character (or a knocked-out one):
        // keep turning the way they paged. (Their dispatcher uses the page's character - a command there would be theirs.)
        unsigned char* pm = PartyMgr();
        int pgType = pm ? OrderType(pm, pg) : -1;
        void* pgChar = (pm && pgType >= 0) ? CharByType(pm, pgType) : nullptr;
        bool bad = pg != P2Page() && (pg == P1Page() || (g_OtherType >= 0 && pg == OrderIndexOf(g_OtherType)) || (pgChar && !NotKnockedOut(pgChar)));   // (a KO'd page only - an unreadable HP isn't "bad")
        int& gt = g_Player->guardTries; int& gw = g_Player->guardWait;
        if (!bad) gt = 0;
        else if (gt == 0 && g_Cfg.ownMenuCloseAfterCmd && g_FCtxOn && GetTickCount64() - g_CtxConfirmT[g_Player->num & 3] < CONFIRM_TURN_MS && !g_CtxAutoClose[g_Player->num & 3])
        {   // (f1.35 / f1.40 tests: "hitting confirm did not close their menu and slow motion stayed active, until I hit
            // cancel"): right after their confirm the game turned their menu to another page - their command went in and the
            // game moved on to the leader's commands. Close it like the game does for player 1 (a made Cancel), don't turn back.
            g_CtxAutoClose[g_Player->num & 3] = 1; gt = PAGE_GUARD_MAX_TURNS + 1;   // done - no turning
            static int logs = 0; if (logs < 40) { ++logs; CoopLog("CMD2 MENU: player %d's command went in - the game turned their menu to page %d; it closes (made Cancel, OwnMenuCloseAfterCommand)", g_Player->num, pg); }
        }
        else if (gw > 0) gw--;
        else if (gt < PAGE_GUARD_MAX_TURNS)
        {
            g_Player->pageReq = g_Player->lastPageDir; gt++; gw = PAGE_GUARD_WAIT_UPDATES;
            static int logs = 0; if (logs < 40) { ++logs; CoopLog("CMD2 MENU: player %d's own menu is on page %d (%s) - turning past it", g_Player->num, pg, pg == P1Page() ? "player 1's character" : (pgChar && CharHP(pgChar) == 0) ? "a knocked-out character" : "the other player's character"); }
        }
        else if (gt == PAGE_GUARD_MAX_TURNS) { gt++; CoopLog("CMD2 MENU: player %d's own menu stays on page %d - couldn't turn past it", g_Player->num, pg); }
    }
}
static void LogOwnSystemState(int ui0, int ui1)
{
    static int lastB8A[4] = {-99, -99, -99, -99}, lastUiA[4] = {-99, -99, -99, -99}, logs = 0;
    int& lastB8 = lastB8A[g_Player->num & 3]; int& lastUi = lastUiA[g_Player->num & 3];
    int b8 = *(int*)(g_Player->ownBcm + BCM_MODE);
    if ((b8 != lastB8 || ui1 != lastUi) && logs < 200) { ++logs; CoopLog("CMD2: player %d's command system ui %d -> %d, mode B8 %d, page 30 %d, widget %p vis %d", g_Player->num, ui0, ui1, b8, *(int*)(g_Player->ownBcm + BCM_PALETTE_PAGE), Bcm2Widget(), Bcm2Widget() ? (int)((unsigned char*)Bcm2Widget())[0xB7] : -1); }
    lastB8 = b8; lastUi = ui1;
}
// f1.267 PARTYSYNC (the author, Swordipede: after the story's mid-battle party swap player 2's Aerith "does not build ATB";
// the f1.266 ATBDIAG log (removed in f1.268): her gauge DID fill (to 1236 / 2000, one bar spent) - "clearly her hud is showing incorrectly"). RE: each
// command system keeps its own copy of the party's character types in party order at bcm +0x200 (TArray<uint8>, count
// +0x208; filled by 0x14AC1A0(bcm + 0x1D0, 1) from the party manager's order +0x230). The party panel's rows take their
// character from that copy (row setup 0x141EF10: row +0x698 = type, name "$menu_partyname_%04d_%04d"), so player 2's panel
// kept Barret's row - Barret, out of the fight, has no ATB. When the party changes the game calls 0x141EE80(bcm) - type
// list + 0x14AA560 + a forced row setup - but only on player 1's system. Now: when their copy differs from player 1's,
// the same refresh runs on their own system (in their focus context, as "the leader's" slot their own), once a second at
// most. Log "PARTYSYNC".
static constexpr uintptr_t RVA_BCM_PARTY_REFRESH = 0x141EE80;
static constexpr size_t BCM_PARTY_TYPES = 0x200;   // TArray<uint8> data; Num at +0x208
static constexpr ULONGLONG PARTY_SYNC_RETRY_MS = 1000;
static bool BcmPartyTypes(unsigned char* b, unsigned char* out, int& n)
{
    n = 0;
    if (!b || !Readable(b + BCM_PARTY_TYPES, 0x10)) return false;
    unsigned char* d = *(unsigned char**)(b + BCM_PARTY_TYPES); int num = *(int*)(b + BCM_PARTY_TYPES + 8);
    if (num < 0 || num > 8 || (num && (!d || !Readable(d, num)))) return false;
    n = num; if (num) memcpy(out, d, num);
    return true;
}
static void PartyListSync(unsigned char* bcm, int ownIdx)
{
    unsigned char a[8], b[8]; int na, nb;
    if (!BcmPartyTypes(bcm, a, na) || !BcmPartyTypes(g_Player->ownBcm, b, nb)) return;
    if (na == nb && !memcmp(a, b, na)) return;
    if (na != PartyCount()) return;   // f1.269: player 1's copy hasn't caught up with the party yet (Rufus -> Arsenal 22:32:02: [0] vs [1 3])
    // (0x14AC1A0 ends with the presenter refresh of [bcm + 0x1E0] + 0x38 - set to the system itself by its setup 0x14A8C40)
    if (!Readable(g_Player->ownBcm + 0x1E0, 8) || *(unsigned char**)(g_Player->ownBcm + 0x1E0) != g_Player->ownBcm) return;
    static ULONGLONG lastTry[4] = {};
    ULONGLONG now = GetTickCount64(); int pi = g_Player->num & 3;
    if (now - lastTry[pi] < PARTY_SYNC_RETRY_MS) return;
    lastTry[pi] = now;
    char was[40] = "", is[40] = "";
    for (int i = 0; i < nb; ++i) snprintf(was + strlen(was), sizeof(was) - strlen(was), "%s%d", i ? " " : "", b[i]);
    for (int i = 0; i < na; ++i) snprintf(is + strlen(is), sizeof(is) - strlen(is), "%s%d", i ? " " : "", a[i]);
    typedef void (*PartyRefresh_t)(void*);
    g_OwnLeaderIdx = ownIdx; g_PresSelf = true;
    { int fc = EnterCtx(g_Player->num); ((PartyRefresh_t)(g_Base + RVA_BCM_PARTY_REFRESH))(g_Player->ownBcm); LeaveCtx(fc); }
    g_PresSelf = false;
    int nc; unsigned char c[8]; bool ok = BcmPartyTypes(g_Player->ownBcm, c, nc) && nc == na && !memcmp(c, a, na);
    CoopLog("PARTYSYNC: player %d's command system had the party types [%s], player 1's [%s] - the game's party refresh run on theirs: %s",
        g_Player->num, was, is, ok ? "now the same (their party panel rows are set up again)" : "STILL different (tried again in 1 s)");
}
static void BcmAfter2(float dt, unsigned char* bcm, const BcmPlayer& bp)
{
    if (!bp.coop || g_GamePaused)   // (own menus always)
    { g_Player->cmdButtonsPrev = g_Player->cmdButtons; if (g_FCtxOn) CtxReleaseAll(); return; }   // nothing stays held in their menu's UI
    // (v17.6 checked: their command system has its own request struct (bcm+0x378), separate from player 1's.)
    DWORD liveButtons = g_Player->cmdButtons;   // the pad as it is (kept as "last frame" so a held button isn't a new press every frame)
    if (g_Player->menuEnding) g_Player->cmdButtons = 0;   // battle ending - everything released in their own system
    g_ActionP2Char = bp.p;
    int ownIdx = P2Page();   // their own party slot for RVA_LEADER_SLOT (passes/07 Hook_LeaderIdx); always (the author: "It should just be on" - OwnHudCharacter removed)
    LogOwnHudSlot(ownIdx);
    PartyListSync(bcm, ownIdx);   // f1.267 (sets / clears g_OwnLeaderIdx itself)
    g_OwnLeaderIdx = ownIdx;
    if (g_Player->ownBcmDirty && Orig_PresRefresh && *(void**)(g_Player->ownBcm + 0x38 + 0x18)) { g_PresSelf = true; Orig_PresRefresh(g_Player->ownBcm + 0x38); g_PresSelf = false; g_Player->ownBcmDirty = false; }
    g_OwnLeaderIdx = -1;
    bool own = g_Player->shortcutOwner == g_Player->num;
    int ui0 = *(int*)(g_Player->ownBcm + BCM_UI_STATE);
    unsigned char req0[0x40], act0[0x34]; memcpy(req0, g_Player->ownBcm + BCM_REQUEST_PTR, 0x40); memcpy(act0, g_Player->ownBcm + 0x524, 0x34);
    int b8Before = *(int*)(g_Player->ownBcm + BCM_MODE);
    Menu2Keys();
    int pIdx = g_Player->num & 3;
    PageRequestFromPad(bp, ui0);
    OwnBasePage();
    int updIdx = LeaderSlotForUpdate(ownIdx, pIdx);
    RunOwnUpdate(dt, updIdx, ui0, pIdx, bp.pressed);
    DropUnaskedPageTurn();
    float* ch = SlowChannel(); if (ch) { g_SlowB2 = (g_SlowB2Valid && g_SlowB2 < *ch) ? g_SlowB2 : *ch; g_SlowB2Valid = true; }   // the slower extra system
    LogPaletteCommand(b8Before, act0, req0);
    int ui1 = *(int*)(g_Player->ownBcm + BCM_UI_STATE);
    OwnMenuPageTurns(ui0, ui1);
    if (own) StepShortcutPage(g_Player->ownBcm);
    LogOwnSystemState(ui0, ui1);
    void* side = Bcm2Side();   // its own party-status panel (Status_C) - player 1's already shows
    if (side) { unsigned char want = PartyOwnVis(); if (((unsigned char*)side)[0xB7] != want) WidgetSetVis(side, want); }   // shown with PartyPanel=Individual (pip_hud/36b)
    if ((bp.pressed & (g_Player->cfg->bAttack | g_Player->cfg->bDodge | g_Player->cfg->bGuard | g_Player->cfg->bAbility)) && !g_Player->menuEnding)
        CoopLog("Action pass %d (own command system): pad pressed buttons 0x%05lX", g_Player->num, bp.pressed);
    g_Player->cmdButtonsPrev = liveButtons;   // (was cmdButtons: 0 while the battle ends -> a held button counted as pressed again every frame)
}

// (restructure 3c, finding #22) this player's pass on player 1's command system runs with THEIR command request, action
// block and a closed palette in player 1's system. MainSysPassSwap puts them in when it is made and puts player 1's back
// when it goes out of scope - the same steps in the same order as the hand-written swap in / out before, but the "back"
// half can no longer be skipped by an early return.
// - the request (bcm+0x378 -> REQ_LEN bytes): player 1's kept in p1Req, the player's own copy (taken the first time) in.
// - the shortcut palette is one shared state in the command manager. When player 1's is open, this player's pass would
//   see "palette button not held" and close it, so the pass runs with the palette closed and player 1's state goes back
//   afterwards (+0x28..+0x33 page, +0x48 bit 0, +0xB8 = 1 UI mode, +0x168). Players 2/3's palettes live only in their own
//   command systems (BcmAfter2); this pass on player 1's system runs while their own system is off (the first updates of
//   a battle, before it is on) - it never opens or holds a palette (palette keys read "up" here, g_InMainSysPass).
// - the action block (bcm+ACT_OFF, ACT_LEN bytes): player 1's kept in p1Act, the player's own in.
struct MainSysPassSwap
{
    unsigned char* bcm;
    unsigned char* req = nullptr;
    bool swapReq = false, canSwap = false;
    alignas(16) unsigned char p1Req[REQ_LEN];
    alignas(16) unsigned char p1Act[ACT_LEN];
    PalSave pal;
    explicit MainSysPassSwap(unsigned char* b) : bcm(b)
    {
        req = Readable(bcm + BCM_REQUEST_PTR, 8) ? *(unsigned char**)(bcm + BCM_REQUEST_PTR) : nullptr;
        swapReq = req && Readable(req, REQ_LEN);
        if (swapReq)
        {
            memcpy(p1Req, req, REQ_LEN);
            if (!g_Player->reqValid) { memcpy(g_Player->req, req, REQ_LEN); *(int*)(g_Player->req + 0x60) = 0; g_Player->reqValid = true; CoopLog("GUARD: player %d gets their own command requests (first copy taken)", g_Player->num); }
            memcpy(req, g_Player->req, REQ_LEN);
        }
        int b8 = *(int*)(bcm + BCM_MODE);
        canSwap = b8 == 0 || b8 == 1;
        if (canSwap)
        {
            PalGet(bcm, pal); PalClose(bcm);
            static int logs = 0;
            if (logs < 4 && b8 == 1) { ++logs; CoopLog("SHORTCUT: player %d's pass on player 1's command system runs with the palette closed (player 1's open - put back after)", g_Player->num); }
        }
        memcpy(p1Act, bcm + ACT_OFF, ACT_LEN);
        if (!g_Player->actValid) { memcpy(g_Player->act, bcm + ACT_OFF, ACT_LEN); *(int*)(g_Player->act + (0x530 - ACT_OFF)) = 0; *(int*)(g_Player->act + (0x528 - ACT_OFF)) = -1; g_Player->actValid = true; }
        memcpy(bcm + ACT_OFF, g_Player->act, ACT_LEN);
    }
    ~MainSysPassSwap()
    {
        if (swapReq) { memcpy(g_Player->req, req, REQ_LEN); memcpy(req, p1Req, REQ_LEN); }
        memcpy(g_Player->act, bcm + ACT_OFF, ACT_LEN); memcpy(bcm + ACT_OFF, p1Act, ACT_LEN);
        if (canSwap) PalPut(bcm, pal);
    }
    MainSysPassSwap(const MainSysPassSwap&) = delete;
    MainSysPassSwap& operator=(const MainSysPassSwap&) = delete;
};
// this player's pass on PLAYER 1's command system - only while their own system isn't on yet (ExtraPass)
static void BcmAfter(void* self, float dt, unsigned char* bcm, const BcmPlayer& bp)
{
    if (!bp.coop || *(int*)(bcm + BCM_UI_STATE) >= BCM_UI_MENU_OPEN || g_GamePaused)   // not while paused
    {
        // No action passes while any command menu is open (anyone's).
        g_Player->cmdButtonsPrev = g_Player->cmdButtons;
        return;
    }
    g_ActionP2Char = bp.p;
    {
        MainSysPassSwap swap(bcm);
        PcMoveSwap moveSwap;   // f1.232: their movement state (dodge direction) for their pass
        moveSwap.LogDodge(bp.pressed);
        g_InActionPass2 = true; g_InMainSysPass = true;
        Orig_BcmUpdate(self, dt);
        g_InActionPass2 = false; g_InMainSysPass = false;
    }   // player 1's request / action block / palette back here
    if (bp.pressed & (g_Player->cfg->bAttack | g_Player->cfg->bDodge | g_Player->cfg->bGuard | g_Player->cfg->bAbility))
        CoopLog("Action pass %d: pad pressed buttons 0x%05lX (char=%p)", g_Player->num, bp.pressed, bp.p);
    g_Player->cmdButtonsPrev = g_Player->cmdButtons;
}
// the command manager sets the battle's menu slowdown (time-scale channel 2) at the end of every pass:
// 0.05 (BattleMenuSlowSpeed) while a command menu is open, 1 otherwise. After all passes, scale it by the setting.
static unsigned char* BcmOf(int p);   // menus/34_focus_1_contexts
static void MenuSlowTick()
{
    float* ch = SlowChannel();   // was written out again here (menus/10)
    if (!ch) return;
    float v = *ch;
    // (the author, test 16): with several command menus open, the STRONGEST slowdown of the open ones sets the pace
    // (before: player 1's MenuSlowdown won whenever his menu was open too, so the game sped up while player 2/3's was open).
    // The main system is player 1's menu; the extra systems are players 2/3's own command systems (their own menus).
    float vMain = g_SlowB2Valid ? g_SlowMain : v;
    bool mainOpen = vMain < 0.999f;
    bool extraOpen = g_SlowB2Valid && g_SlowB2 < 0.999f;
    {   // f1.215 (f1.214 test 23:34:33: a player 2 menu open in a battle cutscene ran it at 1% speed - "soft locked"): no
        // slowdown from players 2/3's menus during a cutscene / scripted shot (their menus are hidden then; f1.215 doesn't
        // open them there either - this is for one that was open when it began)
        bool cut = g_InCutscene || g_ScriptedShot;
        static bool loggedCut = false;
        if (cut && extraOpen)
        {
            extraOpen = false;
            if (g_SlowB2Valid) { g_SlowB2 = 1.f; v = vMain; *ch = v; }
            if (!loggedCut) { loggedCut = true; CoopLog("MENUSLOW: player 2/3's command menu open during a cutscene / scripted shot - no slowdown from it until it's over"); }
        }
        if (!cut) loggedCut = false;
    }
    if (mainOpen || extraOpen) g_MenuOpenTick = GetTickCount64();   // for the PERF lines ("menus open %")
    if (g_SlowB2Valid) { v = g_SlowMain < g_SlowB2 ? g_SlowMain : g_SlowB2; *ch = v; }   // the slower of the two command systems
    float pct = 0.f;
    // (the author: "Separate slowdown for player 2 and 3"): each player's own slowdown; the strongest open one still wins
    if (mainOpen) pct = g_Cfg.menuSlow;
    if (extraOpen)
        for (int p = 2; p <= 3; ++p)
        {
            unsigned char* b = BcmOf(p);
            if (b && Readable(b + BCM_UI_STATE, 4) && *(int*)(b + BCM_UI_STATE) >= BCM_UI_MENU_OPEN && g_Cfg.menuSlowP[p] > pct) pct = g_Cfg.menuSlowP[p];
        }
    if (!mainOpen && !extraOpen) pct = 1.f;   // nothing open (v is 1 then anyway)
    static int lastState = -1;
    int state = v >= 0.999f ? 0 : (mainOpen ? 1 : 0) | (extraOpen ? 2 : 0);
    if (state != lastState)
    {
        lastState = state;
        if (state) { float nv = 1.f - (1.f - v) * pct; CoopLog("MENUSLOW: %s command menu open - the game's speed %.3f, set to %.3f (%.0f%% of the slowdown%s)", state == 3 ? "player 1's and player 2/3's" : state == 2 ? "player 2/3's" : "player 1's", v, nv < 0.01f ? 0.01f : nv, pct * 100.f, state == 3 ? ", the stronger of the two" : ""); }
    }
    if (v >= 0.999f || pct >= 0.999f) return;
    float nv = 1.f - (1.f - v) * pct;
    if (nv < 0.01f) nv = 0.01f;
    if (nv > 1.f) nv = 1.f;
    *ch = nv;
}
static void Hook_BcmUpdate_Inner(void* self, float dt);
// (review B17): one extra player's command pass - on their own command system, or on player 1's before their own is on
// (the first updates of a battle)
static void ExtraPass(void* self, float dt, unsigned char* bcm, const BcmPlayer& bp)
{
    if (g_Player->ownBcmOn) BcmAfter2(dt, bcm, bp); else BcmAfter(self, dt, bcm, bp);
}
static void Hook_BcmUpdate(void* self, float dt)
{
    Hook_BcmUpdate_Inner(self, dt);
    MenuSlowTick();
}
// ---- Hook_BcmUpdate_Inner's steps (restructure 3c) ----
// each extra player's pad read for this update (BcmBefore) and their own command system switched on / off (Bcm2Decide)
static void ReadPadsAndDecide(unsigned char* bcm, BcmPlayer& b2, BcmPlayer& b3)
{
    BcmBefore(bcm, b2);
    if (g_P3Enabled) { WithP3 w; BcmBefore(bcm, b3); }
    Bcm2Decide(bcm, b2.coop);
    if (g_P3Enabled) { WithP3 w; Bcm2Decide(bcm, b3.coop); }
}
static void ExtraPassesP2ThenP3(void* self, float dt, unsigned char* bcm, const BcmPlayer& b2, const BcmPlayer& b3)
{
    ExtraPass(self, dt, bcm, b2);
    if (g_P3Enabled) { WithP3 w; ExtraPass(self, dt, bcm, b3); }
}
// the palette owner's pass goes last (the last pass decides the palette display): player 2 holding theirs -> player 3 first
static void ExtraPassesPaletteOwnerLast(void* self, float dt, unsigned char* bcm, const BcmPlayer& b2, const BcmPlayer& b3)
{
    if (g_P3Enabled && g_Player->shortcutOwner == 2)
    {
        { WithP3 w; ExtraPass(self, dt, bcm, b3); }
        ExtraPass(self, dt, bcm, b2);
    }
    else ExtraPassesP2ThenP3(self, dt, bcm, b2, b3);
}
// the game's own pass (keyboard / player 1, plus anything we inject) and its slowdown value
static void RunGamePass(void* self, float dt)
{
    g_InMainBcmPass = true;                 // (shortcut palette ownership)
    Orig_BcmUpdate(self, dt);
    g_InMainBcmPass = false;
    float* chm = SlowChannel(); if (chm) g_SlowMain = *chm;
}
static void MenuOwnershipSteps(unsigned char* bcm, const BcmPlayer& b2, const BcmPlayer& b3)
{
    StepMenuOwnership(bcm, b2.coop);
    if (g_P3Enabled) { WithP3 w; StepMenuOwnership(bcm, b3.coop); }
}
// once per frame: the battle's command update - every player's pad, the extra players' passes, the game's own pass
static void Hook_BcmUpdate_Inner(void* self, float dt)
{
    WithP2 base;   // start from player 2's state whatever was loaded
    unsigned char* bcm = (unsigned char*)self;
    g_LastBcmTick = GetTickCount64();
    g_LastBattleUi = *(int*)(bcm + BCM_UI_STATE);
    BcmPlayer b2, b3;
    ReadPadsAndDecide(bcm, b2, b3);
    g_SlowB2Valid = false;
    // player 1's own palette is open - the extra passes go first this frame (the last pass decides the palette HUD)
    bool p1PalLast = *(int*)(bcm + BCM_MODE) == 1 && *(int*)(bcm + BCM_PALETTE_PAGE) != -1;
    if (p1PalLast)
    {
        static int logs = 0; if (logs < 3) { ++logs; CoopLog("SHORTCUT: player 1's palette is open - players 2/3's passes run first this frame"); }
        ExtraPassesP2ThenP3(self, dt, bcm, b2, b3);
    }
    RunGamePass(self, dt);
    MenuOwnershipSteps(bcm, b2, b3);
    if (!p1PalLast) ExtraPassesPaletteOwnerLast(self, dt, bcm, b2, b3);
}
