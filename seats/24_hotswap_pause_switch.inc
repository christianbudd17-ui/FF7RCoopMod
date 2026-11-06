// ---- v6.1 / v12.1 / f1.9: seats and hot swap ----
// Seats: 0 = the leader's (player 1's at the start), 1 = player 2's, 2 = player 3's (if on). Each holds the keyboard,
// nobody or one controller - since f1.9 by identity in the seats module (input2/seats, glue input/40b), no slot numbers.
// The leader seat's controller is the one the game reads (its XInput slot 0); a seat holding the keyboard / nobody
// has no controller, so its character goes to the AI.
static void SeatApply(bool hasController)   // on the loaded player (2 or 3), after their seat got something new
{
    g_Player->cmdButtons = g_Player->cmdButtonsPrev = g_Player->padNow = 0;
    g_Player->joinHeldSince = 0; g_Player->joinFired = true;   // a held Start doesn't immediately toggle joining
    if (!hasController)
    {
        if (g_Player->joined) { SetCoop(false, "players swapped - the keyboard can't play a teammate"); g_Player->autoLeft = true; }   // back in when a controller comes round
        g_Player->connected = false;
    }
    else g_Player->connLostAt = 0;   // the connection check picks the new controller up (and joins back if this seat was left by a swap)
}
// (PersonLabel(person) was PlayerLabel(person + 1) written out again - person = 0-based, the name follows the person, v12.1)
// (the author): one key per person. Pressing person X's key makes X the leader: X and whoever
// leads right now trade seats (characters), each keeping their own controller / keyboard. Nobody else moves.
static void TradeLead(int person)
{
    int n = g_P3Enabled ? 3 : 2;
    wchar_t who[40]; PlayerLabel(person + 1, who, 40);
    wchar_t msg[160];
    if (person >= n) { CoopLog("SEATS: player 3's lead key pressed but player 3 is switched off"); HudNotice(L"Player 3 is switched off in the settings"); return; }
    int s = -1; for (int i = 0; i < n; ++i) if (g_SeatPerson[i] == person) s = i;
    if (s < 0) return;
    if (s == 0) { swprintf(msg, 160, L"%ls is already leading", who); HudNotice(msg); return; }
    if (!In2Running())
    {   // without the controller module only the leader has a controller (XInput slot 0) - nothing to trade
        CoopLog("SEATS: lead key pressed but the controller module isn't running - hot swap needs it");
        HudNotice(L"Hot swap isn't available (the controller module didn't start - see the log)"); return;
    }
    bool leadHas = In2SeatHasController(0), sHas = In2SeatHasController(s), in = false;
    bool sAutoWait = In2SeatAutoWaiting(s);   // on Auto, no pad found yet (counts as no controller, but is "theirs")
    if (s == 1) { WithP2 w; in = g_Player->joined; } else { WithP3 w; in = g_Player->joined; }
    if ((sHas || sAutoWait) && !in)
    {   // they left (join key / button) or their controller is gone
        CoopLog("SEATS: %ls's lead key pressed but they aren't playing right now (seat %d not joined)", who, s + 1);
        swprintf(msg, 160, L"%ls isn't playing right now - join first", who); HudNotice(msg); return;
    }
    if (!sHas && !leadHas) { swprintf(msg, 160, L"%ls has no controller", who); HudNotice(msg); return; }
    if (g_Cfg.keyboardKeepsLead && !leadHas)
    {   // (the author; confirmed in the f1.79 test): the game reads the keyboard only for the leader, so a swap away from a keyboard leader
        // leaves that player watching the AI play their new character. Refused while [General] KeyboardLeaderKeepsLead=1.
        wchar_t lk[40]; PlayerLabel(g_SeatPerson[0] + 1, lk, 40);
        CoopLog("SEATS: %ls's lead key ignored - player %d leads on the keyboard (KeyboardLeaderKeepsLead=1)", who, g_SeatPerson[0] + 1);
        swprintf(msg, 160, L"Hot swap disallowed - %ls's primary input is the keyboard", lk); HudNotice(msg); return;   // (the author's wording, 2026-10-26)
    }
    wchar_t lead[40]; PlayerLabel(g_SeatPerson[0] + 1, lead, 40);
    GameRumbleStop("hot swap");   // (code review B2): the old leader's pad stops the game's rumble
    In2SeatsTraded(0, s);   // the seats module trades what the two seats hold
    g_Cfg.p1Controller = sHas || sAutoWait;   // the leader seat plays on a controller now (or the keyboard; an Auto seat
                                              // still looking for a pad is keyboard until it finds one, then that pad leads)
    if (s == 1) { WithP2 w; SeatApply(leadHas); } else { WithP3 w; SeatApply(leadHas); }   // the other seat got the old leader's
    { int t = g_SeatPerson[0]; g_SeatPerson[0] = g_SeatPerson[s]; g_SeatPerson[s] = t; }
    char a[180], b[180];
    In2SeatText(0, a, sizeof(a)); In2SeatText(s, b, sizeof(b));
    CoopLog("SEATS: player %d (as they started) now leads with %s; player %d (was leading) moves to seat %d with %s%s - seats now hold players %d, %d%s",
        person + 1, a, g_SeatPerson[s] + 1, s + 1, b, !leadHas ? " (the AI plays that character until a controller comes round)" : "",
        g_SeatPerson[0] + 1, g_SeatPerson[1] + 1, n == 3 ? (g_SeatPerson[2] == 0 ? ", 1" : g_SeatPerson[2] == 1 ? ", 2" : ", 3") : "");
    if (!leadHas) swprintf(msg, 160, L"%ls leads now  -  %ls's character goes to the AI (keyboard)", who, lead);
    else swprintf(msg, 160, L"%ls leads now  -  %ls plays their character", who, lead);
    HudNotice(msg);
}
static void PollSwapKey()
{
    static bool wasDown[3] = {false, false, false};
    for (int i = 0; i < 3; ++i)
    {   // exact modifiers, so F9 and Ctrl+F9 never both fire
        bool down = HotkeyDown(g_Cfg.leadVk[i], g_Cfg.leadCtrl[i], g_Cfg.leadShift[i], g_Cfg.leadAlt[i]);
        if (down && !wasDown[i]) { if (HotkeysAllowed()) TradeLead(i); else NoteHotkeyIgnored("hot swap key"); }
        wasDown[i] = down;
    }
}

