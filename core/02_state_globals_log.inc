// =================================================================================
// CO-OP PASS 2
// =================================================================================
// What we learned from reading ff7remake_.exe (all addresses are offsets from the exe's base):
//  - AEndPlayerController::Tick (vtable slot 129, code at 0x15F5990) is where the game reads
//    the move stick and pushes movement into whichever character the controller's
//    LeaderCharacter field (offset 0x6F8) points at.
//  - It reads the stick through a small helper, GetAxisPair (0x16E29C0), and checks the
//    walk/dash keys through two other helpers (0x16E26C0 "held", 0x16E25C0 "just pressed").
//  - UEndPartyAPI::GetPartyMember's real code is at 0x15EF5B0 (takes a slot index, returns the
//    character).
// So, every frame, after the game's normal Tick (pad 1 / keyboard -> Cloud), we:
//   1. point LeaderCharacter at party slot 1 (Barret),
//   2. make GetAxisPair return pad 2's left stick and the walk/dash checks return "not pressed",
//   3. run Tick a second time,
//   4. put LeaderCharacter back.
// The walk/dash smoothing state the controller keeps for the character is saved and restored
// per player so the two characters don't share it.
// Players 2/3 join / leave with their JoinKey (F10 / Ctrl+F10) or JoinButton. (v2.0 used pad 2's BACK button, v2.1 Ctrl+F12.)

static constexpr uintptr_t RVA_PC_VTABLE      = 0x4C16C90;
static constexpr int       PC_TICK_SLOT       = 129;
static constexpr uintptr_t RVA_PC_TICK        = 0x15F5990;
static constexpr uintptr_t RVA_GET_AXIS_PAIR  = 0x16E29C0;
static constexpr uintptr_t RVA_KEY_HELD       = 0x16E26C0;
static constexpr uintptr_t RVA_KEY_PRESSED    = 0x16E25C0;
static constexpr uintptr_t RVA_GET_PARTY_MEM  = 0x15EF5B0;
static constexpr uintptr_t RVA_GOBJ_OBJECTS    = 0x53BD480; // FUObjectItem* (24-byte items)
static constexpr uintptr_t RVA_GOBJ_NUM        = 0x53BD48C; // int NumElements
static constexpr uintptr_t OFF_LEADER_CHAR    = 0x6F8;

// Per-character movement state kept on the player controller (found by listing every field
// Tick writes): 0x710-0x74C and the world move vector at 0x90C-0x918.
static constexpr size_t STATE_A_OFF = 0x710, STATE_A_LEN = 0x3C;
static constexpr size_t STATE_B_OFF = 0x90C, STATE_B_LEN = 0x0C;

