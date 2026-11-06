// =================================================================================
// f1.212 TARGETSET - LOG ONLY (f1.213: Hook_UnitSetTarget also lets enemies/22d BossTargetPick change a boss's pick) (the author 2026-10-27: "We need to figure out why the bosses are only targeting Player 1.
// It is imperative."). The Airbuster and the ChiefGhost kept NO hate scores (TARGETLOG "hate empty") - their target is
// not picked by the game's hate update (0x93ED90 -> UnitSetTarget 0x96BFA0, claude/FF7R_Enemy_Targeting_RE.md). Every
// enemy target is written by 0x997FB0 SetUnitTarget(unit, TSharedPtr* target) (unit +0x38 = the target's +0xCB8 handle):
// 32 call sites, 17 more through UnitSetTarget (0x96BFA0, which ends in it). Both are hooked here, read only:
//  - every change of an enemy's target: who it is now, and WHICH CODE wrote it (the caller's RVA; through UnitSetTarget
//    also UnitSetTarget's caller) - 60 lines a minute;
//  - every 30 s in a battle: how many target writes each caller made and onto whom (P1 / P2 / P3 / AI / other).
// Nothing is changed. Party members' own units (player lock-on targets) are left out. Remove once the boss answer is in
// (removal checklist).
// =================================================================================
typedef void (*SetUnitTarget_t)(void* unit, void* sp);
static void BossPhaseScriptCall(void* unit, uintptr_t outer, void* asked, void* given);   // enemies/22f (f1.219, log only)
static void BossPhaseDirect(void* unit, void* h, uintptr_t caller);
static void BossCatCall(void* unit, uintptr_t outer, void* asked, void* given);   // enemies/22d (f1.225 command catalogue)
static void BossCatDirect(void* unit, uintptr_t caller);
// (UnitSetTarget_t: enemies/16)
static SetUnitTarget_t Orig_SetUnitTarget = nullptr;
static UnitSetTarget_t Orig_UnitSetTarget = nullptr;
static uintptr_t g_TsOuter = 0;   // UnitSetTarget's caller while it runs (game thread only)
struct TsTally { uintptr_t caller = 0, outer = 0; int n[5] = {0, 0, 0, 0, 0}; };
static TsTally g_TsTally[24];
static int TsPartySlotOfUnit(void* unit)
{
    unsigned char* mgr = PartyMgr(); int n = PartyCount();
    for (int i = 0; i < n && i < 3; ++i)
    {
        unsigned char* ch = (unsigned char*)CharByType(mgr, OrderType(mgr, i));
        if (ch && Readable(ch + OFF_BATTLE_UNIT, 8) && *(void**)(ch + OFF_BATTLE_UNIT) == unit) return i;
    }
    return -1;
}
// the party member whose handle (+0xCB8) is h, or null
static void* TsCharOfHandle(void* h)
{
    if (!h) return nullptr;
    unsigned char* mgr = PartyMgr(); int n = PartyCount();
    for (int i = 0; i < n && i < 3; ++i)
    {
        unsigned char* ch = (unsigned char*)CharByType(mgr, OrderType(mgr, i));
        if (ch && Readable(ch + 0xCB8, 8) && *(void**)(ch + 0xCB8) == h) return ch;
    }
    return nullptr;
}
static void TsEnemyName(void* unit, char* out, size_t n)
{   // from TARGETLOG's enemy records (enemies/22b g_Tl - weak references, no game calls inside the hook)
    snprintf(out, n, "enemy unit %p", unit);
    for (auto& t : g_Tl)
    {
        if (t.idx < 0) continue;
        void* e = ResolveWeak(t.idx, t.ser);
        if (e && Readable((unsigned char*)e + OFF_BATTLE_UNIT, 8) && *(void**)((unsigned char*)e + OFF_BATTLE_UNIT) == unit) { NameOf(e, out, n); return; }
    }
}
static void TsNote(void* unit, void* h, uintptr_t caller, uintptr_t outer)
{
    if (!g_Cfg.targetLog || GetCurrentThreadId() != g_GameThreadId || !unit) return;
    if (TsPartySlotOfUnit(unit) >= 0) return;   // a party member's own target (lock-on) - not an enemy
    void* ch = TsCharOfHandle(h);
    int who = !h ? 4 : !ch ? 4 : TlWho(ch, g_AttnHuman);   // 4 = nobody / not a party member
    {   // the 30 s tally per (caller, outer)
        TsTally* t = nullptr;
        for (auto& x : g_TsTally) if (x.caller == caller && x.outer == outer) { t = &x; break; }
        if (!t) for (auto& x : g_TsTally) if (!x.caller) { x.caller = caller; x.outer = outer; t = &x; break; }
        if (t) ++t->n[who];
    }
    struct Last { void* unit; void* h; uintptr_t caller; };
    static Last last[24]; static int nLast = 0;
    Last* l = nullptr;
    for (int i = 0; i < nLast; ++i) if (last[i].unit == unit) { l = &last[i]; break; }
    if (!l) { if (nLast < 24) { l = &last[nLast++]; l->unit = unit; l->h = (void*)1; l->caller = 0; } else return; }
    if (l->h == h && l->caller == caller) return;   // the same target from the same code - not news
    bool changed = l->h != h;
    l->h = h; l->caller = caller;
    static LogRate rate;
    if (!LogOk(rate, 60)) return;
    char en[96], tn[24] = "nobody";
    TsEnemyName(unit, en, sizeof(en));
    if (ch) TlShort(ch, tn, sizeof(tn)); else if (h) snprintf(tn, sizeof(tn), "non-party %p", h);
    if (outer) CoopLog("TARGETSET: %s -> %s (%s) %s by UnitSetTarget called from RVA 0x%llX", en, tn, kTlWho[who], changed ? "set" : "set again (other code)", (unsigned long long)outer);
    else CoopLog("TARGETSET: %s -> %s (%s) %s by code at RVA 0x%llX", en, tn, kTlWho[who], changed ? "set" : "set again (other code)", (unsigned long long)caller);
}
static void Hook_SetUnitTarget(void* unit, void* sp)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    void* h = (sp && Readable(sp, 16)) ? *(void**)sp : nullptr;
    uintptr_t caller = (g_Base && ret > g_Base) ? ret - g_Base : 0;
    bool viaUst = caller >= 0x96BFA0 && caller < 0x96C0F7;
    TsNote(unit, h, caller, viaUst ? g_TsOuter : 0);
    if (!viaUst && GetCurrentThreadId() == g_GameThreadId) { BossPhaseDirect(unit, h, caller); BossCatDirect(unit, caller); }   // f1.219 (log only); f1.225 catalogue
    Orig_SetUnitTarget(unit, sp);
}
static void* BossTargetPick(void* unit, void* ch, uintptr_t outer);   // enemies/22d (f1.213)
// ---- f1.225 SetTargetByType's mode (the author: "Yes, build it"). A boss script's AEndBattleAIController::SetTargetByType
// (exec thunk 0x17BC210 -> native 0x9223A0(this, mode) - its only caller - one UnitSetTarget call for all three modes,
// return 0x9226E4; f1.220 / f1.224 called it "target the nearest") takes EBattleAISetTargetType: UCPC = 0 (the character
// the player controls - the leader: [game instance +0x233]), FarPC = 1 (the farthest), NearPC = 2 (the nearest). The native
// has no hookable start (13 bytes, then a call), so the exec thunk is hooked: before it runs, its Blueprint bytecode
// (FFrame +0x20 Code) is looked at - an enum literal is EX_ByteConst (0x24) + the value. Anything else (a variable) =
// unknown (-1). Game thread only; -2 = not inside SetTargetByType.
typedef void (*BpExec_t)(void* ctx, void* stack, void* result);
static BpExec_t Orig_ByType = nullptr;
static int g_ByTypeMode = -2;
static unsigned char g_ByTypeOp = 0;   // the bytecode when the mode is unknown
static void Hook_ByType(void* ctx, void* stack, void* result)
{
    if (GetCurrentThreadId() != g_GameThreadId) { Orig_ByType(ctx, stack, result); return; }
    int prev = g_ByTypeMode; unsigned char prevOp = g_ByTypeOp;
    int mode = -1; unsigned char op = 0xFF;
    unsigned char* code = (stack && Readable((unsigned char*)stack + 0x20, 8)) ? *(unsigned char**)((unsigned char*)stack + 0x20) : nullptr;
    if (code && Readable(code, 2)) { op = code[0]; if (op == 0x24 && code[1] <= 2) mode = code[1]; }
    g_ByTypeMode = mode; g_ByTypeOp = op;
    Orig_ByType(ctx, stack, result);
    g_ByTypeMode = prev; g_ByTypeOp = prevOp;
}
static void Hook_UnitSetTarget(void* unit, void* ch)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    bool gt = GetCurrentThreadId() == g_GameThreadId;
    uintptr_t prev = g_TsOuter;
    uintptr_t outer = (g_Base && ret > g_Base) ? ret - g_Base : 0;
    if (gt) { g_TsOuter = outer; void* asked = ch; ch = BossTargetPick(unit, ch, outer); BossPhaseScriptCall(unit, outer, asked, ch); BossCatCall(unit, outer, asked, ch); }   // f1.213: a boss's script "target the leader" -> one of the people playing (f1.219: logged for bosses)
    Orig_UnitSetTarget(unit, ch);
    if (gt) g_TsOuter = prev;
}
// every 30 s in a battle (from TargetLogTick's summary): the writes per code place
static void TsSummary()
{
    int shown = 0;
    for (auto& t : g_TsTally)
    {
        if (!t.caller) continue;
        if (shown < 12)
        {
            if (t.outer) CoopLog("TARGETSET: last 30 s - UnitSetTarget from RVA 0x%llX: %d write(s) onto P1, %d P2, %d P3, %d AI, %d nobody / other",
                (unsigned long long)t.outer, t.n[0], t.n[1], t.n[2], t.n[3], t.n[4]);
            else CoopLog("TARGETSET: last 30 s - code at RVA 0x%llX: %d write(s) onto P1, %d P2, %d P3, %d AI, %d nobody / other",
                (unsigned long long)t.caller, t.n[0], t.n[1], t.n[2], t.n[3], t.n[4]);
            ++shown;
        }
        t = TsTally();
    }
}
static void InstallTargetSetLog()
{
    static const unsigned char ex1[17] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56};
    void* t1 = MakeInlineHook(g_Base + 0x997FB0, (void*)Hook_SetUnitTarget, 17, ex1, "enemy target writes (TARGETSET, log only)");
    if (t1) Orig_SetUnitTarget = (SetUnitTarget_t)t1;
    static const unsigned char ex2[17] = {0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x83,0xB9,0x10,0x03,0x00,0x00,0x00};
    void* t2 = MakeInlineHook(g_Base + 0x96BFA0, (void*)Hook_UnitSetTarget, 17, ex2, "UnitSetTarget (TARGETSET, log only)");
    if (t2) Orig_UnitSetTarget = (UnitSetTarget_t)t2;
    CoopLog("TARGETSET: target-write logging %s (log only)", t1 && t2 ? "on" : "NOT installed");
    // f1.225: SetTargetByType's exec thunk (mov [rsp+8],rbx; push rdi; sub rsp,20h; mov rax,[rdx+20h] - 14 bytes, nothing RIP-relative)
    static const unsigned char ex3[14] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x42,0x20};
    void* t3 = MakeInlineHook(g_Base + 0x17BC210, (void*)Hook_ByType, 14, ex3, "SetTargetByType (boss target modes)");
    if (t3) Orig_ByType = (BpExec_t)t3;
    CoopLog("BOSSTARGET: SetTargetByType mode reading %s", t3 ? "on" : "NOT installed - its picks are left to the game");
}