// ---- v6.1: game paused (pause menu, tutorials, help screens) ----
// While the game is paused the extra player passes are skipped and any half-open player menu is
// released, so nothing of ours runs on top of the game's own pause screens.
static void* FindObj(const char* name, const char* clsName);   // defined in the v5.6 section
static bool GamePausedNow(void* pc)
{
    static int state = 0; static void* cdo = nullptr; static void* fn = nullptr; static int pW = -1, pR = -1;
    if (state == 0)
    {
        state = -1;
        cdo = FindObj("Default__GameplayStatics", "GameplayStatics");
        fn = cdo ? FindFunc(cdo, "IsGamePaused") : nullptr;
        if (fn) { pW = ParamOff(fn, "WorldContextObject"); pR = ParamOff(fn, "ReturnValue"); }
        if (fn && pW >= 0 && pR >= 0 && ParmsSize(fn) <= 64) state = 1;
        CoopLog("PAUSE: IsGamePaused %s", state == 1 ? "found" : "NOT found - pause detection off");
    }
    if (state != 1 || !pc) return false;
    alignas(16) unsigned char b[64] = {};
    memcpy(b + pW, &pc, 8);
    Orig_ProcessEvent(cdo, fn, b);
    return b[pR] != 0;
}

static void ApplyP2AI(bool coopOn);
// short character name from the object name, e.g. "PC0001_00_Barret_Standard_C_0" -> "Barret"
static void CharShortName(void* c, wchar_t* out, int n)
{
    char nm[96] = "?"; if (c) NameOf(c, nm, sizeof(nm));
    const char* s = nm; char part[40] = "";
    if (!strncmp(nm, "PC", 2))
    {
        const char* a = strchr(nm, '_'); if (a) a = strchr(a + 1, '_');
        if (a) { ++a; const char* b = strchr(a, '_'); size_t L = b ? (size_t)(b - a) : strlen(a); if (L > 39) L = 39; memcpy(part, a, L); part[L] = 0; s = part; }
    }
    swprintf(out, n, L"%hs", s);
}
// D-pad up / down (PS4 style) - this player switches to a different teammate, but only to one
// the AI is playing (never player 1's or another person's character). Their old character goes back
// to the AI. Works in battle and in the field; not while a menu is open, paused or in a cutscene.
static void P2SwitchTick()
{
    if (!Real_XInputGetState) return;
    DWORD b = g_Player->padFrameOk ? PadBits(g_Player->padFrame.Gamepad) : 0;   // this frame's read (passes/05 ReadPadThisFrame)
    DWORD pressed = b & ~g_Player->switchPrevButtons;
    g_Player->switchPrevButtons = b;
    if (!g_Player->joined || g_GamePaused || g_InCutscene || g_MenuOwner || !GetPartyMember) return;
    if (OwnCmdMenuOpen()) return;   // (code review C1): the D-pad moves the cursor in their own command menu, not their character
    if (b & g_Player->cfg->bShortcutHold) return;
    int dir = 0;
    if (g_Player->cfg->bSwitchNext && (pressed & g_Player->cfg->bSwitchNext)) dir = 1;
    else if (g_Player->cfg->bSwitchPrev && (pressed & g_Player->cfg->bSwitchPrev)) dir = -1;
    if (!dir) return;
    unsigned char* mgr = PartyMgr(); if (!mgr) return;
    void* cur = P2Char();   // makes sure charType is up to date
    int lead = LeaderType(mgr), list[3], k = 0, at = -1;
    ForEachPartyMember(mgr, [&](PartyMember& m) {   // rule: their own character + AI members (not player 1's / the other human's)
        int t = m.type;
        if (t < 0 || t == lead || (g_OtherType >= 0 && t == g_OtherType) || !m.Char()) return;
        if (t != g_Player->charType && !NotKnockedOut(m.Char())) return;   // (test): never onto a knocked-out character
        if (t == g_Player->charType) at = k;
        list[k++] = t;
    });
    wchar_t who[40]; PlayerLabel(g_Player->num, who, 40);
    wchar_t msg[120];
    if (!cur || at < 0 || k < 2)
    {
        CoopLog("SWITCH: player %d pressed the D-pad - no other AI teammate to switch to (%d free)", g_Player->num, k);
        swprintf(msg, 120, L"%ls: no AI teammate to switch to", who); HudNotice(msg);
        return;
    }
    int nt = list[(at + dir + k) % k];
    int old = g_Player->charType;
    g_Player->charType = nt;
    ResetP2Targeting();
    void* nc = CharByType(mgr, nt);
    wchar_t cn[40]; CharShortName(nc, cn, 40);
    CoopLog("SWITCH: player %d now plays type %d (was %d) - the old character goes back to the AI", g_Player->num, nt, old);
    swprintf(msg, 120, L"%ls is now playing %ls", who, cn); HudNotice(msg);
}

