// =================================================================================
// f1.226 BOSSACT - LOG ONLY (the author, after f1.225: "Build the attack logging first" - for a test: the Eligor fight played
// as Aerith, the ranged character, "to determine what it does when it targets a far away enemy"). Which moves a boss's
// script picks, how far its target is when it does, and whether the move reaches anyone. Nothing is changed here.
// Found (exe 2026-10-30, AEndBattleAIController, CXXHeaderDump EndGame.hpp):
//  - RegisterAbility(Name, ID, Time, Timeout, ATB, RandomATB, MinRange, MaxRange, ActionMin, ActionMax, HeightMin,
//    HeightMax, ViewRange, ViewReverse, DelayMove, DelayAbility, DelayRandAbility, NeedMove, NeedBetterMove,
//    AerialNeedMove, CancelLostTarget, OverrideLeft, OverrideRight, NeedSee) -> bool: exec 0x17B95B0 -> native 0x923470
//    (this = the AI controller, FNames by value) - hooked (15 bytes), every argument logged;
//  - RequestAction / ForceRequestAction / RequestActionStep (Name) -> native 0x923AC0(this, FName) -> bool - hooked
//    (15 bytes): a script routine by name;
//  - movement commands, exec thunks hooked (the call is logged, not its arguments): RegisterMove 0x17B8E50,
//    RequestActorChase 0x17B49E0, RequestPathFind 0x17B7730, RegisterTurn 0x17B8340, RequestDodge 0x17B0CF0;
//  - events on its AI controller (UObject::ProcessEvent, core/06 + marker/19 ClassifyFn): OnPrepareAbility(Name),
//    OnBeginAbility(Chara, AbilityName), OnCompleteAbility(Name), OnSuspendAbility(Name, Factor, Timing)
//    (EEndAiAbilitySuspendFactor TimeOut / DamageReaction / ClearAction / SuspendAction / Burst / Weak / TargetLost /
//    Sleep; EEndAiAbilitySuspendTiming PreparingRange / SettingRange / PlayingMotion), OnInterceptAbility(Name),
//    OnArrivalRegisterMove / OnCompleteRegisterMove(Name), OnChangeStep(Step), OnChangePhase(New, Old).
// Per move: its target and the distance when picked (prepare) and when it starts; the closest it got; the contacts
// (enemies/22e BossRangeContact - a hit or a dodged / guarded one) while it ran (+1.5 s): on its target / on others; "NO
// CONTACT" when none. At the battle's end a table per move: used, average start distance, reached its target, no contact,
// suspended. Remove once the boss answers are in (removal checklist).
// =================================================================================
static const char* const kBaFactor[8] = {"time out", "damage reaction", "action cleared", "action suspended", "stagger (burst)", "weak spot", "TARGET LOST", "sleep"};
static const char* const kBaTiming[3] = {"getting into range", "setting its range", "playing its motion"};
static void BaName(const void* fname, char* out, size_t n)   // an FName's text
{
    out[0] = 0;
    if (!fname) { snprintf(out, n, "-"); return; }
    FStringRaw s{};
    ((FNameToString_t)(g_Base + RVA_FNAME_TOSTRING))((void*)fname, &s);
    if (s.data)
    {
        WideCharToMultiByte(CP_UTF8, 0, s.data, -1, out, (int)n, nullptr, nullptr);
        typedef void (*Free_t)(void*);
        ((Free_t)(g_Base + 0x16C0370))(s.data);
    }
    if (!out[0]) snprintf(out, n, "-");
}
struct BaStat { char name[48] = ""; int used = 0, reached = 0, noContact = 0, suspended = 0, lost = 0, onOther = 0; double dSum = 0; int farUsed = 0, farReached = 0; };
struct BossAct
{
    void* unit = nullptr; void* enemy = nullptr;
    // the move in progress
    bool on = false; char name[48] = ""; void* tgt = nullptr; char tgtName[24] = "nobody"; ULONGLONG prepAt = 0, beginAt = 0, endAt = 0;
    float dPrep = -1, dBegin = -1, dMin = -1;
    int hitT = 0, missT = 0, other = 0; bool reported = true;
    BaStat st[24]; int nst = 0;
};
// f1.239: the target's name is read when it is set (the character is surely alive then) - a battle end / a move cut after
// a save load must not read a freed character (the 22:48:57 crash in the f1.234 boss rush: NameOf on a freed object)
static void BaSetTgt(BossAct* a, void* tgt)
{
    a->tgt = tgt;
    if (tgt) TlShort(tgt, a->tgtName, sizeof(a->tgtName)); else snprintf(a->tgtName, sizeof(a->tgtName), "nobody");
}
static BossAct g_Ba[4];
static BossAct* BaOf(void* unit, void* enemy, bool make)
{
    for (auto& a : g_Ba) if (a.unit == unit) { if (enemy) a.enemy = enemy; return &a; }
    if (!make) return nullptr;
    for (auto& a : g_Ba) if (!a.unit) { a = BossAct(); a.unit = unit; a.enemy = enemy; return &a; }
    return nullptr;
}
static BaStat* BaStatOf(BossAct& a, const char* name)
{
    for (int i = 0; i < a.nst; ++i) if (!strcmp(a.st[i].name, name)) return &a.st[i];
    if (a.nst >= 24) return nullptr;
    BaStat* s = &a.st[a.nst++]; snprintf(s->name, sizeof(s->name), "%s", name); return s;
}
// the boss behind an AI controller (game thread): its BossState, or null
static BossState* BaBossOfCtrl(void* ctrl, void** pawnOut)
{
    if (!ctrl || GetCurrentThreadId() != g_GameThreadId || !BossAnyBoss()) return nullptr;   // f1.230: no boss = no work
    void* pawn = CtrlPawn(ctrl);
    if (!pawn) return nullptr;
    BossState* b = BossOfEnemy(pawn);
    if (b && pawnOut) *pawnOut = pawn;
    return b;
}
static void* BaTarget(BossState* b)   // its current target (unit +0x38), a party member or null
{
    unsigned char* u = (unsigned char*)b->unit;
    void* h = Readable(u + 0x38, 8) ? *(void**)(u + 0x38) : nullptr;
    return TsCharOfHandle(h);
}
static void BaTgtText(BossState* b, void* tgt, char* out, size_t n)   // "Aerith (P1) 14.2 m away"
{
    if (!tgt) { snprintf(out, n, "no party member"); return; }
    char tn[24] = "?"; TlShort(tgt, tn, sizeof(tn));
    float d = -1; BrFlat(b->enemy, tgt, &d, nullptr);
    snprintf(out, n, "%s (%s) %.1f m away", tn, kTlWho[TlWho(tgt, g_AttnHuman)], d / 100.f);
}
static const char* BaM(char* buf, size_t n, float cm)   // f1.230 (review #8): "12.3 m", or "?" when not known
{
    if (cm < 0) snprintf(buf, n, "?"); else snprintf(buf, n, "%.1f m", cm / 100.f);
    return buf;
}
// f1.230 (review #6): ParamOff walks the function's parameters with a name string each - cached per (function, name)
static int BaParamOff(void* fn, const char* pn)
{
    struct E { void* fn; const char* pn; int off; };
    static E cache[48]; static int n = 0;
    for (int i = 0; i < n; ++i) if (cache[i].fn == fn && cache[i].pn == pn) return cache[i].off;
    int o = ParamOff(fn, pn);
    if (n < 48) cache[n++] = {fn, pn, o};
    return o;
}
static void BaFinish(BossAct& a, BossState* b, const char* how)   // the move in progress is over: one line + its tally
{
    if (!a.on) return;
    a.on = false; a.reported = true;
    BaStat* s = BaStatOf(a, a.name);
    bool reached = a.hitT + a.missT > 0;
    if (s)
    {
        if (a.dBegin >= 0) { s->used++; s->dSum += a.dBegin; if (a.dBegin > BR_MID_CM) { s->farUsed++; if (reached) s->farReached++; } if (reached) s->reached++; else if (!a.other) s->noContact++; }
        if (a.other) s->onOther++;
    }
    static LogRate rate;
    if (!LogOk(rate, 60)) return;
    const char* en = b ? b->name : BossNameOf(a.unit); const char* tn = a.tgt ? a.tgtName : "nobody";   // f1.230 / f1.239: stored names (no object reads)
    char m1[16], m2[16], m3[16];
    CoopLog("BOSSACT: %s - %s %s (%.1f s) | aimed at %s: %s when picked, %s at its start, closest %s | contacts on them %d hit(s) + %d dodged / guarded, on others %d%s",
            en, a.name, how, a.beginAt ? (GetTickCount64() - a.beginAt) / 1000.0 : 0.0, tn, BaM(m1, 16, a.dPrep), BaM(m2, 16, a.dBegin), BaM(m3, 16, a.dMin), a.hitT, a.missT, a.other,
            a.dBegin < 0 ? " - it never started" : (a.hitT + a.missT + a.other) ? "" : " - NO CONTACT");
}
// marker/19 ClassifyFn codes: 6 OnBeginAbility, 7 OnCompleteAbility, 8 OnPrepareAbility, 9 OnSuspendAbility,
// 10 OnInterceptAbility, 11 OnArrivalRegisterMove, 12 OnCompleteRegisterMove, 13 OnChangeStep, 14 OnChangePhase
static void GrabCamMoveNote(void* enemy, const char* move);   // camera/38b (f1.241, log only)
static void BossActEvent(int fc, void* fn, void* ctrl, unsigned char* prm)
{
    if (!g_Cfg.targetLog || !prm || !ctrl) return;
    void* pawn = nullptr;
    BossState* b = BaBossOfCtrl(ctrl, &pawn);
    if (!b) return;
    BossAct* a = BaOf(b->unit, b->enemy, true);
    if (!a) return;
    const char* en = b->name;   // f1.230: the stored name
    ULONGLONG now = GetTickCount64();
    static LogRate rate;
    auto nameParam = [&](const char* pn, char* out, size_t n) { int o = BaParamOff(fn, pn); if (o >= 0) BaName(prm + o, out, n); else snprintf(out, n, "?"); };
    char nm[48] = "?", tg[64];
    if (fc == 8 || fc == 6)
    {
        nameParam(fc == 8 ? "InName" : "InAbilityName", nm, sizeof(nm));
        GrabCamMoveNote(b->enemy, nm);   // f1.241 GRABCAM: a grab-like move starts the camera watch
        void* tgt = BaTarget(b); BaTgtText(b, tgt, tg, sizeof(tg));
        float d = -1; if (tgt) BrFlat(b->enemy, tgt, &d, nullptr);
        if (fc == 8 || !a->on || strcmp(a->name, nm))
        {   // a new move (prepared, or begun without a prepare)
            if (a->on) BaFinish(*a, b, "replaced");
            a->on = true; a->reported = false; snprintf(a->name, sizeof(a->name), "%s", nm); BaSetTgt(a, tgt); a->prepAt = now; a->beginAt = 0; a->endAt = 0;
            a->dPrep = d; a->dBegin = -1; a->dMin = d; a->hitT = a->missT = a->other = 0;
        }
        if (fc == 8) { if (LogOk(rate, 60)) CoopLog("BOSSACT: %s prepares %s - its target %s", en, nm, tg); return; }
        a->beginAt = now; a->dBegin = d; BaSetTgt(a, tgt); if (d >= 0 && (a->dMin < 0 || d < a->dMin)) a->dMin = d;
        void* ch = nullptr; { int o = BaParamOff(fn, "InChara"); if (o >= 0) ch = *(void**)(prm + o); }
        char cn[24] = "-"; if (ch) { if (ch == pawn) snprintf(cn, sizeof(cn), "itself"); else TlShort(ch, cn, sizeof(cn)); }
        char m1[16];
        if (LogOk(rate, 60)) CoopLog("BOSSACT: %s begins %s (chara %s) - its target %s (%.1f s after it was picked, %s then)", en, nm, cn, tg, (now - a->prepAt) / 1000.0, BaM(m1, 16, a->dPrep));
        return;
    }
    if (fc == 7 || fc == 9 || fc == 10)
    {
        nameParam("InName", nm, sizeof(nm));
        if (fc == 9)
        {
            int of = BaParamOff(fn, "InFactor"), ot = BaParamOff(fn, "InTiming");
            int f = of >= 0 ? prm[of] : -1, t = ot >= 0 ? prm[ot] : -1;
            void* tgt = BaTarget(b); BaTgtText(b, tgt, tg, sizeof(tg));
            if (LogOk(rate, 60)) CoopLog("BOSSACT: %s's %s SUSPENDED - %s, while %s - its target %s", en, nm, f >= 0 && f < 8 ? kBaFactor[f] : "?", t >= 0 && t < 3 ? kBaTiming[t] : "?", tg);
            if (a->on && !strcmp(a->name, nm)) { BaStat* s = BaStatOf(*a, nm); if (s) { s->suspended++; if (f == 6) s->lost++; } }
        }
        else if (fc == 10)
        {   // f1.231 (S14 Ghoul log: 114 prepares, 113 "intercepted", 0 OnBeginAbility): for this boss OnInterceptAbility is the
            // move STARTING (its motion), not an end - its start distance is taken here; it ends at the next prepare / a
            // complete / suspend (or 30 s)
            void* tgt = BaTarget(b); float d = -1; if (tgt) BrFlat(b->enemy, tgt, &d, nullptr);
            if (a->on && !strcmp(a->name, nm) && !a->beginAt) { a->beginAt = now; a->dBegin = d; if (tgt) BaSetTgt(a, tgt); if (d >= 0 && (a->dMin < 0 || d < a->dMin)) a->dMin = d; }
            char m1[16];
            if (LogOk(rate, 60)) CoopLog("BOSSACT: %s's %s starts (intercept) - its target %s", en, nm, BaM(m1, 16, d));
            return;
        }
        if (a->on && !strcmp(a->name, nm)) { a->endAt = now; a->reported = false; }   // reported 1.5 s later (late contacts count)
        return;
    }
    if (fc == 11 || fc == 12)
    {
        nameParam("InName", nm, sizeof(nm));
        void* tgt = BaTarget(b); BaTgtText(b, tgt, tg, sizeof(tg));
        static LogRate mr;
        if (LogOk(mr, 30)) CoopLog("BOSSACT: %s - move %s %s - its target %s", en, nm, fc == 11 ? "arrived" : "done", tg);
        return;
    }
    if (fc == 13)
    {
        int o = BaParamOff(fn, "InStep");
        if (o >= 0 && LogOk(rate, 60)) CoopLog("BOSSACT: %s's script step -> %d", en, *(int*)(prm + o));
        return;
    }
    if (fc == 14)
    {
        int on = BaParamOff(fn, "InNew"), oo = BaParamOff(fn, "InOld");
        if (on >= 0 && oo >= 0) CoopLog("BOSSACT: %s's script PHASE %d -> %d", en, *(int*)(prm + oo), *(int*)(prm + on));
        return;
    }
}
// enemies/22e BossRangeContact: an attack of this boss connected
static void BossActContact(void* unit, void* tgt, bool dodged)
{
    BossAct* a = BaOf(unit, nullptr, false);
    if (!a || !a->on || !tgt) return;
    if (tgt == a->tgt) { if (dodged) a->missT++; else a->hitT++; }
    else a->other++;
}
// enemies/22d BossTick (4 times a second): the closest it got; a finished move reported 1.5 s after its end
static void BossActTick(ULONGLONG now)
{
    if (!g_Cfg.targetLog) return;
    for (auto& a : g_Ba)
    {
        if (!a.unit || !a.on) continue;
        BossState* b = BossOfUnit(a.unit);
        if (!b || !b->enemy) continue;
        if (now - b->seen > 1000) { BaFinish(a, b, "cut - it left the enemy list"); continue; }   // f1.230 (review #2): no reads of a stale object
        if (a.tgt) { float d = -1; if (BrFlat(b->enemy, a.tgt, &d, nullptr) && (a.dMin < 0 || d < a.dMin)) a.dMin = d; }
        if (a.endAt && now - a.endAt >= 1500) BaFinish(a, b, "done");
        else if (!a.endAt && now - a.prepAt > 30000) BaFinish(a, b, "no end seen in 30 s");
    }
}
// enemies/22d BossTick when the battle is over: the table per move
static void BossActBattleEnd()
{
    for (auto& a : g_Ba)
    {
        if (!a.unit) continue;
        BossState* b = BossOfUnit(a.unit);
        if (a.on) BaFinish(a, b, "cut by the battle's end");
        const char* en = b ? b->name : "?";   // f1.230: the stored name
        CoopLog("BOSSACT: battle summary for %s - %d different move(s):", en, a.nst);
        for (int i = 0; i < a.nst; ++i)
        {
            BaStat& s = a.st[i];
            CoopLog("BOSSACT:   %s - started %d, average distance at its start %.1f m, reached its target %d, NO contact %d, hit someone else %d, suspended %d (target lost %d) | started over 8 m away %d, of them reached %d",
                s.name, s.used, s.used ? s.dSum / s.used / 100.0 : 0.0, s.reached, s.noContact, s.onOther, s.suspended, s.lost, s.farUsed, s.farReached);
        }
    }
    for (auto& a : g_Ba) a = BossAct();
}
// ---- the script's commands (hooks)
typedef bool (*BaRegAbility_t)(void* self, uint64_t name, uint64_t id, float time, float timeout, int atb, int ratb, float minR, float maxR,
    float actMin, float actMax, float hMin, float hMax, float view, bool viewRev, float dMove, float dAbil, float dRand, bool needMove,
    bool needBetter, bool aerialNeed, bool cancelLost, uint64_t ovL, uint64_t ovR, bool needSee);