// ---- Players 2 and 3: one record each ----
// Players 2 and 3 run the same code. It works on g_Player = the current player's record: player 2's, or player 3's while
// WithP3 is active (WithP2 / WithP3 / LoadP3 only switch which record g_Player points at). g_OtherPlayer = the other one
// (e.g. code running for player 2 that needs player 3's color). So g_Player->stickX is "this player's left stick".
// (Restructure step 2: before it, every one of these values was a global that existed twice - x / x_p3 - and LoadP3
// swapped all the values in and out.)
// ---- named numbers used by several files (restructure clean-up, review findings #8 / #24 / #44 / #48) ----
static constexpr float     DEG_TO_RAD = 0.01745329f;          // degrees -> radians
static constexpr float     RAD_TO_DEG = 57.29578f;            // radians -> degrees
static constexpr int       RIGHT_STICK_DEADZONE = 8689;       // = Microsoft's XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
static constexpr ULONGLONG AUTO_LEAVE_AFTER_MS = 3000;        // a player's controller gone this long -> their character back to the AI
static constexpr ULONGLONG IN_BATTLE_MS = 500;                // the battle command system updated this recently = a battle is on
static constexpr ULONGLONG BATTLE_UI_STALE_MS = 1000;         // ... not updated for this long = the battle UI has stopped
static constexpr ULONGLONG CONFIRM_TURN_MS = 600;             // right after a confirm in their own menu: the game's own turn
static constexpr ULONGLONG HIDDEN_TURN_MAX_MS = 1500;         // their own menu stays hidden while it turns at most this long
static constexpr ULONGLONG ALLY_PICK_STALE_MS = 1500;         // an L2 / R2 teammate pick not used by then is dropped
static constexpr float     JOIN_HOLD_SECONDS = 1.0f;          // hold the join button this long to join / leave (the author 2026-10-26: not a setting)
static constexpr float     CAM_EXTRA_TILT = 5.0f;             // the co-op camera always looks this many degrees steeper (sits higher) than the game's (the author 2026-10-26: not a setting, was [Camera] ExtraTilt)
static constexpr float     FPS_SMOOTHING = 0.05f;             // how fast the shown frame rate follows the real one
static constexpr int       OPEN_TURN_MAX_TRIES = 10;          // turns tried to bring their own menu to their page when it opens
static constexpr int       PAGE_GUARD_MAX_TURNS = 6;          // page-guard turns past a page that isn't theirs before giving up
// (finding #48) battle-command updates waited between the own-menu turns (passes/12 OwnMenuPageTurns)
static constexpr int       OPEN_HIDE_WAIT_UPDATES = 1;        // after the just-opened menu is hidden, before its first turn
static constexpr int       TURN_WAIT_HIDDEN_UPDATES = 2;      // between two turns while the menu is hidden
static constexpr int       TURN_WAIT_SHOWN_UPDATES = 5;       // ... while it shows (the turn is seen - slower)
static constexpr int       PAGE_GUARD_WAIT_UPDATES = 5;       // between two page-guard turns
// a party character's fields (finding #26)
static constexpr size_t OFF_BATTLE_UNIT = 0xCB0;     // chara -> its battle unit (HP, target, guard byte ...)
static constexpr size_t OFF_CHAR_AI_CTRL = 0xCA8;    // chara -> its AI battle controller
static constexpr size_t OFF_AI_FLAGS = 0xB90;        // AI controller: flags (DWORD)
static constexpr DWORD  AIFLAG_DECISIONS_OFF = 0x40; //   bit: the AI makes no decisions for this character
static constexpr size_t OFF_AI_FLAGS_CHANGED = 0x22F9;   // AI controller: byte set to 1 after the flags change
// the game's battle command manager ("bcm" - player 1's, and each extra player's own command system): field offsets
// (finding #26 - they were bare numbers in menus / passes / pip_hud)
static constexpr size_t BCM_BASE_PAGE = 0x28;      // int: the page (party slot) the menu opens on
static constexpr size_t BCM_SHOWN_PAGE = 0x2C;     // int: the page shown while open, -1 = the base page
static constexpr size_t BCM_PALETTE_PAGE = 0x30;   // int: the shortcut palette's page, -1 = closed
static constexpr size_t BCM_PALETTE_FLAGS = 0x48;  // uint32: bit 0 = the palette is up
static constexpr size_t BCM_MODE = 0xB8;           // int: 0 = normal, 1 = shortcut palette, 4 = command menu
static constexpr size_t BCM_PALETTE_STATE = 0x168; // int: palette state (-1 = closed) - saved / restored with the palette
static constexpr size_t BCM_UI_STATE = 0x174;      // int: 0 / 1 = no battle, 8 = battle, 9 = a command menu open
static constexpr int    BCM_UI_BATTLE = 8, BCM_UI_MENU_OPEN = 9;
static constexpr size_t BCM_CMDC_INDEX = 0x300, BCM_CMDC_SERIAL = 0x304;   // weak pointer to its Command_C widget (object index / serial)
static constexpr size_t BCM_REQUEST_PTR = 0x378;   // pointer to its command request block (REQ_LEN bytes)
static constexpr size_t REQ_LEN = 0x80;                     // menus/08: the command request block kept per player
static constexpr size_t ACT_OFF = 0x524, ACT_LEN = 0x34;    // menus/08: bcm+0x524..0x557, the action block kept per player
struct PlayerCfg;   // settings/03: one player's own settings
struct PlayerSlot
{
    PlayerCfg* cfg = nullptr;     // their settings (settings/03 g_P2Cfg / g_P3Cfg - set there)
    // who / state (core/02, seats/04)
    int   partySlot = 1;          // party member index (player 2 = 1, player 3 = 2)
    bool  joined = true;            // this player is in (joined)
    int   num = 2;                 // 2 or 3
    float color[4] = {0.30f, 1.f, 0.45f, 0.95f};   // their color (P2 green)
    int   charType = -1;              // their character type (-1 = none)
    // AI controller of their character (core/02, passes/15)
    void* aiOffCtrl = nullptr;
    int   aiOffWIdx = -2, aiOffWSer = 0;
    int   aiOffType = -1;
    // sticks / buttons (core/02, passes/05, passes/07, passes/15)
    float stickX = 0.f, stickY = 0.f;
    float rStickX = 0.f, rStickY = 0.f;
    volatile bool camStick = false;
    DWORD padNow = 0;
    XINPUT_STATE padFrame = {};   // (review #23 / #28): their controller, read ONCE per frame (passes/05 ReadPadThisFrame)
    bool  padFrameOk = false;     // ... and whether it read (false = no controller / unplugged; padFrame is then all zero)
    DWORD fieldPrevButtons = 0;
    DWORD cmdButtons = 0, cmdButtonsPrev = 0;
    DWORD switchPrevButtons = 0;
    // movement state kept for their character (core/02)
    unsigned char moveState[STATE_A_LEN + STATE_B_LEN] = {};
    bool  moveStateValid = false;
    void* lastChar = nullptr;
    // joining / connection (passes/05)
    bool  connected = false, autoLeft = false;
    ULONGLONG connLostAt = 0, joinHeldSince = 0;
    bool  connFirstCheck = true, joinFired = false, joinKeyWasDown = false;
    // player 1's menu kept off their character (menus/08); their command requests / actions in player 1's system (passes/12 BcmAfter)
    int   pageWait = 0;             // updates to wait before paging player 1's menu past this player's character again
    alignas(16) unsigned char req[REQ_LEN] = {};
    bool  reqValid = false;
    unsigned char act[ACT_LEN] = {};
    bool  actValid = false;
    // shortcut palette (passes/07)
    volatile int shortcutOwner = 0;
    uint64_t shortcutPressFName = 0;
    ULONGLONG shortcutSince = 0;
    bool  shortcutPageOk = false;
    bool  shortcutTurn = false;
    int   shortcutWait = 0, shortcutTries = 0;
    int   shortcutDir = 1, shortcutLastPage = -99;
    ULONGLONG shortcutT0 = 0;
    // their own command system (passes/07, menus/10)
    unsigned char* ownBcm = nullptr;
    bool  ownBcmDirty = false, ownBcmOn = false;
    bool  ownMenuTurn = false;
    // paging in their own command menu (passes/07, passes/12, menus/08, menus/10)
    bool  internalTurn = false;     // the mod's own (hidden) turn was answered this update - NextPage lets it through
    int   pageReq = 0;              // -1 / +1: answer PrevMember / NextMember once in their system's next update
    int   lastPageDir = 1;          // the way they last paged (the page guard keeps going that way)
    int   aiPageType = -1;          // L2 / R2 with the menu closed: the AI teammate whose page it opens on
    ULONGLONG aiPageTick = 0;       // when aiPageType was set
    bool  menuTurning = false;      // their own menu is turning to its opening page
    int   guardTries = 0, guardWait = 0;   // the page guard
    bool  ownHidden = false;        // their own menu is hidden while it turns (OwnMenuHide)
    ULONGLONG ownHiddenT = 0;
    float ownHideCol[4] = {};       // its color before hiding
    bool  menuEnding = false;       // their battle is ending - their own system gets no buttons
    // where their own Command_C goes in their window (menus/35 Bcm2IntoWindow)
    float cmdOffX = 0.f, cmdOffY = 0.f;          // the offset given to SetRenderTranslation
    float cmdWantX = -1e9f, cmdWantY = -1e9f;    // the spot it was worked out for
    float cmdLastScale = 0.f;
    bool  cmdOffSet = false;
    int   cmdFrame = 0, cmdSetAt = 0;
    float cmdScaleNow = 1.f;                     // the scale on it now (CommandMenuScale)
    ULONGLONG cmdBottomMs = 0;                   // when it was last placed "along the bottom" (no window)
    bool  ownPanelShown = false;                 // pip_hud/36b: we showed / moved their own party panel
    bool  cmdHudCutHidden = false;               // menus/10 OwnCmdHudCutscene (f1.209): their Command_C hidden for a cutscene / scripted shot
    unsigned char cmdHudCutVis = 4;              //   its visibility before that (put back afterwards)
    ULONGLONG cmdHudCutEnd = 0;                  //   when the cutscene / shot ended (shown again 0.5 s later, if the battle goes on)
    void* unregFn = nullptr; void* unregW = nullptr; int unregTick = 0;
    // targeting (enemies/16, seats/17)
    bool  tgtLocked = false;
    int   tgtIdx = -1, tgtSerial = 0;
    int   tgtAppliedIdx = -1, tgtAppliedSerial = 0;
    ULONGLONG tgtLastApply = 0;
    bool  tgtR3Was = false;
    int   tgtFlickDir = 0;
    bool  tgtWasActive = false;
    volatile bool drawValid = false;
    int   tgtLastEnemyCount = -1;
    int   tgtListIdx[64] = {}, tgtListSer[64] = {}, tgtListN = 0;
    ULONGLONG tgtListScan = 0;
    // their split-screen window (marker/19, pip_hud/30)
    int   pipActIdx = -1, pipActSer = 0, pipRtIdx = -1, pipRtSer = 0;
    bool  pipVisible = false, pipTicking = true, pipFresh = false;
    float pipYaw = 0.f;
    int   pipFails = 0;
    ULONGLONG pipLastTry = 0;
    float pipCamPos[3] = {0, 0, 0}, pipCamRot[2] = {0, 0};   // pitch, yaw
    bool  pipCamOk = false;
    float pipYawS = 0.f, pipPitchAdj = 0.f, pipRtAspect = 0.f;
    ULONGLONG pipLastT = 0, pipLastManual = 0;
};
static PlayerSlot MakePlayer3Slot()   // player 3's starting values (where they differ from player 2's)
{
    PlayerSlot s;
    s.partySlot = 2;
    s.joined = false;
    s.num = 3;
    s.color[0] = 1.f; s.color[1] = 0.62f; s.color[2] = 0.15f; s.color[3] = 0.95f;   // P3 orange
    s.pipFresh = true;
    return s;
}
static PlayerSlot g_Player2Slot;
static PlayerSlot g_Player3Slot = MakePlayer3Slot();
static PlayerSlot* g_Player = &g_Player2Slot;     // the player the code is working on now (player 3's inside WithP3)
static PlayerSlot* g_OtherPlayer = &g_Player3Slot;   // the other one
// ---- end of the per-player records ----

