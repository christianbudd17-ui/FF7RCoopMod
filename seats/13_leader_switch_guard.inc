// ---- v4.4: player 1 switching block ----
// Every leader change goes through the party manager's SetPartyLeader (0x15F18B0, args: manager,
// new leader character, two flags). During a battle with player 2 in, a switch onto player 2's
// character is redirected to an AI-controlled party member if there is one (3-person party), or
// cancelled (2-person party). Outside battle nothing is blocked, so story/scripted leader changes
// are left alone. Every call is logged once per calling site, so we can see which code switches.
static constexpr uintptr_t RVA_FIELD_LEADER_RESTORE = 0x9B5224;   // f1.234: the game's "field leader back" call (0x9B4A00)
static constexpr uintptr_t RVA_SET_LEADER = 0x15F18B0;
static volatile bool g_InSwitchTo = false;   // (moved up from below for f1.245) inside the game's character-switch routine (D-pad / KO)
typedef void (*SetLeader_t)(void* mgr, void* newLeader, bool f1, bool f2);
static bool BattleWorldOn();   // enemies/18 (f1.248)
static bool QuietLeadAsk(void* mgr, void* newLeader, bool f1, bool f2, uintptr_t site);   // seats/24b (f1.248)
static ULONGLONG g_QuietBattleSeenAt = 0;   // f1.248: the last frame InBattleNow() was true (seats/24b tick)
static ULONGLONG g_FieldRestoreAt = 0;   // f1.260: the game's battle-end "field leader back" call (0x9B5224) - when
static volatile bool g_QuietSwitching = false;   // f1.248: the mod's own hand-back switch (seats/24b) is running
static void* g_QuietWant = nullptr;               // ... onto this character
// f1.245 LOG ONLY (the author: "add logging to look for other instances where the game tries to demand a specific
// character is put to the lead during battle ... it may happen in the shinra building for instance, where Tifa is made
// into the party leader to do a platforming section"): every leader change the game's story / fight scripts ask for -
// not the game's character-switch routine (D-pad / KO), not the battle-end field-leader call (0x9B5224), not a call that
// names the leader it already has - in battle AND in the field, with who plays that character and the boss(es) in the
// battle. Max 80 lines a session, one per site + character per 5 s. On the end-of-project removal checklist.
static const char* BossFirstName();   // enemies/22d (f1.245)
static void LeaderScriptLog(void* newLeader, uintptr_t site, bool inBattle)
{
    static int logs = 0; if (logs >= 80 || !newLeader || g_InSwitchTo || site == RVA_FIELD_LEADER_RESTORE) return;
    void* lead = LeaderChar();
    if (newLeader == lead) return;
    static struct { uintptr_t site; void* who; ULONGLONG at; } last[16]; static int nLast = 0;
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < nLast; ++i) if (last[i].site == site && last[i].who == newLeader) { if (now - last[i].at < 5000) return; last[i].at = now; goto logIt; }
    if (nLast < 16) { last[nLast].site = site; last[nLast].who = newLeader; last[nLast].at = now; ++nLast; }
