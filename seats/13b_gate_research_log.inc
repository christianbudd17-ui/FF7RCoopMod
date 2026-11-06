// =================================================================================
// f1.246 GATELOG - LOG ONLY (research). The author (Failed Experiment fight, f1.244 log 20:25:07): the fight's script asks
// for Tifa as the leader (SetPartyLeader from RVA 0xDD6787, after the EB0010_00_CatWalk shot) so she can jump a gate; with
// player 2 on Tifa the switch block refuses it and Tifa (player 2) can't jump. The author: "what we need is an imperceptible
// change ... It just needs to recognize Tifa as the leader to make that jump ... This will happen later if there is a P2
// controlling Tifa". Unknown: HOW the gate decides who may jump (a "who leads?" question, a leader-only prompt, a trigger
// area). So, when the game's own script asks for a person's character as the leader (seats/13, battle or field since f1.247 -
// not the D-pad / KO switch routine, not the battle-end field-leader call), a 90 s watch starts (each new ask extends it):
//  - every distinct script event (UFunction through ProcessEvent) + the class of the object it ran on, once each - after
//    a 2 s quiet start, so the events that run all the time anyway are known first and not logged;
//  - every distinct game code spot that asks "who leads?" (GetLeader 0x15F1CE0, the leader-slot read 0x1420650) and what
//    it got, once each.
// Comparing a run where the jump fails (player 2 on Tifa, no hot swap) with one where it works (hot swap) shows the check.
// Game thread only. Max 900 event lines + 150 leader-ask lines per watch. On the end-of-project removal checklist.
// =================================================================================
static constexpr ULONGLONG GATE_WATCH_MS = 90000, GATE_QUIET_MS = 2000;
static constexpr int GATE_PE_SLOTS = 16384;   // open-addressing set of (function, class) pairs
static struct { void* fn; void* cls; } g_GatePE[GATE_PE_SLOTS];
static int g_GatePEUsed = 0, g_GatePELines = 0, g_GatePEDropped = 0;
static struct { int kind; uintptr_t site; int ans; } g_GateAsk[256];
static int g_GateAskN = 0, g_GateAskLines = 0;
static ULONGLONG g_GateStart = 0;
// f1.249 (the author: "Is there some way to see what that point IS?"): where the asked-for character is (world position,
// 4x a second) while a watch / a quiet lead hand-off runs, where she is when a cutscene begins, and every trigger / overlap
// event (name has Overlap / Trigger / BndEvt / Touch / Enter) with the position of the object it ran on
static int g_GateWhoIdx = -1, g_GateWhoSer = 0, g_GatePosLines = 0, g_GateTrigLines = 0;
static char g_GateWhoName[48] = "?";
static bool CharLoc(void* actor, float out[3]);   // pip_hud/30
static bool ActorLoc(void* actor, float out[3]);  // enemies/18
static bool GateWhoPos(char* out, size_t n)
{
    void* c = (g_GateWhoIdx >= 0) ? ResolveWeak(g_GateWhoIdx, g_GateWhoSer) : nullptr;
    float p[3];
    if (!c || !CharLoc(c, p)) { snprintf(out, n, "%s: no position", g_GateWhoName); return false; }
    snprintf(out, n, "%s at (%.1f, %.1f, %.1f) m%s", g_GateWhoName, p[0] / 100.f, p[1] / 100.f, p[2] / 100.f, c == LeaderChar() ? " - the leader" : " - not the leader");
    return true;
}
static void GateWatchEnd(ULONGLONG now)
{
    CoopLog("GATELOG: watch over (%.0f s) - %d script event / class pair(s) seen, %d logged%s; %d \"who leads?\" spot(s) logged",
        (now - g_GateStart) / 1000.0, g_GatePEUsed, g_GatePELines, g_GatePEDropped ? " (more not logged - line cap)" : "", g_GateAskLines);
    g_GateUntil = 0;
}
static void GateWatchStart(void* who, uintptr_t site, bool inBattle)
{
    ULONGLONG now = GetTickCount64();
    char nm[96] = "?"; NameOf(who, nm, sizeof(nm));
    if (g_GateUntil && now <= g_GateUntil)
    {
        g_GateUntil = now + GATE_WATCH_MS;
        CoopLog("GATELOG: +%.1f s the game asks again for %s as the leader (RVA 0x%llX, the mod sees %s) - watch extended", (now - g_GateStart) / 1000.0, nm,
            (unsigned long long)site, inBattle ? "a battle" : "no battle");
        return;
    }
    memset(g_GatePE, 0, sizeof(g_GatePE)); g_GatePEUsed = g_GatePELines = g_GatePEDropped = 0;
    g_GatePosLines = g_GateTrigLines = 0;
    if (!MakeWeak(who, &g_GateWhoIdx, &g_GateWhoSer)) g_GateWhoIdx = -1;
    {   // short name: "PC0002_00_Tifa_Standard_C_0" -> "Tifa"
        const char* a = strchr(nm, '_'); if (a) a = strchr(a + 1, '_');
        const char* st = a ? a + 1 : nm; const char* e = strchr(st, '_'); size_t L = e ? (size_t)(e - st) : strlen(st); if (L > 47) L = 47;
        memcpy(g_GateWhoName, st, L); g_GateWhoName[L] = 0;
    }
    g_GateAskN = g_GateAskLines = 0;
    g_GateStart = now; g_GateUntil = now + GATE_WATCH_MS;
    CoopLog("GATELOG: watch starts - the game's script asks for %s (a person's character) as the leader (RVA 0x%llX) - the mod sees %s "
        "(battle menu state %d, last battle-system update %llu ms ago); script events new after %.0f s and \"who leads?\" spots are logged for %.0f s",
        nm, (unsigned long long)site, inBattle ? "a battle (the switch block runs)" : "NO battle (the switch goes through)", g_LastBattleUi,
        (unsigned long long)(now - g_LastBcmTick), GATE_QUIET_MS / 1000.0, GATE_WATCH_MS / 1000.0);
}
static void GateLogPE(void* obj, void* fn)
{
    if (!fn || GetCurrentThreadId() != g_GameThreadId) return;
    ULONGLONG now = GetTickCount64();
    if (now > g_GateUntil) { GateWatchEnd(now); return; }
    {   // f1.249: trigger / overlap events - every call (cap 300), the function's kind cached per function
        static struct { void* fn; char trig; } fc[4096];
        int k = (int)((((uintptr_t)fn >> 4) * 2654435761u) & 4095), q = 0;
        while (q < 4096 && fc[k].fn && fc[k].fn != fn) { k = (k + 1) & 4095; ++q; }
        char fnm[96] = "";
        if (q < 4096 && !fc[k].fn)
        {
            NameOf(fn, fnm, sizeof(fnm));
            fc[k].fn = fn; fc[k].trig = (strstr(fnm, "Overlap") || strstr(fnm, "Trigger") || strstr(fnm, "BndEvt") || strstr(fnm, "Touch") || strstr(fnm, "Enter")) ? 1 : 0;
        }
        if (q < 4096 && fc[k].trig && g_GateTrigLines < 300)
        {
            ++g_GateTrigLines;
            if (!fnm[0]) NameOf(fn, fnm, sizeof(fnm));
            char onm[96] = "-"; if (obj) NameOf(obj, onm, sizeof(onm));
            float p[3]; char op[64] = "no position";
            if (obj && ActorLoc(obj, p)) snprintf(op, sizeof(op), "(%.1f, %.1f, %.1f) m", p[0] / 100.f, p[1] / 100.f, p[2] / 100.f);
            char wp[120]; GateWhoPos(wp, sizeof(wp));
            CoopLog("GATETRIG: +%.1f s %s on %s at %s | %s", (now - g_GateStart) / 1000.0, fnm, onm, op, wp);
        }
    }
    void* cls = (obj && Readable((unsigned char*)obj + 0x10, 8)) ? *(void**)((unsigned char*)obj + 0x10) : nullptr;
    uintptr_t h = (((uintptr_t)fn >> 4) * 2654435761u) ^ ((uintptr_t)cls >> 4);
    int i = (int)(h & (GATE_PE_SLOTS - 1));
    for (int probe = 0; probe < GATE_PE_SLOTS; ++probe, i = (i + 1) & (GATE_PE_SLOTS - 1))
    {
        if (g_GatePE[i].fn == fn && g_GatePE[i].cls == cls) return;   // seen in this watch
        if (!g_GatePE[i].fn) break;
    }
    if (g_GatePE[i].fn || g_GatePEUsed >= GATE_PE_SLOTS * 3 / 4) return;   // full
    g_GatePE[i].fn = fn; g_GatePE[i].cls = cls; ++g_GatePEUsed;
    if (now - g_GateStart < GATE_QUIET_MS) return;   // the quiet start: known, not logged
    if (g_GatePELines >= 900) { ++g_GatePEDropped; return; }
    ++g_GatePELines;
    char fnm[96] = "?", onm[96] = "-", cnm[96] = "-";
    NameOf(fn, fnm, sizeof(fnm));
    if (obj) NameOf(obj, onm, sizeof(onm));
    if (cls) ClassNameOf(cls, cnm, sizeof(cnm));
    CoopLog("GATELOG: +%.1f s event %s on %s (%s)", (now - g_GateStart) / 1000.0, fnm, onm, cnm);
}
static void GateLogLeaderAsk(int kind, uintptr_t site, int ans)
{
    if (GetCurrentThreadId() != g_GameThreadId) return;
    ULONGLONG now = GetTickCount64();
    if (now > g_GateUntil) return;   // (GateLogPE ends the watch)
    for (int i = 0; i < g_GateAskN; ++i) if (g_GateAsk[i].kind == kind && g_GateAsk[i].site == site && g_GateAsk[i].ans == ans) return;
    if (g_GateAskN >= 256 || g_GateAskLines >= 150) return;
    g_GateAsk[g_GateAskN].kind = kind; g_GateAsk[g_GateAskN].site = site; g_GateAsk[g_GateAskN].ans = ans; ++g_GateAskN;
    ++g_GateAskLines;
    if (kind == 0)
    {
        void* ld = LeaderChar(); char ln[96] = "?"; if (ld) NameOf(ld, ln, sizeof(ln));
        CoopLog("GATELOG: +%.1f s code at RVA 0x%llX asks who leads (GetLeader) - the leader is %s%s", (now - g_GateStart) / 1000.0,
            (unsigned long long)site, ln, g_InActionPass2 ? " (inside a player 2 / 3 command pass - the mod answers their character)" : "");
    }
    else CoopLog("GATELOG: +%.1f s code at RVA 0x%llX reads the leader's party slot -> %d", (now - g_GateStart) / 1000.0, (unsigned long long)site, ans);
}
// f1.249: every frame (core/27) - the asked-for character's world position 4x a second while a watch or a quiet lead
// hand-off runs (max 800 lines a watch)
static bool QuietLeadActive();   // seats/24b
static void GatePosTick()
{
    ULONGLONG now = GetTickCount64();
    if (!((g_GateUntil && now <= g_GateUntil) || QuietLeadActive())) return;
    static ULONGLONG at = 0; if (now - at < 250) return; at = now;
    if (g_GatePosLines >= 800) return;
    ++g_GatePosLines;
    char wp[120]; GateWhoPos(wp, sizeof(wp));
    CoopLog("GATEPOS: +%.1f s %s%s", (now - g_GateStart) / 1000.0, wp, g_ScriptedShot ? " (the game's shot)" : g_InCutscene ? " (cutscene)" : "");
}
// f1.249: a cutscene begins (core/06) during a watch - where the asked-for character is right then
static void GateCutsceneNote(void* obj)
{
    ULONGLONG now = GetTickCount64();
    if (!g_GateUntil || now > g_GateUntil) return;
    char on[96] = "?"; NameOf(obj, on, sizeof(on));
    char wp[120]; GateWhoPos(wp, sizeof(wp));
    CoopLog("GATEPOS: +%.1f s CUTSCENE begins on %s - %s", (now - g_GateStart) / 1000.0, on, wp);
}