typedef void     (*Tick_t)(void* pc, float dt);
typedef uint64_t (*GetAxisPair_t)(void* keyX, void* keyY, float* out);
typedef uint64_t (*KeyCheck_t)(void* key);
typedef void*    (*GetPartyMember_t)(int index);

static uintptr_t        g_Base = 0;
static Tick_t           Orig_Tick = nullptr;
static GetAxisPair_t    Orig_GetAxisPair = nullptr;
static KeyCheck_t       Orig_KeyHeld = nullptr;
static KeyCheck_t       Orig_KeyPressed = nullptr;
static GetPartyMember_t GetPartyMember = nullptr;

static bool  g_HooksInstalled = false;
static bool  g_InPass2 = false;
// PLAYER 3: the per-player values are in two records (PlayerSlot, above). LoadP3(true) makes player 3's record the
// current one (so all the player-2 code runs unchanged for player 3), LoadP3(false) switches back; WithP3 / WithP2 do it
// for a block of code.
static bool  g_P3Enabled = false;        // [Input2] Seat3 isn't None (blank = Auto = on)
static int g_SeatPerson[3] = {0, 1, 2};   // which person (the player number they started as, 0-based) sits in each seat now
static void PlayerLabel(int pnum, wchar_t* out, size_t n)
{
    swprintf(out, n, L"Player %d", pnum);
}
static bool  g_P3Loaded = false;         // player 3's state is in the normal variables right now
static int   g_OtherType = -1;           // the other human player's character type (-1 = none)
// (code review C2): weak reference to aiOffCtrl (UObject index + serial) - after a save load / chapter change the old
// controller is freed, and handing the AI back must not write into that memory. -2 = no reference (old behaviour)
static void* g_P3HumanChar = nullptr;    // player 3's character this frame while a person plays it
static bool  g_P3Human = false;
static void LoadP3(bool want);
struct WithP3 { bool prev; WithP3() { prev = g_P3Loaded; LoadP3(true); } ~WithP3() { LoadP3(prev); } };
struct WithP2 { bool prev; WithP2() { prev = g_P3Loaded; LoadP3(false); } ~WithP2() { LoadP3(prev); } };
static void* g_P1Pc = nullptr;           // player 1's player controller (Hook_Tick) - for the game's projection calls
static volatile int  g_MenuOwner = 0;        // 1 = player 1's command menu is open, 0 = not (menus/08 StepMenuOwnership)
static volatile bool g_GamePaused = false;  // (set by the pause check in Hook_Tick)
static unsigned char g_P1State[STATE_A_LEN + STATE_B_LEN];