static BaRegAbility_t Orig_BaRegAbility = nullptr;
static bool Hook_BaRegAbility(void* self, uint64_t name, uint64_t id, float time, float timeout, int atb, int ratb, float minR, float maxR,
    float actMin, float actMax, float hMin, float hMax, float view, bool viewRev, float dMove, float dAbil, float dRand, bool needMove,
    bool needBetter, bool aerialNeed, bool cancelLost, uint64_t ovL, uint64_t ovR, bool needSee)
{
    bool r = Orig_BaRegAbility(self, name, id, time, timeout, atb, ratb, minR, maxR, actMin, actMax, hMin, hMax, view, viewRev, dMove, dAbil, dRand,
        needMove, needBetter, aerialNeed, cancelLost, ovL, ovR, needSee);
    BossState* b = g_Cfg.targetLog ? BaBossOfCtrl(self, nullptr) : nullptr;
    static LogRate rate;
    if (b && LogOk(rate, 60))   // f1.230: the rate check before any string work
    {
        const char* en = b->name; char nm[48], idn[48], tg[64]; BaName(&name, nm, sizeof(nm)); BaName(&id, idn, sizeof(idn));
        BaTgtText(b, BaTarget(b), tg, sizeof(tg));
        CoopLog("BOSSACT: %s registers %s (ID %s)%s - range %g - %g, action range %g - %g, height %g - %g, view %g%s | needs a move %d, a better move %d, aerial %d, cancel if target lost %d, needs to see it %d | ATB %d +%d, time %g, timeout %g, delays move %g / ability %g +%g | its target %s",
            en, nm, idn, r ? "" : " (REFUSED)", minR, maxR, actMin, actMax, hMin, hMax, view, viewRev ? " (reversed)" : "", needMove, needBetter, aerialNeed, cancelLost, needSee,
            atb, ratb, time, timeout, dMove, dAbil, dRand, tg);
    }
    return r;
}
typedef bool (*BaReqAction_t)(void* self, uint64_t name);
static BaReqAction_t Orig_BaReqAction = nullptr;
static bool Hook_BaReqAction(void* self, uint64_t name)
{
    bool r = Orig_BaReqAction(self, name);
    BossState* b = g_Cfg.targetLog ? BaBossOfCtrl(self, nullptr) : nullptr;
    static LogRate rate;
    if (b && LogOk(rate, 60))   // f1.230: the rate check before any string work
    {
        const char* en = b->name; char nm[48], tg[64]; BaName(&name, nm, sizeof(nm)); BaTgtText(b, BaTarget(b), tg, sizeof(tg));
        CoopLog("BOSSACT: %s requests action %s%s - its target %s", en, nm, r ? "" : " (refused)", tg);
    }
    return r;
}
// movement commands (exec thunks: ctx = the AI controller)
typedef void (*BaExec_t)(void* ctx, void* stack, void* result);
static const char* const kBaMove[5] = {"RegisterMove (walk to a spot)", "RequestActorChase (chase)", "RequestPathFind (path to its target)", "RegisterTurn (turn)", "RequestDodge (dodge)"};
static BaExec_t Orig_BaMove[5] = {};
static void BaMoveNote(int k, void* ctx)
{
    BossState* b = g_Cfg.targetLog ? BaBossOfCtrl(ctx, nullptr) : nullptr;
    static LogRate rate;
    if (!b || !LogOk(rate, 40)) return;   // f1.230: the rate check before any string work
    const char* en = b->name; char tg[64]; BaTgtText(b, BaTarget(b), tg, sizeof(tg));
    CoopLog("BOSSACT: %s - %s - its target %s", en, kBaMove[k], tg);
}
static void Hook_BaMove0(void* c, void* s, void* r) { Orig_BaMove[0](c, s, r); BaMoveNote(0, c); }
static void Hook_BaMove1(void* c, void* s, void* r) { Orig_BaMove[1](c, s, r); BaMoveNote(1, c); }
static void Hook_BaMove2(void* c, void* s, void* r) { Orig_BaMove[2](c, s, r); BaMoveNote(2, c); }
static void Hook_BaMove3(void* c, void* s, void* r) { Orig_BaMove[3](c, s, r); BaMoveNote(3, c); }
static void Hook_BaMove4(void* c, void* s, void* r) { Orig_BaMove[4](c, s, r); BaMoveNote(4, c); }
static void InstallBossActLog()
{
    int ok = 0;
    static const unsigned char exReg[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x56};
    if (void* t = MakeInlineHook(g_Base + 0x923470, (void*)Hook_BaRegAbility, 15, exReg, "RegisterAbility (BOSSACT, log only)")) { Orig_BaRegAbility = (BaRegAbility_t)t; ++ok; }
    static const unsigned char exReq[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
    if (void* t = MakeInlineHook(g_Base + 0x923AC0, (void*)Hook_BaReqAction, 15, exReq, "RequestAction (BOSSACT, log only)")) { Orig_BaReqAction = (BaReqAction_t)t; ++ok; }
    static const unsigned char exMove[14] = {0x48,0x8B,0xC4,0x4C,0x89,0x40,0x18,0x48,0x89,0x48,0x08,0x55,0x53,0x56};
    static const unsigned char exChase[15] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20};
    static const unsigned char exPath[15] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20};
    static const unsigned char exTurn[16] = {0x40,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
    struct M { uintptr_t rva; void* hook; size_t len; const unsigned char* ex; const char* nm; };
    const M ms[5] = {
        {0x17B8E50, (void*)Hook_BaMove0, 14, exMove, "RegisterMove (BOSSACT, log only)"},
        {0x17B49E0, (void*)Hook_BaMove1, 15, exChase, "RequestActorChase (BOSSACT, log only)"},
        {0x17B7730, (void*)Hook_BaMove2, 15, exPath, "RequestPathFind (BOSSACT, log only)"},
        {0x17B8340, (void*)Hook_BaMove3, 16, exTurn, "RegisterTurn (BOSSACT, log only)"},
        {0x17B0CF0, (void*)Hook_BaMove4, 15, exChase, "RequestDodge (BOSSACT, log only)"}};
    for (int k = 0; k < 5; ++k) if (void* t = MakeInlineHook(g_Base + ms[k].rva, ms[k].hook, ms[k].len, ms[k].ex, ms[k].nm)) { Orig_BaMove[k] = (BaExec_t)t; ++ok; }
    CoopLog("BOSSACT: boss move logging - %d of 7 hooks in (log only)", ok);
}