logIt:
    ++logs;
    const char* who = "an AI teammate";
    { WithP2 w; if (g_Player->joined && P2Char() == newLeader) who = "player 2's"; }
    if (g_P3Human && g_P3HumanChar == newLeader) who = "player 3's";
    char nm[96] = "?"; NameOf(newLeader, nm, sizeof(nm));
    char ln[96] = "nobody"; if (lead) NameOf(lead, ln, sizeof(ln));
    const char* boss = inBattle ? BossFirstName() : nullptr;
    CoopLog("LEADERSCRIPT: the game asks for %s as the leader (%s character) in %s%s%s - leader now %s, from RVA 0x%llX%s",
        nm, who, inBattle ? "a battle" : "the field", boss ? " with the boss " : "", boss ? boss : "", ln, (unsigned long long)site,
        !strcmp(who, "an AI teammate") ? "" : (inBattle ? " (a person's - the switch block decides below)" : " (field - nothing blocked)"));
}
static SetLeader_t Orig_SetLeader = nullptr;
static void Hook_SetLeader(void* mgr, void* newLeader, bool f1, bool f2)
{
    uintptr_t site = (uintptr_t)__builtin_return_address(0) - g_Base - 5;
    {
        static uintptr_t seen[64]; static int nSeen = 0; bool found = false;
        for (int i = 0; i < nSeen; ++i) if (seen[i] == site) found = true;
        if (!found && nSeen < 64)
        {
            seen[nSeen++] = site;
            char nm[96] = "null"; if (newLeader) NameOf(newLeader, nm, sizeof(nm));
            CoopLog("LEADER: SetPartyLeader(%s, %d, %d) called from RVA 0x%llX", nm, (int)f1, (int)f2, (unsigned long long)site);
        }
    }
    if (g_QuietSwitching) { Orig_SetLeader(mgr, newLeader, f1, f2); return; }   // f1.248: the mod's own hand-back (seats/24b) - as asked
    if (site == RVA_FIELD_LEADER_RESTORE) g_FieldRestoreAt = GetTickCount64();   // f1.260
    bool inBattle = InBattleNow();
    WithP2 base;
    void* p2 = (g_Player->joined && inBattle) ? P2Char() : nullptr;
    void* p3 = (inBattle && g_P3Human) ? g_P3HumanChar : nullptr;
    LeaderScriptLog(newLeader, site, inBattle);   // f1.245 (log only)
    if (!g_InSwitchTo && !g_GamePaused && site != RVA_FIELD_LEADER_RESTORE && newLeader && newLeader != LeaderChar())
    {   // f1.246 / f1.247 (log only): the game's script wants a person's character to lead - watch what runs next.
        // f1.247: no longer only "in a battle" - f1.246 test 20:58:33: the Failed Experiment's ask came 0.6 s into its
        // catwalk shot, when the battle menu was already away (InBattleNow false), so neither the block nor the watch ran.
        void* a2 = g_Player->joined ? P2Char() : nullptr;
        void* a3 = g_P3Human ? g_P3HumanChar : nullptr;
        if ((a2 && newLeader == a2) || (a3 && newLeader == a3)) GateWatchStart(newLeader, site, inBattle);
    }
    if (((p2 && newLeader == p2) || (p3 && newLeader == p3)) && g_GamePaused)
    {   // (the author, f1.195 test: after a hot swap - player 1 on Tifa, player 3 on Cloud - "Return to main menu"
        // from the pause menu in a battle hung the game; the log ends right after this hook refused the game's own
        // SetPartyLeader(Cloud) from 0x9B5224): the game is leaving its pause menu (quit to the title / retry) - its own
        // leader change goes through. g_GamePaused is still the pause menu's value here (set after the game's tick).
        char nm[96] = "?"; NameOf(newLeader, nm, sizeof(nm));
        CoopLog("LEADER: switch to a human player's %s allowed - the game is leaving its pause menu (quit / retry, from RVA 0x%llX)", nm, (unsigned long long)site);
        Orig_SetLeader(mgr, newLeader, f1, f2);
        return;
    }
    if (((p2 && newLeader == p2) || (p3 && newLeader == p3)) && site == RVA_FIELD_LEADER_RESTORE)
    {   // f1.234 (the author, S16: "after the battle, P1 continued to play as Tifa ... unless there was a hot swap, P1 should
        // always be the leader, which in the field should be Cloud"): at a battle's end the game puts its FIELD leader back
        // (0x9B4A00 -> 0x9B5224: SetPartyLeader(the character of the save data's field-leader type, +0x5089B)). Player 2 had
        // D-padded onto Cloud mid-fight, so the redirect below gave player 1 the free AI member (Tifa) for the field. Now
        // that call goes through: player 1 is the story's field leader again and player 2 / 3 move to a free character
        // (P2Char picks their own party slot's character, else a free one, when theirs became the leader).
        char nm[96] = "?"; NameOf(newLeader, nm, sizeof(nm));
        CoopLog("LEADER: the game puts its field leader %s back (battle over, RVA 0x%llX) - allowed: player 1 takes it, player %d moves to a free character",
            nm, (unsigned long long)site, (p2 && newLeader == p2) ? 2 : 3);
        Orig_SetLeader(mgr, newLeader, f1, f2);
        return;
    }
    if (!g_InSwitchTo && !g_GamePaused && site != RVA_FIELD_LEADER_RESTORE && newLeader && newLeader != LeaderChar()
        && g_FieldRestoreAt && GetTickCount64() - g_FieldRestoreAt < 3000)
    {   // f1.260 (Failed Experiment f1.259 log 01:02:43: the battle-end field-leader call made Tifa the leader - player 1 took
        // her, player 2 moved to Barret (f1.234) - and 10 ms later the fight's script asked for Barret; the gate hold
        // (seats/24b) dropped that ask, so after the fight player 1 was on Tifa and player 2 on Barret, swapped. The author:
        // "Yes, fix that problem."): a script's leader ask within 3 s after that call goes through as asked - player 1 takes
        // it (the field rule), player 2 / 3 moves to the free character - here: back to Barret / Tifa as before the fight.
        void* a2 = g_Player->joined ? P2Char() : nullptr;
        void* a3 = g_P3Human ? g_P3HumanChar : nullptr;
        if ((a2 && newLeader == a2) || (a3 && newLeader == a3))
        {
            char nm[96] = "?"; NameOf(newLeader, nm, sizeof(nm));
            CoopLog("LEADER: the game asks for %s as the leader right after its battle-end field-leader call (RVA 0x%llX) - allowed: player 1 takes it, player %d moves to the free character",
                nm, (unsigned long long)site, (a2 && newLeader == a2) ? 2 : 3);
            Orig_SetLeader(mgr, newLeader, f1, f2);
            return;
        }
    }
    if (!g_InSwitchTo && !g_GamePaused && site != RVA_FIELD_LEADER_RESTORE && newLeader && newLeader != LeaderChar()
        && (inBattle || (BattleWorldOn() && GetTickCount64() - g_QuietBattleSeenAt < 10000))   // a battle, or its scripted shot (battle menu away) right after one
        && QuietLeadAsk(mgr, newLeader, f1, f2, site)) return;   // f1.248: the game's script asks for a person's character - a quiet hand-off
    if ((p2 && newLeader == p2) || (p3 && newLeader == p3))
    {
        unsigned char* pm = PartyMgr();
        void* leader = LeaderChar();   // (was GetPartyMember(0) = first in the party order, not the leader)
        void* alt = nullptr;
        ForEachPartyMember(pm, [&](PartyMember& m) {   // rule: an AI member (not a person's, not the leader) - the LAST one
            void* c = m.Char();
            if (c && c != p2 && c != p3 && c != leader && NotKnockedOut(c)) alt = c;   // not a KO'd one
        });
        char nm[96] = "?"; NameOf(newLeader, nm, sizeof(nm));
        static ULONGLONG lastLog = 0; bool logIt = GetTickCount64() - lastLog > 2000; if (logIt) lastLog = GetTickCount64();
        if (alt)
        {
            char an[96] = "?"; NameOf(alt, an, sizeof(an));
            if (logIt) CoopLog("LEADER: switch to a human player's %s redirected to AI member %s (from RVA 0x%llX)", nm, an, (unsigned long long)site);
            Orig_SetLeader(mgr, alt, f1, f2);
            return;
        }
        int hp = leader ? CharHP(leader) : -1;
        if (leader && hp == 0)
        {
            if (logIt) CoopLog("LEADER: player 1's character is down - the game's switch to a human player's %s refused (from RVA 0x%llX): player 1 stays the leader", nm, (unsigned long long)site);
            return;
        }
        if (logIt) CoopLog("LEADER: switch to a human player's %s blocked (from RVA 0x%llX, leader HP %d)", nm, (unsigned long long)site, hp);
        return;
    }
    Orig_SetLeader(mgr, newLeader, f1, f2);
}

