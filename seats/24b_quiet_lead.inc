// =================================================================================
// f1.248 QUIETLEAD - a battle script that needs a person's character as THE leader gets it, without anyone changing
// characters. The author (Failed Experiment fight, Barret = player 1, Tifa = player 2): "Tifa is supposed to jump over a
// gate - but as P2, she cannot do that"; "I don't want P1 and P2 to change characters. I just want P2 to be seen as the
// leader for that gate jump moment"; "what we need is an imperceptible change. So, no screen switching ... This will
// happen later if there is a P2 controlling Tifa"; "If the game gets loaded, the fight ends, whatever, then it needs to
// swap back."
// Logs (f1.244 - f1.247): after its EB0010_00_CatWalk shot the fight's script asks for Tifa as the leader (SetPartyLeader
// from RVA 0xDD6787 + 0x15F102A) and, when refused, plays the shot again every 20 - 28 s (f1.247 21:11:52 - 21:13:53, seven
// tries). When the switch went through (f1.247 21:15:51, Tifa played by the AI) the jump cutscene began 2.3 s later by
// itself (21:15:53). So the script needs Tifa to really lead for a moment.
// What happens now, in a battle (or its scripted shot - BattleWorldOn), when the game's script (not the D-pad / KO switch
// routine) asks for player 2's / 3's character as the leader:
//  1. that player's seat and the leader seat trade controllers (as the hot swap does, no notice) and the game's switch
//     goes through: player 2's controller now drives Tifa as the leader, player 1's controller drives Barret (the player 2
//     slot picks the free character) - nobody changes characters;
//  2. players 2 / 3's windows and command HUD stay hidden meanwhile (the game's shot / cutscene is full screen anyway);
//  3. hand back, once the cutscene that follows is over (+1 s), or 30 s without one: the game's own character-switch
//     routine (the D-pad's, 0x9BAA80) puts the old leader back (the mod's "can be switched to" answer allows only that
//     character), then the controllers trade back;
//  4. the battle ends / a save loads / the title (no battle world, no battle menu), the game asks for the old leader
//     itself, or that player leaves: the controllers trade back at once.
// Not done (old block) when the seat being handed the lead has no controller. f1.335 / f1.336: player 1 on the keyboard is fine -
// player 1's character stands still for the hand-off (its seat holds the keyboard but stays in, QuietSeatApply).
// Log lines "QUIETLEAD: ...". Risk MEDIUM (the first time the mod starts a leader switch itself).
// =================================================================================
static struct
{
    bool active = false; int seat = 0;               // the seat (1 = player 2, 2 = player 3) whose character leads for now
    void* mgr = nullptr; bool f1 = false, f2 = false; // the game's SetPartyLeader arguments (fallback hand-back)
    void* oldLeader = nullptr;                        // the leader before (player 1's character)
    void* quietLeader = nullptr;                      // the character the script asked for
    ULONGLONG since = 0, cutEndAt = 0; bool sawCut = false;
    ULONGLONG lastShot = 0;                           // f1.249: the last time the game played a battle camera shot / scripted shot
    int person0 = -1, personS = -1;                   // g_SeatPerson after the trade (a hot swap meanwhile = don't trade back)
    bool zone = false;                                // f1.250: started by the gate zone (not by the script's ask)
} g_QL;
// f1.250 (the author, f1.249 test: "it DID work from P2 - although ... looked like views trading places, camera cutting away";
// "Can we just set the trigger to be Tifa walks into the zone?" - "Yes"): in the Failed Experiment fight (EB0010) the
// script's ask for player 2's / 3's Tifa is HELD (nothing changes on screen) and the hand-off starts only when their Tifa
// comes within QZ_IN of the fight's trigger zone - f1.249 log 21:53:22: the fight script's talk trigger box
// (btsc_undg7_boss010_C, OnBeginTalkTriggerBoxCollide) fired with the leader Tifa at (105.2, 19.1) .. (102.3, 13.9) m, the
// jump cutscene began 0.1 s after its End at (101.7, 13.2), all at z -16.9 m. She becomes the leader ~3 m before the box,
// the game's own box fires for her, the cutscene covers the swap. Walks away (beyond QZ_OUT) before it -> hand back; a
// 60 s hand-off without a cutscene -> hand back and wait until she has been away (QZ_OUT) before the next one.
static constexpr float QZ_X = 10350.f, QZ_Y = 1600.f, QZ_Z = -1690.f;   // the zone's middle (cm)
static constexpr float QZ_IN = 300.f, QZ_OUT = 1000.f, QZ_DZ = 300.f;
static constexpr float QZ_REARM = 500.f;            // f1.256: after a hand-off without a jump, again once they have been 5 m out
static constexpr ULONGLONG QZ_JUMP_WAIT_MS = 5000;  // f1.256: no jump cutscene within 5 s of the hand-off -> hand back  // hand-off within / hand back beyond (horizontal), height range
// (f1.254: was 10 / 14 m - f1.253 test 23:00:53: from 9.9 m she needed 3 s to the jump, the author: "the weird leader switch
// that adjusted the camera for just a moment"; f1.252: standing in the zone when she became the leader, "as soon as I moved
// her it triggered" - so she may already be inside the box when the hand-off comes)
// (f1.252: was 7 / 11 m - f1.250 log 22:07:36: the box's first contact came at (108.8, 21.2) = 7.4 m from the middle; she
// must lead BEFORE she reaches it)
static struct
{
    bool pending = false, armed = true; int seat = 0;
    void* want = nullptr; void* mgr = nullptr; bool f1 = false, f2 = false;
    ULONGLONG since = 0, logAt = 0;
} g_QZ;
static bool QuietSwitchTo(void* want, void* mgr, bool f1, bool f2);   // below
static ULONGLONG g_QLReleaseAt = 0;   // f1.255: when the held story shot lets go (0.5 s after the hand-back)
// f1.282 (the author, 2026-11-05 00:14: "If she is not in position to run to the gate (behind it, or to the side) then it
// fails and she runs into the side of the stairs but doesn't reach the area where it will trigger"). Log 00:14:04 / 00:14:47:
// the hand-off started with Tifa north of the stairs (102.1, 18.6) / (101.1, 17.7) - straight at the jump spot she hit the
// stairs' side and stopped (102.8, 15.7), no jump. Every run that jumped started ON the stairs' line, up and to the right of
// the jump spot: (105.2, 19.1) 60 deg, (104.5, 18.7) 63 deg, (105.8, 17.8) 49 deg, 6 - 7 m away (the game's trigger box hits
// along it at 50 - 59 deg). Now the hand-off starts only there - bearing QS_A0 - QS_A1 from the jump spot, QS_D0 - QS_D1 away
// (as a leader walking the stairs in the normal game); elsewhere near the gate nothing happens until she comes that way.
static constexpr float QS_BX = 10160.f, QS_BY = 1295.f;              // the jump spot (cm) - the jump cutscene began here in every run
static constexpr float QS_A0 = 42.f, QS_A1 = 65.f;                   // the stairs' line, degrees from the jump spot (world x = 0)
static constexpr float QS_D0 = 80.f, QS_D1 = 350.f;                  // how far up the stairs (cm)
// f1.283 (the author after the f1.282 run: "there is just no way to avoid that camera switch? It's always jarring"): was
// 1.5 - 7.5 m - the hand-off now comes 0.8 - 3.5 m before the jump spot, so the full-screen moment before the jump cutscene is
// short (f1.282 run 00:41:29: 3.9 s from 5.1 m with no steering; the f1.259 - f1.281 runs: 1.7 - 2 s from 6 - 7 m)
static constexpr float QS_RA0 = 36.f, QS_RA1 = 71.f, QS_RD1 = 900.f; // re-arm: out of this wider part after a hand-off without a jump
// true = on the stairs' line; *dist (cm) / *brg (deg) for the log; wide = the wider re-arm part
static bool QuietOnStairs(void* c, float* dist, float* brg, bool wide)
{
    float p[3];
    *dist = -1.f; *brg = 0.f;
    if (!c || !CharLoc(c, p) || fabsf(p[2] - QZ_Z) > QZ_DZ) return false;
    float dx = p[0] - QS_BX, dy = p[1] - QS_BY;
    *dist = sqrtf(dx * dx + dy * dy); *brg = atan2f(dy, dx) * RAD_TO_DEG;
    if (wide) return *brg >= QS_RA0 && *brg <= QS_RA1 && *dist >= 100.f && *dist <= QS_RD1;
    return *brg >= QS_A0 && *brg <= QS_A1 && *dist >= QS_D0 && *dist <= QS_D1;
}
static float QuietZoneDist(void* c)
{
    float p[3];
    if (!c || !CharLoc(c, p)) return -1.f;
    if (fabsf(p[2] - QZ_Z) > QZ_DZ) return 1e9f;
    float dx = p[0] - QZ_X, dy = p[1] - QZ_Y;
    return sqrtf(dx * dx + dy * dy);
}
// f1.336 (K1 run 22:20:31 with f1.335, the author: "gate jump still failed. Switched to P1 again too"): the trade gave seat 2
// the keyboard -> SeatApply took that player OUT ("the keyboard can't play a teammate") -> the hand-off ended at once ("that
// player left"), player 3 took Tifa, player 2 came back IN on the trade back = the mess. For the hand-off's few seconds the
// seat that gets the keyboard stays IN (not connected, no input - its character stands still; no auto-leave: connLostAt
// stays 0), and the trade back gives it its controller again (SeatApply(true) - the connection check picks it up).
static void QuietSeatApply(bool hasController)   // on the loaded player (2 or 3)
{
    if (hasController) { SeatApply(true); return; }
    g_Player->cmdButtons = g_Player->cmdButtonsPrev = g_Player->padNow = 0;
    g_Player->joinHeldSince = 0; g_Player->joinFired = true;
    g_Player->connected = false; g_Player->connLostAt = 0;
}
static void QuietSeatTrade(int s, const char* why)
{
    bool leadHas = In2SeatHasController(0), sHas = In2SeatHasController(s);
    GameRumbleStop("quiet lead hand-off");
    In2SeatsTraded(0, s);
    g_Cfg.p1Controller = sHas;
    if (s == 1) { WithP2 w; QuietSeatApply(leadHas); } else { WithP3 w; QuietSeatApply(leadHas); }
    { int t = g_SeatPerson[0]; g_SeatPerson[0] = g_SeatPerson[s]; g_SeatPerson[s] = t; }
    char a[180], b[180]; In2SeatText(0, a, sizeof(a)); In2SeatText(s, b, sizeof(b));
    CoopLog("QUIETLEAD: seats 1 and %d traded controllers (%s) - seat 1: %s; seat %d: %s%s", s + 1, why, a, s + 1, b,
        leadHas ? "" : " (the keyboard - that seat stays in, its character stands still until the trade back)");
}
static void QuietLeadEnd(const char* why, bool tradeBack)
{
    if (!g_QL.active) return;
    g_QL.active = false;
    AutoStepStop("the hand-off ended");   // f1.259
    if (g_QL.zone) g_QLReleaseAt = GetTickCount64() + 500;   // f1.255: the split screen comes back 0.5 s after the hand-back
    int s = g_QL.seat;
    if (tradeBack && g_SeatPerson[0] == g_QL.person0 && g_SeatPerson[s] == g_QL.personS) QuietSeatTrade(s, why);
    else if (tradeBack) CoopLog("QUIETLEAD: the seats changed meanwhile (a hot swap?) - not traded back (%s)", why);
    g_QuietLeadHide = false;
    CoopLog("QUIETLEAD: over after %.1f s (%s)", (GetTickCount64() - g_QL.since) / 1000.0, why);
}
static bool QuietLeadAsk(void* mgr, void* newLeader, bool f1, bool f2, uintptr_t site)
{
    char nm[96] = "?"; NameOf(newLeader, nm, sizeof(nm));
    if (g_QL.active)
    {
        if (newLeader != g_QL.oldLeader) return false;
        // the game asks for the old leader itself (e.g. the fight's end) - let it and trade back
        CoopLog("QUIETLEAD: the game's script asks for %s (the leader before the hand-off) - allowed, the controllers trade back (RVA 0x%llX)", nm, (unsigned long long)site);
        Orig_SetLeader(mgr, newLeader, f1, f2);
        QuietLeadEnd("the game put the old leader back", true);
        return true;
    }
    int s = 0;
    { WithP2 w; if (g_Player->joined && P2Char() == newLeader) s = 1; }
    if (!s && g_P3Human && g_P3HumanChar == newLeader) s = 2;
    if (!s) return false;
    // f1.335 (K1 run 22:07, the author: "it didn't activate with Tifa ... I could only perform the gate jump with P1"): player 1
    // on the keyboard (Seat1=Keyboard) refused the hand-off - seat 1 needed a controller. Now only the seat being handed the lead
    // needs one: player 1's character stands still for the hand-off's few seconds (the game reads the keyboard for the leader
    // only; f1.336 QuietSeatApply keeps that seat in) and the trade back gives player 1 the keyboard again.
    if (!In2Running() || !In2SeatHasController(s))
    {
        static int logs = 0; if (logs < 10) { ++logs; CoopLog("QUIETLEAD: the game's script asks for %s (player %d's) as the leader, but seat %d has no controller - no hand-off (the switch block decides)", nm, s + 1, s + 1); }
        return false;
    }
    if ((g_QZ.pending && g_QZ.want == newLeader)   // f1.258: already held - keep holding (f1.257 bug: the skipped reminder
                                                   // left no fresh shot owner, the ask went to the at-once hand-off - screens flipped)
        || BossFightIs("EB0010_") || (!strncmp(g_LastShotOwner, "EB0010_", 7) && GetTickCount64() - g_LastShotAt < 10000))
    {   // f1.250: held until their Tifa is near the gate zone (QuietLeadTick). f1.251: also known by the catwalk shot's owner -
        // f1.250 test 22:07:26: the boss isn't in the battle's lists until after the jump (BOSSTARGET 22:08:08), so the hold
        // didn't run and the hand-off started at the ask (10 s of traded views)
        if (!g_QZ.pending || g_QZ.want != newLeader)
        {
            g_QZ = {}; g_QZ.pending = true; g_QZ.armed = true; g_QZ.seat = s; g_QZ.want = newLeader; g_QZ.mgr = mgr; g_QZ.f1 = f1; g_QZ.f2 = f2; g_QZ.since = GetTickCount64();
            CoopLog("QUIETLEAD: the Failed Experiment's script asks for %s (player %d's) as the leader (RVA 0x%llX) - held: nothing changes until their %s "
                "comes within %.0f m of the gate zone (%.1f, %.1f), then the hand-off", nm, s + 1, (unsigned long long)site, nm, QZ_IN / 100.f, QZ_X / 100.f, QZ_Y / 100.f);
        }
        return true;   // (the ask is dropped - the game asks again with its next catwalk shot)
    }
    void* old = LeaderChar();
    char on[96] = "?"; if (old) NameOf(old, on, sizeof(on));
    g_QL = {};
    g_QL.active = true; g_QL.seat = s; g_QL.mgr = mgr; g_QL.f1 = f1; g_QL.f2 = f2;
    g_QL.oldLeader = old; g_QL.quietLeader = newLeader; g_QL.since = g_QL.lastShot = GetTickCount64();
    // (f1.249: windows NOT hidden any more - f1.248 test 21:35:05: with player 2's window hidden the co-op camera took the
    // main view over the game's catwalk shot; the author: "Coop camera kicked on, split screen went away")
    QuietSeatTrade(s, "the hand-off starts");
    g_QL.person0 = g_SeatPerson[0]; g_QL.personS = g_SeatPerson[s];
    CoopLog("QUIETLEAD: the game's script asks for %s (player %d's) as the leader (RVA 0x%llX, the mod sees %s) - %s leads for now with player %d's "
        "controller, player 1's controller plays %s; back after the cutscene that follows (or 60 s without a shot / cutscene)", nm, s + 1, (unsigned long long)site,
        InBattleNow() ? "a battle" : "the battle's scripted shot", nm, s + 1, on);
    Orig_SetLeader(mgr, newLeader, f1, f2);
    return true;
}
static void QuietLeadHandBack(const char* why)
{
    void* want = g_QL.oldLeader;
    bool inParty = false;
    unsigned char* pm = PartyMgr();
    if (want && pm) ForEachPartyMember(pm, [&](PartyMember& m) { if (m.Char() == want) inParty = true; });
    if (!inParty) { QuietLeadEnd("the old leader isn't in the party now - controllers back only", true); return; }
    if (LeaderChar() != want) QuietSwitchTo(want, g_QL.mgr, g_QL.f1, g_QL.f2);   // (f1.250: one helper for both ways)
    QuietLeadEnd(why, true);
}
// f1.250: the game's own switch routine (as the D-pad), only `want` allowed; fallback SetPartyLeader. true = `want` leads.
static bool QuietSwitchTo(void* want, void* mgr, bool f1, bool f2)
{
    if (Orig_SwitchTo && Orig_CanSwitch)
    {
        void* pc = *(void**)(g_Base + RVA_SWITCH_PC);
        if (pc)
        {
            g_QuietWant = want; g_QuietSwitching = true; bool was = g_InSwitchTo; g_InSwitchTo = true;
            Orig_SwitchTo(pc, 1, 0, 1);   // as the D-pad does (0x141D98E: dl = r9b = direction, r8b = 0)
            g_InSwitchTo = was; g_QuietSwitching = false; g_QuietWant = nullptr;
            if (LeaderChar() == want) { CoopLog("QUIETLEAD: the leader changed through the game's own switch"); return true; }
        }
    }
    g_QuietSwitching = true; Orig_SetLeader(mgr, want, f1, f2); g_QuietSwitching = false;
    bool ok = LeaderChar() == want;
    CoopLog("QUIETLEAD: the game's switch routine didn't change the leader - set directly (%s)", ok ? "now right" : "STILL WRONG");
    return ok;
}
static void QuietZoneStart(float d)
{
    int s = g_QZ.seat;
    if (!In2Running() || !In2SeatHasController(s)) return;   // f1.335: player 1 may be on the keyboard (see QuietLeadAsk)
    void* old = LeaderChar();
    char nm[96] = "?", on[96] = "?"; NameOf(g_QZ.want, nm, sizeof(nm)); if (old) NameOf(old, on, sizeof(on));
    g_QL = {};
    g_QL.active = true; g_QL.zone = true; g_QL.seat = s; g_QL.mgr = g_QZ.mgr; g_QL.f1 = g_QZ.f1; g_QL.f2 = g_QZ.f2;
    g_QL.oldLeader = old; g_QL.quietLeader = g_QZ.want; g_QL.since = g_QL.lastShot = GetTickCount64();
    g_QZ.armed = false;
    StoryShotHold("the gate hand-off");   // f1.255: full screen, the game's camera, before anything changes
    g_QLReleaseAt = 0;
    float sd = -1.f, sb = 0.f; { void* c = nullptr; if (s == 1) { WithP2 w; c = P2Char(); } else c = g_P3HumanChar; QuietOnStairs(c, &sd, &sb, false); }
    CoopLog("QUIETLEAD: player %d's %s is %.1f m from the gate zone, on the stairs (%.1f m from the jump spot at %.0f deg) - the hand-off starts (%s leads with player %d's controller, player 1's controller plays %s)",
        s + 1, nm, d / 100.f, sd / 100.f, sb, nm, s + 1, on);
    if (!In2SeatHasController(0)) CoopLog("QUIETLEAD: player 1 is on the keyboard - %s stands still for the hand-off, the keyboard goes back to player 1 after it", on);   // f1.335 / f1.336
    QuietSeatTrade(s, "the hand-off starts - the gate zone");
    g_QL.person0 = g_SeatPerson[0]; g_QL.personS = g_SeatPerson[s];
    if (!QuietSwitchTo(g_QZ.want, g_QZ.mgr, g_QZ.f1, g_QZ.f2)) QuietLeadEnd("the leader didn't change - controllers back", true);
    else AutoStepStart(QS_BX, QS_BY);   // f1.259: the last step to the jump spot (101.6, 12.95 m) - the mod steers, 3 s at most (f1.282)
}
static void QuietZoneTick(ULONGLONG now)
{
    if (!g_QZ.pending) return;
    bool battle = InBattleNow() || BattleWorldOn();
    int s = g_QZ.seat;
    bool in = false; void* c = nullptr;
    if (s == 1) { WithP2 w; in = g_Player->joined; c = in ? P2Char() : nullptr; } else { WithP3 w; in = g_Player->joined; c = g_P3Human ? g_P3HumanChar : nullptr; }
    if (!battle || !in || (!g_QL.active && c != g_QZ.want))
    {
        CoopLog("QUIETLEAD: the held ask for the gate zone is dropped (%s)", !battle ? "the battle is over / a load" : !in ? "that player left" : "that player isn't on that character any more");
        g_QZ = {}; return;
    }
    if (g_QL.active || g_GamePaused || g_InCutscene) return;
    float d = QuietZoneDist(c);
    if (d < 0) return;
    float sd, sb, wd, wb;
    bool stairs = QuietOnStairs(c, &sd, &sb, false), wideIn = QuietOnStairs(c, &wd, &wb, true);
    if (d > QZ_REARM || !wideIn) g_QZ.armed = true;   // (f1.256: was QZ_OUT; f1.282: or off the stairs' wider part)
    if (now - g_QZ.logAt > 3000)
    {
        g_QZ.logAt = now;
        CoopLog("QUIETLEAD: held - player %d's character %.1f m from the gate zone, %.1f m from the jump spot at %.0f deg (the stairs: %.0f - %.0f deg, %.1f - %.1f m)%s",
            s + 1, d > 1e8f ? -1.f : d / 100.f, sd / 100.f, sb, QS_A0, QS_A1, QS_D0 / 100.f, QS_D1 / 100.f, g_QZ.armed ? "" : " (waits until they have been away)");
    }
    // f1.282: near the gate but not on the stairs - the old trigger (within QZ_IN) would have started here; logged once per visit
    static bool offLogged = false;
    if (d < QZ_IN && !stairs) { if (!offLogged) { offLogged = true; CoopLog("QUIETLEAD: player %d's character is at the gate but not on the stairs' line (%.1f m from the jump spot at %.0f deg) - no hand-off until they come down the stairs", s + 1, sd / 100.f, sb); } }
    else if (d > QZ_IN + 100.f) offLogged = false;
    if (g_QZ.armed && stairs) QuietZoneStart(d);
}
// every frame (core/27, after the hot swap key)
static void QuietLeadTick()
{
    if (InBattleNow()) g_QuietBattleSeenAt = GetTickCount64();
    ULONGLONG now = GetTickCount64();
    if (g_QLReleaseAt && now >= g_QLReleaseAt) { g_QLReleaseAt = 0; StoryShotRelease("0.5 s after the hand-back"); }   // f1.255
    QuietZoneTick(now);   // f1.250
    AutoStepTick();       // f1.259
    if (!g_QL.active) return;
    bool battle = InBattleNow() || BattleWorldOn();
    if (!battle) { QuietLeadEnd("the battle is over / a load / the title", true); g_QZ = {}; return; }
    int s = g_QL.seat;
    bool in = false; if (s == 1) { WithP2 w; in = g_Player->joined; } else { WithP3 w; in = g_Player->joined; }
    if (!in) { QuietLeadHandBack("that player left"); g_QZ = {}; return; }   // f1.284 (code review #57): the old leader back too, not only the controllers
    if (g_GamePaused) return;
    if (g_CutSince > g_QL.since) g_QL.sawCut = true;   // f1.251: this tick doesn't run during the jump cutscene (f1.250 log: no tick
                                                       // 22:07:38 - 22:08:02) - a cutscene that began after the hand-off counts
    if (g_InCutscene) { g_QL.sawCut = true; g_QL.cutEndAt = 0; return; }
    if (g_QL.sawCut)
    {
        if (!g_QL.cutEndAt) { g_QL.cutEndAt = now; return; }
        if (now - g_QL.cutEndAt >= (g_QL.zone ? 300u : 1000u)) { QuietLeadHandBack("the cutscene after it is over"); g_QZ = {}; }   // (f1.250: the held ask is done; f1.255: 0.3 s under the held full screen)
        return;
    }
    if (g_QL.zone)
    {   // f1.250: walked away from the gate zone before the jump - hand back (the held ask stays; again when they come back)
        // f1.273 (code review #58): the stored pointer is only compared - the position is read from the game's current
        // leader (a load mid hand-off could have freed the stored character; CharLoc calls into the game with it)
        void* cur = LeaderChar();
        float d = (cur && cur == g_QL.quietLeader) ? QuietZoneDist(cur) : -1.f;
        if (d > QZ_OUT) { QuietLeadHandBack("they walked away from the gate zone"); g_QZ.armed = true; return; }
        // f1.256 (the author: "If the other guardrails are in place I suppose that's good enough"): no jump within 5 s
        // (stopped / hanging around in the zone) - hand back, split screen back; again once they have been 5 m out
        if (now - g_QL.since > QZ_JUMP_WAIT_MS) { QuietLeadHandBack("no jump cutscene within 5 s of the hand-off"); g_QZ.armed = false; return; }
    }
    // f1.249 (f1.248 test: the 30 s from the first ask ran out 7 s into the game's next catwalk shot, with player 2's Tifa
    // ~0.5 m short of where the jump fired in the runs that worked): 60 s since the last battle camera shot / scripted shot
    if (g_ScriptedShot) g_QL.lastShot = now;
    if (now - g_QL.lastShot > 60000) { QuietLeadHandBack("no cutscene came - 60 s without a shot"); g_QZ.armed = false; }
}
// f1.249: the game plays a battle camera shot (camera/38b Hook_GcPlayBcs) - a hand-off going on waits on
static void QuietLeadShotNote()
{
    if (!g_QL.active && !g_QZ.pending) return;
    ULONGLONG now = GetTickCount64(); g_QL.lastShot = now;
    if (g_GateUntil && now <= g_GateUntil && g_GateUntil < now + 60000) g_GateUntil = now + 60000;   // the GATELOG watch goes on with it
}
static bool QuietLeadActive() { return g_QL.active || g_QZ.pending; }

