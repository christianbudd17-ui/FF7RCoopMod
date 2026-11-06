// =================================================================================
// BATTLE STATE AND PLAYER 1'S MENU: in a battle?, hotkeys allowed?, player 1's keys, the game window, player 1's
// controller, the focus-context flags, player 1's menu kept off players 2 / 3 (restructure 3b: menus/08 split by job -
// the keys each command system sees are in menus/08b, the L2 / R2 pick, shortcut palette and BcmBefore in menus/08c)
// =================================================================================
// bcm (the game's battle command manager): +0x174 = battle UI state (8 = normal battle, 9 = a command menu open, 0 / 1 = no
// battle); the menu keeps one page per party slot (+0x2C = the page shown, -1 = the base page +0x28); NextMember /
// PrevMember turn the pages. Players 2 / 3 use their OWN command systems (menus/10) - since 2026-10-26 there is no
// "turn in player 1's menu" any more (the shared-menu fallback, unused since ~f1.30, was removed - the author approved).
// Player 1 opening his menu: players 2 / 3's actions pause, and player 1 can't page to their characters.
static volatile uint64_t g_InjectFName = 0;   // key to report as "just pressed" in the next real BCM pass
static volatile bool g_InjectHit = false;
static int  g_LastPageDir = 1;               // +1 NextMember, -1 PrevMember
static ULONGLONG g_LastBcmTick = 0;
static int g_LastBattleUi = 0;            // bcm+0x174, refreshed every BCM update
static bool InBattleNow() { return GetTickCount64() - g_LastBcmTick < IN_BATTLE_MS && (g_LastBattleUi == 8 || g_LastBattleUi == 9); }
// mod hotkeys only count in normal play - walking around or fighting. Not while the game is
// paused (main menu, pause, tutorials), in a cutscene, or while a battle command menu is open:
// the game uses F1-F8 in its menus, so the same key must not also flip something of ours.
static bool HotkeysAllowed()
{
    if (g_GamePaused || g_InCutscene) return false;
    if (GetTickCount64() - g_LastBcmTick < IN_BATTLE_MS && g_LastBattleUi >= 9) return false;
    return true;
}
static void NoteHotkeyIgnored(const char* what)
{
    CoopLog("HOTKEY: %s pressed outside normal play (%s) - ignored", what,
        g_GamePaused ? "game paused / menu" : g_InCutscene ? "cutscene" : "command menu open");
}

// (KeyFName and the RVA_KEY_* key names are in passes/05)

static uint64_t P1KeyFilter(void* key, uint64_t fname, uint64_t r)
{
    int st = (int)(r & 0xFF);
    // player 2/3's shortcut palette no longer runs in player 1's pass - player 1's keys always count here
    if (g_InjectFName && fname == g_InjectFName) { g_InjectHit = true; return (r & ~0xFFull) | 1; }
    if (st == 1)
    {
        if (fname == KeyFName(RVA_KEY_NEXTMEMBER)) g_LastPageDir = 1;
        else if (fname == KeyFName(RVA_KEY_PREVMEMBER)) g_LastPageDir = -1;
    }
    return r;
}


// the game's window, remembered the first time it is in front (input/40b In2Window, pip_hud/30c monitors). (The v3.7 window
// hook that swallowed player 1's keyboard during a turn in his menu is gone with that turn, 2026-10-26.)
static HWND g_GameHwnd = nullptr;
static void NoteGameWindow()
{
    if (g_GameHwnd) return;
    HWND fg = g_Keys.fg;   // this frame's reading (settings/03 KeySnapTake)
    if (!fg || !g_Keys.focus) return;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (wcscmp(cls, L"UnrealWindow") != 0) return;
    g_GameHwnd = fg;
    CoopLog("Game window found (%p)", fg);
}

// ---- player 1's controller ----
// is player 1's controller actually there? (set to Controller in the settings, but unplugged / switched off ->
// player 1 plays on the keyboard). Gone after 2 s without an answer, back on the first answer.
static bool P1PadLive()
{
    static ULONGLONG lastChk = 0, failSince = 0; static bool live = true, first = true;
    if (!g_Cfg.p1Controller || !In2SeatHasController(0)) return false;   // the leader seat holds a controller
    ULONGLONG now = GetTickCount64();
    if (now - lastChk < 250) return live;
    lastChk = now;
    XINPUT_STATE st{};
    bool ok = PadGetSeat(0, &st) == ERROR_SUCCESS;
    char sd[180]; In2SeatText(0, sd, sizeof(sd));
    if (first) { first = false; live = ok; CoopLog("PAD: player 1's controller (%s) %s", sd, ok ? "found - controller mode" : "not found - player 1 on the keyboard"); return live; }
    if (ok) { failSince = 0; if (!live) { live = true; CoopLog("PAD: player 1's controller is back - controller mode"); } }
    else
    {
        if (!failSince) failSince = now;
        if (live && now - failSince > 2000) { live = false; CoopLog("PAD: player 1's controller (%s) not found - player 1 on the keyboard", sd); }
    }
    return live;
}
// two command menus at once, step B (menus/34_focus_1_contexts.inc): per-player focus contexts (always - the Shared menu style is gone)
static bool g_FCtxOn = false;   // latched when the focus hooks go in (a later ini change doesn't switch it)
static int g_FCtxLive = 1;      // whose focus context is in the game's focus manager now
static bool g_CtxGateWas[4] = {false, false, false, false};   // this player's pad goes into their own menu's UI this frame (CtxKeys)
static int EnterCtx(int p); static void LeaveCtx(int prev); static void CtxKeys(); static void CtxReleaseAll();
static void WidgetSetVis(void* w, unsigned char v); static void* Bcm2Widget();   // (defined later)
static int MenuPage(unsigned char* bcm)
{
    int p = *(int*)(bcm + BCM_SHOWN_PAGE);
    return p >= 0 ? p : *(int*)(bcm + BCM_BASE_PAGE);
}

// Called every frame right after the game's real BCM update, for player 2 and then player 3: player 1's menu
// (g_MenuOwner = 1 while it is open) must not show this player's character - if he pages to it, page past it.
static void StepMenuOwnership(unsigned char* bcm, bool coop)
{
    if (g_Player->num == 2) P1PadLive();   // (once per frame)
    int ui = *(int*)(bcm + BCM_UI_STATE);
    bool open = ui >= 9;
    int page = MenuPage(bcm);
    if (g_InjectHit) { g_InjectFName = 0; g_InjectHit = false; }
    if (open && g_MenuOwner == 0)
    {
        g_MenuOwner = 1; CoopLog("P1 MENU: player 1 opened the menu (page %d)", page);
        if (CutsceneSafetyReady()) CutsceneClear("player 1 opened their command menu");   // f1.235 (core/06)
    }
    if (!open && g_MenuOwner == 1) { g_MenuOwner = 0; CoopLog("P1 MENU: player 1's menu closed"); }   // f1.211 (the multi-use report: the extra cast vs the close)
    if (g_MenuOwner == 1 && coop && page == P2Page())
    {
        if (g_Player->pageWait > 0) { g_Player->pageWait--; return; }
        g_InjectFName = KeyFName(g_LastPageDir > 0 ? RVA_KEY_NEXTMEMBER : RVA_KEY_PREVMEMBER);
        g_Player->pageWait = 4;
        CoopLog("P1 MENU: player 1 paged to player %d's character - skipping past it", g_Player->num);
    }
}