// the game's whole "switch to character" routine is 0x9BAA80. It calls SetPartyLeader halfway and then hands the
// camera, the battle input state etc. over to the character IT picked.
// (RE 2026-10-19, after the f1.23 test: "switching characters still keeps the camera on Cloud"): its first argument
// is NOT a character - it's rcx = the player controller (+0x638 / +0x63C written), dl = direction (1 = next), r8b / r9b =
// flags. The routine picks the new character itself: from the leader it walks the party order in that direction and takes
// the first one that passes 0x67FD30 (a class check), unit +0x611 != 0xD and 0x9CC4C0(controller, character) - the
// per-type "can be switched to" check, called ONLY from here. So the v8.9 redirect (comparing the first argument with
// player 2's character) never matched: the routine went on with player 2's character, SetPartyLeader redirected only the
// leader, and the camera / battle input stayed behind. Now Hook_SwitchTo only marks "inside the switch" and
// Hook_CanSwitchTo makes the people-played characters fail the game's own check, so the routine itself picks the AI
// teammate (camera, input and leader all on it) or finds nobody and switches nothing.
// (g_InSwitchTo is defined at the top of this file since f1.245)
static constexpr uintptr_t RVA_SWITCH_PC = 0x57E9720;   // (RE): the global both game callers pass as the switch routine's
                                                        // first argument (KO auto-switch 0x98660E: dl=1 r8b=1 r9b=0; D-pad 0x141D98E)