extern "C" __attribute__((used)) const char g_CoopVersionMarker[] = "FF7RCOOPMOD_VERSION=" COOP_MOD_VERSION ";";   // the settings app looks for this
// f1.200 (the author, public beta: "do that"): the detailed log is ALWAYS on - the [General] DetailedLog setting is gone.
// The short-log filter below is kept (g_DetailedLog = true skips it) - it lists which lines are event / research lines for
// the end-of-project log clean-up. Was (the author 2026-10-25): DetailedLog off = only the lines a bug report always needs: version,
// settings, hooks, controllers / seats / joins, party, pause, errors. The rest (event + research lines) is written only with
// it on. The groups: claude/FF7R_End_of_Project_Removal_Checklist.md "Logging". A line is classified by its written text:
// it starts with one of kLogDetail's prefixes ('~' = any digits; with `contains`: only lines that also contain that) - unless
// it contains one of kLogAlways' words (a failure is always written, whatever module it comes from). The table was checked
// against a real 61 569-line log (f1.149 test): 8.2 MB -> 0.37 MB with it off.
static volatile bool g_DetailedLog = true;   // f1.200: always (was the DetailedLog setting)
struct LogRule { const char* prefix; const char* contains; };
static const LogRule kLogDetail[] = {
    {"FOCUS", nullptr},
    {"CTX", nullptr},
    {"CAMWALL", nullptr},
    {"NEWFN", nullptr},
    {"MARKER", nullptr},
    {"DIAG", nullptr},
    {"ICONS", nullptr},
    {"HUD:", nullptr},
    {"HUD marker", nullptr},
    {"HUDDIAG", nullptr},
    {"HUDCOPY", nullptr},
    {"UI:", nullptr},
    {"CMD2", nullptr},
    {"CMDKEY", nullptr},
    {"CMDMENU", nullptr},
    {"MENU2", nullptr},
    {"MENUDIAG", nullptr},
    {"MENUROW", nullptr},
    {"MENUTARGET", nullptr},
    {"MENUSLOW", nullptr},
    {"MENUDIM", nullptr},
    {"UIDISP", nullptr},
    {"ITEMS", nullptr},
    {"PIP", nullptr},
    {"Action pass", nullptr},
    {"Pass ~", nullptr},
    {"P~ target", nullptr},
    {"P~ R3", nullptr},
    {"P~ SHORTCUT", nullptr},
    {"SHORTCUT", nullptr},
    {"P~ MENU", nullptr},
    {"P1 PAD", nullptr},
    {"P1 TARGET", nullptr},
    {"INPUT2: #", "pressed"},
    {"INPUT2 READ", nullptr},
    {"INPUT2 MAP", nullptr},
    {"INPUT2 OUTPUT", nullptr},
    {"TARGETLOG", nullptr},
    {"PARTYHUD", nullptr},
    {"CAMERA", nullptr},
    {"CINEMATIC", nullptr},
    {"TALKSCENE", nullptr},
    {"TALKDIAG", nullptr},
    {"CUTSCENE", nullptr},
    {"DAMAGE:", nullptr},
    {"ATTACKRATE", nullptr},
    {"BALANCE:", nullptr},
    {"ATTENTION", nullptr},
    {"TGTFILTER", nullptr},
    {"TGTDIAG", nullptr},
    {"TGTPICK", nullptr},
    {"DANGER", nullptr},
    {"RUMBLE", nullptr},
    {"GUARD", nullptr},
    {"PLAYER RULES", nullptr},
    {"SMOOTH", nullptr},
    {"RETICLE", nullptr},
    {"RETANIM", nullptr},
    {"PERF", nullptr},
    {"STAGGER", nullptr},
    {"NAMECHK", nullptr},
    {"PAGES", nullptr},
    {"PRESSURE", nullptr},
    {"AUDIO", nullptr},
    {"LOCKON", nullptr},
    {"LOCKDIAG", nullptr},
    {"FIELD:", nullptr},
    {"KEYBOARD:", nullptr},
    {"Player ~ AI", nullptr},
    {"Player ~: the AI", nullptr},
    {"LEADER:", nullptr},
    {"SWITCH", nullptr},
    {"KOLEAD", nullptr},
    {"TURNKEYS", nullptr},
    {"HOTKEY", nullptr},
    {"HELP", nullptr},
    {"DYNQ", nullptr},
    {"OWNHUD", nullptr},
    {"PARTYBAL: battle", nullptr},
    {"DINPUT", nullptr},
    {"XINPUT", nullptr},
    {"INPUTDIAG", nullptr},
    {"TGTSCAN", nullptr},
    {"INPUT2 PATH", nullptr},
};
// (code review: "ICONS: not available", "ITEMS: dispatch hook failed", "PIP: couldn't create ...", "CTX: ... OFF -
// a focus hook failed" etc. were dropped from the short log): failure words are matched ignoring case, more of them.
// Checked on the f1.149 log: +94 lines (TGTFILTER "can't pick it", "CAMERA: can't fit everyone", PARTYHUD "OFF -" ...).
static const char* const kLogAlways[] = {"not installed", "not found", "failed", "disabled", "exception", "crash", "error",
    "not as expected", "doesn't match", "unavailable", "not available", "couldn't", "can't", "warning", " off -"};