// =================================================================================
// f1.253 KO2 - LOG ONLY. The author (f1.252 Failed Experiment, after the hand-back): "Tifa went down, was revived but had
// lost control, couldn't attack, etc." Log 22:45:58 (+565 heal = the revive): player 2's attack presses at 22:46:10 reached
// their command pass but Tifa hit nothing, and their menu "couldn't turn to player 2's page". No KO / revive lines for
// players 2 / 3 existed. Now: their character going down / coming back, with its unit state, KO sub-state, AI controller
// (decisions off?), their own command system and its mode - at the change and each second for 5 s after a revive.
// =================================================================================
static void Ko2WatchOne()
{
    static int lastHp[4] = {-1, -1, -1, -1}; static ULONGLONG revAt[4] = {}, lastSnap[4] = {}; static void* lastC[4] = {};
    int pi = g_Player->num & 3;
    unsigned char* c = (g_Player->joined && InBattleNow()) ? (unsigned char*)P2Char() : nullptr;
    if (!c) { lastHp[pi] = -1; lastC[pi] = nullptr; revAt[pi] = 0; return; }
    if (c != lastC[pi]) { lastC[pi] = c; lastHp[pi] = -1; revAt[pi] = 0; }
    int hp = CharHP(c);
    if (hp < 0) return;
    ULONGLONG now = GetTickCount64();
    const char* what = nullptr;
    if (lastHp[pi] > 0 && hp == 0) what = "went down";
    else if (lastHp[pi] == 0 && hp > 0) { what = "came back (revived)"; revAt[pi] = now; }
    else if (revAt[pi] && now - revAt[pi] <= 5000 && now - lastSnap[pi] >= 1000) what = "after the revive";
    lastHp[pi] = hp;
    if (!what) return;
    lastSnap[pi] = now;
    unsigned char* unit = Readable(c + OFF_BATTLE_UNIT, 8) ? *(unsigned char**)(c + OFF_BATTLE_UNIT) : nullptr;
    int st = unit && Readable(unit + 0x611, 1) ? unit[0x611] : -1, sub = unit && Readable(unit + 0x2DC, 4) ? *(int*)(unit + 0x2DC) : -1;
    unsigned char* ctrl = Readable(c + OFF_CHAR_AI_CTRL, 8) ? *(unsigned char**)(c + OFF_CHAR_AI_CTRL) : nullptr;
    int off = ctrl && Readable(ctrl + OFF_AI_FLAGS, 4) ? ((*(DWORD*)(ctrl + OFF_AI_FLAGS) & AIFLAG_DECISIONS_OFF) ? 1 : 0) : -1;
    char nm[96] = "?"; NameOf(c, nm, sizeof(nm));
    CoopLog("KO2: player %d's %s %s - HP %d, unit state 0x%X, KO sub-state %d | AI controller %p (decisions %s; the mod turned off %p) | own command system %s, mode B8 %d, ui %d | leader %s",
        g_Player->num, nm, what, hp, st, sub, ctrl, off == 1 ? "OFF" : off == 0 ? "ON" : "?", g_Player->aiOffCtrl,
        g_Player->ownBcmOn ? "on" : "off", g_Player->ownBcm ? *(int*)(g_Player->ownBcm + BCM_MODE) : -1, g_Player->ownBcm ? *(int*)(g_Player->ownBcm + BCM_UI_STATE) : -1,
        LeaderChar() == (void*)c ? "them" : "not them");
}
static void Ko2WatchTick() { { WithP2 w; Ko2WatchOne(); } if (g_P3Enabled) { WithP3 w; Ko2WatchOne(); } }