typedef uint8_t (*SwitchTo_t)(void* pc, uint8_t dir, uint8_t f1, uint8_t f2);
static SwitchTo_t Orig_SwitchTo = nullptr;
static int g_SwitchSkippedPeople = 0;   // people-played characters Hook_CanSwitchTo turned down in this switch (player 1 up)
static uint8_t Hook_SwitchTo(void* pc, uint8_t dir, uint8_t f1, uint8_t f2)
{
    bool was = g_InSwitchTo; g_InSwitchTo = true;
    int skippedBefore = g_SwitchSkippedPeople; g_SwitchSkippedPeople = 0;
    unsigned char* pm = PartyMgr(); int leadBefore = LeaderType(pm);
    uint8_t r = Orig_SwitchTo(pc, dir, f1, f2);
    g_InSwitchTo = was;
    // (the author, f1.204 test: player 1 on the KEYBOARD got no "No AI teammate" notice - seats/24 LeaderSwitchWatch
    // only watches player 1's controller): the game's own switch skipped a person's character and found nobody else ->
    // the notice comes from here, for the keyboard and the controller alike
    if (g_SwitchSkippedPeople > 0 && LeaderType(PartyMgr()) == leadBefore)
    {
        static ULONGLONG last = 0; ULONGLONG now = GetTickCount64();
        if (now - last > 1500)
        {
            last = now;
            HudNotice(L"No AI teammate to switch to");
            static int logs = 0; if (logs < 40) { ++logs; CoopLog("SWITCH: the game's switch found no AI teammate (%d played by people) - \"No AI teammate to switch to\" shown", g_SwitchSkippedPeople); }
        }
    }
    g_SwitchSkippedPeople = skippedBefore;
    return r;
}
// 0x9CC4C0(controller, character): true = this character may be switched to. While the game's switch routine runs in a
// battle: a character a person plays (player 2's / 3's) is not offered - unless player 1's character is down and no AI
// teammate is left, then the routine may still give the lead to player 2/3 (the v7.1 KO hand-off in Hook_SetLeader).
// While player 2/3 leads (KO hand-off) nothing is changed here (Hook_SetLeader decides).
typedef uint8_t (*CanSwitch_t)(void* pc, void* ch);
static CanSwitch_t Orig_CanSwitch = nullptr;
// TEST SWITCH ([General] KoKeepLeader, default 0) - the author (f1.120 run, KO restore "jumps back through all of the
// menus by clicking through"): "Can the leader auto switch thing be disabled when there are no AI characters available? I
// assume that is ultimately the cause." With it on: player 1's character down + no AI teammate left -> the game's KO
// switch (unit KO-state handler 0x9864C0 -> switch routine 0x9BAA80, 0x98660E) finds nobody (players 2/3's characters fail
// the "can be switched to" check here), so player 1's knocked-out character stays the leader and players 2/3 keep their
// own command systems and menus (no KOMENU close / KORESTORE). RE (exe 2026-10-24): while the knocked-out unit IS the
// leader, that handler only tries the switch and skips the rest of its KO handling (from 0x98661F: KO sub-state
// unit+0x2DC ...) - so it may try every frame and the character may not finish "going down" properly. UNKNOWN: revive
// (Phoenix Down), the battle's end, the game over check. KOLEAD log lines watch it (KoKeepTick).
static ULONGLONG g_KoKeepSince = 0; static int g_KoKeepTries = 0, g_KoKeepTriesTotal = 0;
static void* g_KoKeepChar = nullptr;
static uint8_t Hook_CanSwitchTo(void* pc, void* ch)
{
    uint8_t r = Orig_CanSwitch(pc, ch);
    if (g_QuietSwitching) return (r && ch == g_QuietWant) ? 1 : 0;   // f1.248: the mod's own hand-back goes to that one character
    if (!r || !g_InSwitchTo || !ch || !GetPartyMember) return r;
    if (!InBattleNow()) return r;
    WithP2 base;
    void* p2 = g_Player->joined ? P2Char() : nullptr;
    void* p3 = g_P3Human ? g_P3HumanChar : nullptr;
    if (!((p2 && ch == p2) || (p3 && ch == p3))) return r;
    void* leader = LeaderChar();
    bool leaderUp = leader && CharHP(leader) != 0;
    bool aiLeft = false;
    unsigned char* pm = PartyMgr();
    ForEachPartyMember(pm, [&](PartyMember& m) {   // rule: an AI member standing for sure (an unreadable HP doesn't count)
        void* x = m.Char();
        if (x && x != p2 && x != p3 && x != leader && SurelyStanding(x)) aiLeft = true;
    });
    if (!leaderUp && !aiLeft)
    {
        // test: nobody takes over - player 1's character stays the leader
        ++g_KoKeepTries; ++g_KoKeepTriesTotal;
        if (!g_KoKeepSince)
        {
            g_KoKeepSince = GetTickCount64(); g_KoKeepChar = leader;
            char nm[96] = "?"; NameOf(ch, nm, sizeof(nm));
            CoopLog("KOLEAD: player 1's character is down and no AI teammate is left - the game's switch to %s (player %d) is refused: player 1 stays the leader, players 2/3 keep their own menus", nm, ch == p2 ? 2 : 3);
            {   // (f1.124 run 22:09:11: "no AI teammate is left" while Cloud had been revived 11 s before - was he down
                // again? the log couldn't tell): every party member with HP and who plays them
                char line[400]; size_t o = 0; line[0] = 0; bool full = false;
                ForEachPartyMember(pm, [&](PartyMember& m) {
                    void* x = m.Char(); if (!x || full) return;
                    char xn[96] = "?"; NameOf(x, xn, sizeof(xn));
                    const char* who = x == leader ? "the leader (player 1's controls)" : x == p2 ? "player 2" : x == p3 ? "player 3" : "AI";
                    o += snprintf(line + o, sizeof(line) - o, "%s%s HP %d (%s)", o ? ", " : "", xn, CharHP(x), who);
                    if (o >= sizeof(line)) { o = sizeof(line) - 1; full = true; }
                });
                CoopLog("KOLEAD: party - %s", line);
            }
        }
        return 0;
    }
    if (leaderUp) ++g_SwitchSkippedPeople;   // (Hook_SwitchTo: the "No AI teammate" notice; not for the KO switch)
    char nm[96] = "?"; NameOf(ch, nm, sizeof(nm));
    static ULONGLONG last = 0; if (GetTickCount64() - last > 2000) { last = GetTickCount64(); CoopLog("SWITCH: the game's character switch skips %s (player %d plays it)%s", nm, ch == p2 ? 2 : 3, leaderUp ? "" : " - player 1 is down, an AI teammate takes over"); }
    return 0;
}

