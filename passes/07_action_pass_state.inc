// =================================================================================
// ACTION PASS 2 (player 2 battle actions)
// =================================================================================
// From the v3.1 stack captures: the player's battle actions come from a "battle command manager"
// update, 0x1418010(self, dt), called once per frame from the HUD manager. It reads buttons through
// the command widget's key-state lookup 0x121A310(widget, FKey) (0 = up, 1 = just pressed, 2 = held)
// and gets the acting character from the party manager's "get leader" 0x15F1CE0(mgr, flag).
// The extra players' passes run the update again with "get leader" returning their character and the key
// lookup answering from their pad.
static constexpr uintptr_t RVA_BCM_UPDATE   = 0x1418010;
static constexpr uintptr_t RVA_GET_LEADER   = 0x15F1CE0;
static constexpr uintptr_t RVA_WIDGET_KEY   = 0x121A310;
typedef void  (*BcmUpdate_t)(void* self, float dt);
typedef void* (*GetLeader_t)(void* mgr, unsigned char flag);
typedef uint64_t (*WidgetKey_t)(void* widget, void* key);
static BcmUpdate_t Orig_BcmUpdate = nullptr;
static GetLeader_t Orig_GetLeader = nullptr;
static WidgetKey_t Orig_WidgetKey = nullptr;
static bool g_InActionPass2 = false;
static bool g_InMainSysPass = false;   // an extra player's pass on PLAYER 1's command system (BcmAfter) - no palette keys there
static void* g_ActionP2Char = nullptr;

// The battle keys the extra players' command passes answer from their own pad (menus/08b Hook_WidgetKey). f1.85 (review A19):
// each row points at its setting (read at the time of the call - player 2's or player 3's, whoever is loaded) - before, every
// key the game asked about re-picked all 9 rows' buttons by name (~1000 string compares a frame).
struct KeyMap { uintptr_t rvaFKey; DWORD PlayerCfg::* button; int slot; const char* name; uint64_t fname; };   // slot 1-4 = Shortcut1-4 (bShortcut[slot - 1])
static KeyMap g_KeyMap[] = {
    {RVA_KEY_ATTACK,  &PlayerCfg::bAttack,  0, "Attack", 0},          // X
    {RVA_KEY_DODGE,   &PlayerCfg::bDodge,   0, "Dodge", 0},           // B
    {RVA_KEY_GUARD,   &PlayerCfg::bGuard,   0, "Guard", 0},           // RB
    {RVA_KEY_ABILITY, &PlayerCfg::bAbility, 0, "UniqueAbility", 0},   // Y
    // shortcut palette. ShortcutMenu is both the "open palette" press and the hold the BCM checks
    // (bcm+0x558 holds a copy of the same FKey). Shortcut1-4_Second pick a slot while it's held.
    {RVA_KEY_SHORTCUTMENU, &PlayerCfg::bShortcutHold, 0, "ShortcutMenu", 0},   // LB
    {RVA_KEY_SLOT[0], nullptr, 1, "Shortcut1", 0},
    {RVA_KEY_SLOT[1], nullptr, 2, "Shortcut2", 0},
    {RVA_KEY_SLOT[2], nullptr, 3, "Shortcut3", 0},
    {RVA_KEY_SLOT[3], nullptr, 4, "Shortcut4", 0},
    // R3 (TargetLock) is no longer fed into the shared command manager - player 2's own
    // targeting code (P2 TARGETING section) handles R3 and the right stick instead.
};

static bool g_InMainBcmPass = false;             // inside the game's own command pass
// (The own-menu paging state - internalTurn, pageReq, lastPageDir, aiPageType ... - is in each player's record, core/02.)
static ULONGLONG g_CtxConfirmT[4] = {};   // last confirm (A) sent into a player's own menu
static int g_CtxAutoClose[4] = {};        // 1 = send a made Cancel (down) next, 2 = its key-up next (OwnMenuCloseAfterCommand)
// f1.233: while a boss's damage event handler runs for a hit by player 2 / 3 (enemies/22f BossEventPre), "the leader" is the
// one who hit it - its script starts a phase change only for the leader's hit (S14 / S15: player 2's hits on the floor did
// nothing, player 1's next hit started the phase at once)
static void* g_LeaderOverride = nullptr;
static int g_LeaderOverrideAsked = 0;
static void* Hook_GetLeader(void* mgr, unsigned char flag)
{
    void* r = Orig_GetLeader(mgr, flag);
    if (g_GateUntil) GateLogLeaderAsk(0, (uintptr_t)__builtin_return_address(0) - g_Base - 5, -1);   // f1.246 (log only)
    if (g_LeaderOverride && GetCurrentThreadId() == g_GameThreadId) { ++g_LeaderOverrideAsked; return g_LeaderOverride; }
    if (g_InActionPass2 && g_ActionP2Char) return g_ActionP2Char;
    return r;
}
// (the author, f1.118 / f1.119 runs: players 2/3's closed command bar "shows Cloud's bar in all three windows"; with
// QuietPageSetup their menu pages WERE set up on Barret / Tifa and the bar still showed Cloud). RE (exe 2026-10-24):
// 0x1420650(rcx) = "which party slot is the leader": it reads the party manager's leader TYPE byte (0x657AC0()+0x233) and
// returns its index in the order (+0x230..), 0 if not below [rcx+0x10] - it does NOT go through GetLeader (0x15F1CE0), so
// the GetLeader override above never reached it. The presenter (bcm+0x38) stores it at presenter+0x88 every update
// (0x13C55E0, called from the command-system update 0x14188AB) and the closed HUD's cells are filled from that slot
// (0x13C92EC / 0x13C9E50 / 0x13CB6FC read +0x88); the presenter refresh and the command-system update call it ~20 more
// times. So in players 2/3's own system it said "Cloud's slot". Now, only while their own command system updates or
// refreshes (g_OwnLeaderIdx set by passes/12 BcmAfter2), it answers their own slot - the same character GetLeader already
// answers there. (always on - the OwnHudCharacter setting was removed.)
typedef int (*LeaderIdx_t)(void* rcx);
static LeaderIdx_t Orig_LeaderIdx = nullptr;
static int g_OwnLeaderIdx = -1;
static int Hook_LeaderIdx(void* rcx)
{
    int r = Orig_LeaderIdx(rcx);
    if (g_GateUntil) GateLogLeaderAsk(1, (uintptr_t)__builtin_return_address(0) - g_Base - 5, r);   // f1.246 (log only)
    if (g_OwnLeaderIdx >= 0 && g_OwnLeaderIdx < 3 && rcx && Readable((unsigned char*)rcx + 0x10, 4)
        && g_OwnLeaderIdx < *(int*)((unsigned char*)rcx + 0x10)) return g_OwnLeaderIdx;   // (the game's own bound check)
    return r;
}


// ---- v3.6 MENU DIAGNOSTIC ----
// Logs (1) every key the battle command manager sees player 1 press, by name, and (2) a snapshot of
// the command manager's page/menu state every time it changes - so we can see which field means
// "command menu open" and which character's page is showing.