// (R1, the author: "P1 did not switch to the AI player with up/down"): the leader's D-pad switch is the GAME's own
// (SwitchToCharacter), the mod only redirects / blocks it (seats/13). This watches the leader's controller in battle:
// with no AI teammate to switch to (everyone else is a person or knocked out) it says so on screen, like players 2/3
// get; otherwise it logs the press and, a moment later, whether the game switched - so the next log shows where it stops.
static void LeaderSwitchWatch()
{
    static WORD prevB = 0; static ULONGLONG checkAt = 0; static int leadBefore = -1, logs = 0;
    if (!GetPartyMember || !g_Base) return;
    unsigned char* pm = PartyMgr(); if (!pm) return;
    ULONGLONG now = GetTickCount64();
    if (checkAt && now >= checkAt)
    {
        checkAt = 0;
        int lead = LeaderType(pm);
        if (logs < 60) { ++logs; if (lead == leadBefore) CoopLog("SWITCH: the game did NOT switch the leader after the D-pad press (still type %d, battle ui %d)", lead, g_LastBattleUi); else CoopLog("SWITCH: the game switched the leader: type %d -> %d", leadBefore, lead); }
    }
    XINPUT_STATE st{}; WORD b = 0;
    if (g_Cfg.p1Controller && In2SeatHasController(0) && PadGetSeat(0, &st) == ERROR_SUCCESS) b = st.Gamepad.wButtons & 0x0003;   // D-pad up / down
    WORD pressed = b & ~prevB; prevB = b;
    if (!pressed) return;
    bool inBattle = (now - g_LastBcmTick < IN_BATTLE_MS) && g_LastBattleUi == 8;   // a battle, the leader's command menu closed
    if (!inBattle || g_GamePaused || g_InCutscene || g_MenuOwner) return;
    void* p2 = g_Player->joined ? P2Char() : nullptr;
    void* p3 = g_P3Human ? g_P3HumanChar : nullptr;
    int lead = LeaderType(pm), n = PartyCount(), free = 0, people = 0;
    ForEachPartyMember(pm, [&](PartyMember& m) {   // rule: everyone but the leader - people counted, AI members not knocked out
        int t = m.type; if (t < 0 || t == lead) return;
        void* c = m.Char(); if (!c) return;
        if (c == p2 || c == p3) { ++people; return; }
        if (NotKnockedOut(c)) ++free;
    });
    const char* dir = (pressed & 0x0001) ? "up" : "down";
    if (!free && people > 0)   // (a party of 1 - a solo story fight - has nobody to switch to anyway: no notice)
    {
        if (logs < 60) { ++logs; CoopLog("SWITCH: the leader pressed D-pad %s - no AI teammate to switch to (%d played by people, party of %d)", dir, people, n); }
        HudNotice(L"No AI teammate to switch to");
        return;
    }
    if (!free) return;
    leadBefore = lead; checkAt = now + 800;
    if (logs < 60) { ++logs; CoopLog("SWITCH: the leader pressed D-pad %s - %d AI teammate(s) free, the game should switch", dir, free); }
}