// KoKeepLeader test - watch the knocked-out leader (every frame from core/27; logs only)
static void KoKeepTick()
{
    if (!g_KoKeepSince) return;
    ULONGLONG now = GetTickCount64();
    void* lead = LeaderChar();
    int hp = lead ? CharHP(lead) : -1;
    unsigned char* unit = lead && Readable((unsigned char*)lead + OFF_BATTLE_UNIT, 8) ? *(unsigned char**)((unsigned char*)lead + OFF_BATTLE_UNIT) : nullptr;   // battle unit = chara+0xCB0
    int st = unit && Readable(unit + 0x611, 1) ? unit[0x611] : -1, sub = unit && Readable(unit + 0x2DC, 4) ? *(int*)(unit + 0x2DC) : -1;
    static int lastSt = -9, lastSub = -9; static ULONGLONG lastSum = 0;
    if (st != lastSt || sub != lastSub) { lastSt = st; lastSub = sub; CoopLog("KOLEAD: player 1's character - unit state 0x%X, KO sub-state %d, HP %d, battle %d", st, sub, hp, (int)InBattleNow()); }
    if (now - lastSum > 5000) { lastSum = now; CoopLog("KOLEAD: still down after %.0f s - %d switch attempt(s) blocked in the last 5 s", (now - g_KoKeepSince) / 1000.0, g_KoKeepTries); g_KoKeepTries = 0; }
    if (lead != g_KoKeepChar || hp > 0 || !InBattleNow())
    {
        CoopLog("KOLEAD: ended after %.1f s (%s; HP %d) - %d switch attempt(s) blocked in all", (now - g_KoKeepSince) / 1000.0,
            lead != g_KoKeepChar ? "the leader changed" : hp > 0 ? "player 1's character is back up" : "the battle ended", hp, g_KoKeepTriesTotal);
        g_KoKeepSince = 0; g_KoKeepTries = g_KoKeepTriesTotal = 0; g_KoKeepChar = nullptr; lastSt = lastSub = -9;
    }
}