static const char* const kLogForceDetail[] = {"DIAG "};   // research counters whose text holds "failed" (DIAG SWAP: "N failed")
static bool LogPrefix(const char* t, const char* p)
{
    while (*p)
    {
        if (*p == '~') { if (*t < '0' || *t > '9') return false; while (*t >= '0' && *t <= '9') ++t; ++p; continue; }
        if (*t != *p) return false;
        ++t; ++p;
    }
    return true;
}
static bool LogIsDetail(const char* t)
{
    for (const char* f : kLogForceDetail) if (!strncmp(t, f, strlen(f))) return true;
    {   // the failure words, ignoring case (kLogAlways is lower case)
        char low[1024]; size_t i = 0;
        for (; t[i] && i < sizeof(low) - 1; ++i) low[i] = (t[i] >= 'A' && t[i] <= 'Z') ? (char)(t[i] + 32) : t[i];
        low[i] = 0;
        for (const char* a : kLogAlways) if (strstr(low, a)) return false;
    }
    for (const LogRule& r : kLogDetail) if (LogPrefix(t, r.prefix) && (!r.contains || strstr(t, r.contains))) return true;
    return false;
}
// (the author: twice a test round was lost because the line we needed had used up its lifetime count in the first
// minutes - the reticle lines by 08:29, the stagger lines before the 08:57 test): a per-minute allowance instead.
// LogOk(r, n) = true for at most n lines in each 60 s; the count starts again every minute, so a line can show up at any
// time in a session while the log stays about the same size.
struct LogRate { int n = 0; ULONGLONG t0 = 0; };
static bool LogOk(LogRate& r, int perMin)
{
    ULONGLONG now = GetTickCount64();
    if (!r.t0 || now - r.t0 >= 60000) { r.t0 = now; r.n = 0; }
    if (r.n >= perMin) return false;
    ++r.n; return true;
}
__attribute__((format(printf, 1, 2)))
static void CoopLog(const char* fmt, ...);   // the compiler checks every format
static void CoopLog(const char* fmt, ...)
{
    // (the author, f1.207): a long play must fit in the log. Once per launch: a log over 20 MB (LOG_KEEP_AT_START) is set
    // aside and a new one started; during a session it's set aside past 40 MB (LOG_RESTART_IN_SESSION). "Set aside" =
    // renamed to FF7RCoopMod_Coop_previous.log (replacing an older one) - nothing is lost silently; the new log's first
    // line says so. Only if the rename fails (another program holds the file without sharing it) is the log deleted, as
    // before f1.207. The old extra logs (Pad2 / Debug) are still deleted at launch.
    // the line is formatted first (DetailedLog decides on its text), and the file stays open (one lock, flushed per
    // line - was opened / closed for every line). Shared for reading, so the log can be opened while the game runs. A
    // CRITICAL_SECTION (not an SRW lock): the crash reporter may log on a thread that already holds it.
    char msg[4096];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    if (!g_DetailedLog && LogIsDetail(msg)) return;
    static CRITICAL_SECTION* cs = [] { static CRITICAL_SECTION c; InitializeCriticalSection(&c); return &c; }();
    static FILE* f = nullptr;
    static bool housekept = false;
    static unsigned lines = 0;
    static const char* setAsideNote = nullptr;   // written as the new log's first line after a set-aside
    static constexpr unsigned long long LOG_KEEP_AT_START = 20ull * 1024 * 1024;
    static constexpr long LOG_RESTART_IN_SESSION = 40L * 1024 * 1024;
    // 1 = renamed to _previous, 2 = couldn't rename, deleted it instead, 0 = neither (still in use - written on)
    auto setAside = []() -> int {
        if (MoveFileExW(L"FF7RCoopMod_Coop.log", L"FF7RCoopMod_Coop_previous.log", MOVEFILE_REPLACE_EXISTING)) return 1;
        return DeleteFileW(L"FF7RCoopMod_Coop.log") ? 2 : 0;
    };
    static const char* const kAtStart[3] = {
        "LOG: the last log is over 20 MB but could not be renamed or deleted (open in another program?) - written on",
        "LOG: the last log was over 20 MB - it is now FF7RCoopMod_Coop_previous.log; this one starts fresh",
        "LOG: the last log was over 20 MB and could not be renamed (open in another program?) - it was deleted"};
    static const char* const kInSession[3] = {
        "LOG: the log passed 40 MB but could not be renamed or deleted (open in another program?) - written on",
        "LOG: the log passed 40 MB - everything before this line is in FF7RCoopMod_Coop_previous.log",
        "LOG: the log passed 40 MB and could not be renamed (open in another program?) - the earlier part was deleted"};
    EnterCriticalSection(cs);
    if (!housekept)
    {
        housekept = true;
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (GetFileAttributesExW(L"FF7RCoopMod_Coop.log", GetFileExInfoStandard, &fa) &&
            (((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) > LOG_KEEP_AT_START)
            setAsideNote = kAtStart[setAside()];
        DeleteFileW(L"FF7RCoopMod_Pad2.log");
        DeleteFileW(L"FF7RCoopMod_Debug.log");
    }
    if ((++lines & 1023) == 0 && f && ftell(f) > LOG_RESTART_IN_SESSION)
    {
        fclose(f); f = nullptr;
        setAsideNote = kInSession[setAside()];
    }
    if (!f) f = _wfsopen(L"FF7RCoopMod_Coop.log", L"a", _SH_DENYNO);
    if (f)
    {
        SYSTEMTIME t; GetLocalTime(&t);
        if (setAsideNote)
        {
            fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, setAsideNote);
            setAsideNote = nullptr;
        }
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
        fflush(f);
    }
    LeaveCriticalSection(cs);
}

// ---- writing code into the game (core/28 MakeInlineHook, enemies/23 PatchLeaderSite) ----
static int g_HooksFailed = 0;   // (finding #35) hooks / patches that couldn't go in (the bytes differ - a game update?): told on screen once (marker/20)
// (finding #34): the game's other threads are paused while a hook's first bytes are rewritten (what Microsoft Detours /
// MinHook do) - a thread running those bytes at that very moment could crash. A paused thread whose next instruction is
// INSIDE the bytes being replaced is let go for a moment and paused again (up to 20 tries). Between the pause and the
// resume nothing may take a lock - no log line, no memory allocation (a paused thread could be holding it).
struct OtherThreadsPaused
{
    static constexpr int MAX_THREADS = 1024;
    HANDLE h[MAX_THREADS]; int n = 0;
    bool clear = false;   // true = no paused thread is inside the patched bytes
    int  notPaused = 0;   // threads that couldn't be paused (more than MAX_THREADS, or OpenThread / SuspendThread failed)
    OtherThreadsPaused(uintptr_t from, size_t len)
    {
        for (int attempt = 0; attempt < 20 && !clear; ++attempt)
        {
            if (attempt) { Resume(); Sleep(1); }
            Pause();
            clear = !AnyInside(from, len);
        }
    }
    ~OtherThreadsPaused() { Resume(); }
    void Pause()
    {
        static DWORD ids[MAX_THREADS]; int m = 0;   // the ids first (the snapshot is closed before anything is paused)
        DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();
        notPaused = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE)
        {
            THREADENTRY32 te; te.dwSize = sizeof(te);
            if (Thread32First(snap, &te))
                do
                {
                    if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(te.th32OwnerProcessID) &&
                        te.th32OwnerProcessID == pid && te.th32ThreadID != me)
                    { if (m < MAX_THREADS) ids[m++] = te.th32ThreadID; else ++notPaused; }
                    te.dwSize = sizeof(te);
                } while (Thread32Next(snap, &te));
            CloseHandle(snap);
        }
        for (int i = 0; i < m; ++i)
        {
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, ids[i]);
            if (!t) { ++notPaused; continue; }
            if (SuspendThread(t) == (DWORD)-1) { CloseHandle(t); ++notPaused; continue; }
            h[n++] = t;
        }
    }
    void Resume() { for (int i = 0; i < n; ++i) { ResumeThread(h[i]); CloseHandle(h[i]); } n = 0; }
    bool AnyInside(uintptr_t from, size_t len)
    {
        for (int i = 0; i < n; ++i)
        {
            CONTEXT c; memset(&c, 0, sizeof(c)); c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h[i], &c) && c.Rip >= from && c.Rip < from + len) return true;   // (also waits for the pause to land)
        }
        return false;
    }
};