// AI guard requests. EndBattleAIController.RequestGuard (native exec 0x17B6190, from the
// native table) stores the request at ctrl+0x664 (time) / +0x1FA8 (flag); the controller's tick
// (0x91CA9B) then puts the character into guard. This is how party AI copies the leader's guard.
// For a character a person is playing, the request is dropped right after it's stored.
typedef void (*ExecFn_t)(void* ctx, void* stack, void* result);
static ExecFn_t Orig_RequestGuardExec = nullptr;
static void Hook_RequestGuardExec(void* ctx, void* stack, void* result)
{
    unsigned char* c = (unsigned char*)ctx;
    float t0 = 0.f; unsigned char f0 = 0; bool rd = c && Readable(c + 0x664, 4) && Readable(c + 0x1FA8, 1);
    if (rd) { t0 = *(float*)(c + 0x664); f0 = c[0x1FA8]; }
    Orig_RequestGuardExec(ctx, stack, result);   // always: it reads the parameters off the script stack
    // log who asks, and from which pass. The AI party members copy the LEADER's guard; a request made
    // during player 2/3's extra passes (where the game thinks their character leads) is undone, so it can't
    // cancel or replace the copy of player 1's guard.
    if (rd)
    {
        const char* pass = g_InActionPass2 ? "player 2/3 action pass" : g_InPass2 ? "player 2/3 movement pass" : g_InMainBcmPass ? "main command pass" : "outside the passes";
        static ULONGLONG lastLog = 0; ULONGLONG now = GetTickCount64();
        bool human = (g_Player->aiOffCtrl && c == g_Player->aiOffCtrl) || (g_OtherPlayer->aiOffCtrl && c == g_OtherPlayer->aiOffCtrl);
        if (now - lastLog > 500) { lastLog = now; CoopLog("GUARD: RequestGuard for ctrl %p (%s) - time %.2f -> %.2f, flag %d -> %d, during %s", c, human ? "human-played" : "AI", t0, *(float*)(c + 0x664), (int)f0, (int)c[0x1FA8], pass); }
        if ((g_InActionPass2 || g_InPass2) && !human)
        {
            *(float*)(c + 0x664) = t0; c[0x1FA8] = f0;
            static ULONGLONG lastU = 0; if (now - lastU > 2000) { lastU = now; CoopLog("GUARD: that request was undone (made during a player 2/3 pass)"); }
        }
    }
    if (c && ((g_Player->aiOffCtrl && c == g_Player->aiOffCtrl) || (g_OtherPlayer->aiOffCtrl && c == g_OtherPlayer->aiOffCtrl)))
    {
        *(float*)(c + 0x664) = 0.f; c[0x1FA8] = 0;
        static ULONGLONG last = 0;
        if (GetTickCount64() - last > 3000) { last = GetTickCount64(); CoopLog("GUARD: AI guard request dropped for a human-played character (ctrl %p)", c); }
    }
}
// two more key-state functions (action-mapping keys, map at +0x138) that the FIELD attack
// code calls directly (0x109B4BA, 0x109CF38, 0x109D33C). In the extra movement pass they answer
// from player 2's controller (Attack only, outside battle) - never player 1's keys.
typedef uint64_t (*KeyFn_t)(void* key);
static KeyFn_t Orig_KeyE00 = nullptr, Orig_KeyF30 = nullptr;
static uint64_t P2FieldKey(void* key, bool edge, uint64_t r)
{
    bool down, was;
    if (FieldAttackKey(key ? *(uint64_t*)key : 0, &down, &was))   // shared with passes/05 + menus/08b
    {
        bool v = edge ? (down && !was) : down;
        if (v) { static ULONGLONG last = 0; if (GetTickCount64() - last > 1500) { last = GetTickCount64(); CoopLog("FIELD: player %d attack (%s)", g_Player->num, edge ? "pressed" : "held"); } }
        return (r & ~0xFFull) | (v ? 1 : 0);
    }
    return r & ~0xFFull;
}
static uint64_t Hook_KeyE00(void* key) { uint64_t r = Orig_KeyE00(key); return g_InPass2 ? P2FieldKey(key, true, r) : r; }
static uint64_t Hook_KeyF30(void* key) { uint64_t r = Orig_KeyF30(key); return g_InPass2 ? P2FieldKey(key, false, r) : r; }
// 0x987270(unit, state, flag) changes a battle unit's state; state 4 = guard (the AI controller
// tick calls it with 4 to guard, and checks unit+0x611 == 4 for "already guarding"). Log every place
// that puts a party member into guard, once per (caller, character), plus a running summary, so we can
// see which code makes Barret guard when player 1 guards (and Cloud when player 2 guards).
typedef uint64_t (*UnitState_t)(void* unit, uint8_t state, uint8_t flag, void* a4);
static UnitState_t Orig_UnitState = nullptr;
static uint64_t Hook_UnitState(void* unit, uint8_t state, uint8_t flag, void* a4)
{
    if (state == 4 && unit && g_Player->joined && Readable((unsigned char*)unit + 0x58, 1))
    {
        int type = ((unsigned char*)unit)[0x58];
        if (type <= 8)   // party members only
        {
            uintptr_t site = (uintptr_t)__builtin_return_address(0) - g_Base - 5;
            static uintptr_t seen[128]; static int nSeen = 0;
            uintptr_t tag = site * 16 + (uintptr_t)type;
            bool known = false; for (int i = 0; i < nSeen; ++i) if (seen[i] == tag) known = true;
            unsigned char* pm = PartyMgr();
            int lead = LeaderType(pm);
            bool p1Guard = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool p2Guard = (g_Player->padNow & g_Player->cfg->bGuard) != 0;
            static ULONGLONG lastAny = 0;
            if ((!known && nSeen < 128) || GetTickCount64() - lastAny > 400)
            {
                lastAny = GetTickCount64();
                if (!known && nSeen < 128) seen[nSeen++] = tag;
                CoopLog("GUARDSET: type %d (%s) -> guard from RVA 0x%llX, flag %d [P1 Ctrl %d, P2 guard btn %d, in pass: move %d action %d main %d]",
                    type, type == lead ? "leader" : (type == g_Player->charType ? "player 2's" : "other"), (unsigned long long)site, flag,
                    (int)p1Guard, (int)p2Guard, (int)g_InPass2, (int)g_InActionPass2, (int)g_InMainBcmPass);
            }
        }
    }
    return Orig_UnitState(unit, state, flag, a4);
}
static void* MakeInlineHook(uintptr_t target, void* hook, size_t stealLen, const unsigned char* expect, const char* name, void** outOrig);
