// =================================================================================
// f1.219 BOSSHP / BOSSSET / BOSSIDLE - LOG ONLY (the author, f1.218 Ghoul fight 13:02: "bosses in the game all have
// 'Phases' where they stop taking damage and their routines start to change. A cutscene always occurs during these
// phases ... This should have been one of those changes, but it failed." - "I'd like to determine the cause and fix the
// bug. Go ahead and build the logging"). The f1.218 log could not show the boss's HP (DAMAGE lines are capped, the HP
// re-apply stops after 10 tries) nor every target write on it. Nothing is changed here. For each boss:
//  - BOSSHP once a second when something changed (5 s otherwise): the game's HP / max HP (and %), the hits it took
//    since the last line and their damage - "NOT going down" when hits landed but its HP did not move (a phase lock /
//    immune) - its unit state byte (+0x611), its target, whether it is still in the enemy list; a line when its max HP
//    changes (the game resetting the mod's HP multiplier, or a phase);
//  - BOSSSET every target write on it: its script's UnitSetTarget calls (which code, the character asked for, what the
//    mod passed on) and direct writes by other code (60 a minute each);
//  - BOSSIDLE when its script hasn't asked for a target for 5 s in a battle (then every 10 s): HP, target, state.
// The boss is followed through a weak reference (no stale pointer reads). Remove once the phase answer is in (removal
// checklist).
// =================================================================================
struct BossPhase
{
    int idx = -1, ser = 0; void* unit = nullptr; char name[96] = "?";
    int lastCur = -1, lastMax = -1; int st611 = -1; bool inList = true;
    ULONGLONG lastLine = 0, lastScript = 0, idleAt = 0, made = 0;
    int hitsN = 0, zeroN = 0; long long hitsDmg = 0;
    // f1.220 phase guard: its HP sits on the game's HP floor (a phase lock) - the mod leaves its target to the game
    bool locked = false, lockSet = false; ULONGLONG lockAt = 0; int lastFloor = -2, lastFloor2 = -2;
    ULONGLONG otherAt = 0;   // f1.227: when its HP went down to its phase-start line (the "other floor")
    int evalLine = -2; bool lockFloor = false;   // f1.230 (review #5): the line otherAt belongs to; the lock is (also) on the floor
};
static BossPhase g_Bp[4];
static BossPhase* BpOfUnit(void* unit)
{
    for (auto& p : g_Bp) if (p.unit && p.unit == unit) return &p;
    return nullptr;
}
static bool BpHP(void* unit, int* cur, int* mx)
{
    unsigned char* u = (unsigned char*)unit;
    unsigned char* st = (u && Readable(u + 0x5E8, 8)) ? *(unsigned char**)(u + 0x5E8) : nullptr;
    if (!st || !Readable(st, 0x40)) return false;
    void** vt = *(void***)st;
    if (!vt || !Readable(vt, 0x68) || !InText(vt[0x48 / 8]) || !InText(vt[0x58 / 8])) return false;
    *cur = ((StatGetI_t)vt[0x48 / 8])(st); *mx = ((StatGetI_t)vt[0x58 / 8])(st);
    return true;
}
// (f1.220 RE, exe 2026-10-29): the HP floor a boss's script sets for a phase change - AEndBattleAIController::
// SetHPMinLimitPercent (exec 0x17BD200: ceil(max HP x percent / 100)) / SetHPMinLimit (0x17BD3B0) -> its unit's status
// (+0x5E8, vtable 0x4B6C3E8) vfunc +0x110 = status +0x38; ClearHPMinLimit (0x17BD150) writes -1. Read with vfunc +0x108.
// (+0x3C / vfunc +0x118 = the PassiveDamage / AIPCAttack floor - logged only.) Damage stops at the floor: the game's own
// "can't be hurt" for a phase change, not other immunities.
static bool BpFloor(void* unit, int* fl, int* fl2)
{
    unsigned char* u = (unsigned char*)unit;
    unsigned char* st = (u && Readable(u + 0x5E8, 8)) ? *(unsigned char**)(u + 0x5E8) : nullptr;
    if (!st || !Readable(st, 0x40)) return false;
    void** vt = *(void***)st;
    if (!vt || !Readable(vt, 0x120) || !InText(vt[0x108 / 8]) || !InText(vt[0x118 / 8])) return false;
    *fl = ((StatGetI_t)vt[0x108 / 8])(st); *fl2 = ((StatGetI_t)vt[0x118 / 8])(st);
    return true;
}
static void BpTargetName(void* unit, char* out, size_t n)
{
    unsigned char* u = (unsigned char*)unit;
    void* h = (u && Readable(u + 0x38, 8)) ? *(void**)(u + 0x38) : nullptr;
    void* ch = TsCharOfHandle(h);
    if (ch) { char s[24]; TlShort(ch, s, sizeof(s)); snprintf(out, n, "%s (%s)", s, kTlWho[TlWho(ch, g_AttnHuman)]); }
    else snprintf(out, n, h ? "not a party member" : "nobody");
}
// enemies/22c Hook_UnitSetTarget: every UnitSetTarget call on a boss (asked = the caller's character, given = what goes on)
static void BossPhaseScriptCall(void* unit, uintptr_t outer, void* asked, void* given)
{
    if (!g_Cfg.targetLog) return;
    BossPhase* p = BpOfUnit(unit);
    if (!p) return;
    if (outer == RVA_BP_SETTARGET_RET) p->lastScript = GetTickCount64();
    char a[24] = "nobody", g[24] = "nobody";
    if (asked) TlShort(asked, a, sizeof(a));
    if (given) TlShort(given, g, sizeof(g));
    static LogRate rate;
    char bt[48]; snprintf(bt, sizeof(bt), " (its script's SetTargetByType: %s)", ByTypeName(g_ByTypeMode));   // f1.225 (was "target the nearest")
    if (LogOk(rate, 60)) CoopLog("BOSSSET: %s - UnitSetTarget from RVA 0x%llX%s asked for %s%s -> %s%s", p->name, (unsigned long long)outer,
        outer == RVA_BP_SETTARGET_RET ? " (its script's \"target the leader\")" : outer == RVA_BP_BYTYPE_RET ? bt : outer == RVA_BP_SETPLAYER_RET ? " (its script's SetTargetPlayer)" : "",
        a, asked && asked == LeaderChar() ? " (the leader)" : "", g, given != asked ? " - CHANGED by the mod" : "");
}
// enemies/22c Hook_SetUnitTarget: a target written on a boss's unit directly (not through UnitSetTarget)
static void BossPhaseDirect(void* unit, void* h, uintptr_t caller)
{
    if (!g_Cfg.targetLog) return;
    BossPhase* p = BpOfUnit(unit);
    if (!p) return;
    void* ch = TsCharOfHandle(h); char g[24] = "nobody";
    if (ch) TlShort(ch, g, sizeof(g)); else if (h) snprintf(g, sizeof(g), "non-party");
    static LogRate rate;
    if (LogOk(rate, 60)) CoopLog("BOSSSET: %s - target written directly by code at RVA 0x%llX -> %s", p->name, (unsigned long long)caller, g);
}
// ---- f1.220 PHASE GUARD (the author: "Yes", after the f1.218 / f1.219 Ghoul fights; "During the phase change the boss gets an
// indicator that it is immune to damage, maybe that should be used and not just hits that didn't do damage as that can
// happen anyway"). A boss's phase change starts with its HP on the game's HP floor (f1.219 log 13:26:43: 10090 / 18683 =
// 54.0 %, 21 hits for 2,500+ and it didn't move). In the two fights where the phase never came the mod changed its
// target right after (f1.219: 0.5 s later, then 3 more times; f1.218: 3 times in 12 s); where it came, the mod made no
// change then (f1.214) or the phase move had already started on the leader (f1.217); without the mod's boss targeting
// (f1.208 Airbuster, f1.211 Ghoul) it always came. So while its HP is ON the game's floor (status +0x38 - not "hits did
// no damage", which other immunities do too) its script's own choice (the leader) goes through, and its target is put
// back on the leader once - the normal game - until the floor is cleared / its HP is below it.
// f1.227 (the author: "Go ahead and do the fixes now"; S13 Eligor: its phase change began at ~70 % - unit state 0x08 at
// 20:46:19 - while its HP floor was at 65 %, so the guard came in only with the cutscene 8 s later): a phase change STARTS at
// the other line its script sets with the floor - status +0x3C, "passive damage / AI attacks" (Ghoul 55 % over a 54 % floor,
// Eligor 70 % / 65 %, then 50 % / 43.75 %). The guard now also holds from the moment its HP is at or below that line
// (while it is set) - for at most 30 s per crossing (in case a boss keeps that line for other reasons) - and, as before,
// all the time its HP is on the floor itself. (Both lines are fixed HP numbers: since f1.227 a boss's max HP is no longer
// changed by EnemyHP, so they stay where the game put them.)
static constexpr ULONGLONG BOSS_OTHER_LINE_HOLD_MS = 30000;
static void BpEval(BossPhase& p, int cur, int mx, int floor, int floor2, const char* where)
{
    ULONGLONG t = GetTickCount64();
    bool onFloor = floor >= 0 && cur > 0 && cur <= floor + 1;   // f1.221: the game stops it 1 above the floor (f1.220 log 14:00:41: floor 10089, HP 10090)
    bool onLine = floor2 > 0 && cur > 0 && cur <= floor2 + 1;
    if (floor2 != p.evalLine) { p.evalLine = floor2; p.otherAt = 0; }   // f1.230 (review #5): a new line (a new phase) gets its own hold
    if (!onLine) p.otherAt = 0; else if (!p.otherAt) p.otherAt = t;
    bool lineHold = onLine && t - p.otherAt < BOSS_OTHER_LINE_HOLD_MS;
    bool now = onFloor || lineHold;
    if (now && p.locked && onFloor && !p.lockFloor)
        CoopLog("BOSSTARGET: %s - now on the game's HP floor too (HP %d, floor %d) - still left to the game", p.name, cur, floor);   // f1.230
    p.lockFloor = now && onFloor;
    if (now == p.locked) return;
    p.locked = now;
    if (now)
    {
        p.lockSet = false; p.lockAt = t;
        if (onFloor) CoopLog("BOSSTARGET: %s's HP is on the game's HP floor - %d / %d (%.1f%%), floor %d (%s): a phase change - the mod leaves its target to the game (the leader)",
            p.name, cur, mx, mx > 0 ? 100.0 * cur / mx : 0.0, floor, where);
        else CoopLog("BOSSTARGET: %s's HP reached its phase-start line - %d / %d (%.1f%%), line %d, floor %d (%s): a phase change is starting - the mod leaves its target to the game (the leader) for up to %.0f s",
            p.name, cur, mx, mx > 0 ? 100.0 * cur / mx : 0.0, floor2, floor, where, BOSS_OTHER_LINE_HOLD_MS / 1000.0);
    }
    else
    {
        CoopLog("BOSSTARGET: %s - %s (HP %d, floor %d, line %d, %.0f s) - the mod's boss targeting is back", p.name,
            onLine ? "30 s on its phase-start line without reaching the floor" : "off the HP floor / phase line", cur, floor, floor2, (t - p.lockAt) / 1000.0);
        if (BossState* bs = BossOfUnit(p.unit)) bs->afterPhase = true;   // f1.240: its next pick (enemies/22d BossRotate) is a random one
    }
}
static bool BossLocked(void* unit)   // enemies/22d BossTargetPick
{
    BossPhase* p = BpOfUnit(unit);
    return p && p->locked;
}
// enemies/22 (the take-damage event, before the "no damage" return): every hit on a boss, 0 damage included
static void BossPhaseHit(void* self, int dmg)
{
    if (!self) return;   // f1.227: not only with TargetLog - the phase guard depends on it
    unsigned char* u = Readable((unsigned char*)self + OFF_BATTLE_UNIT, 8) ? *(unsigned char**)((unsigned char*)self + OFF_BATTLE_UNIT) : nullptr;
    BossPhase* p = u ? BpOfUnit(u) : nullptr;
    if (!p) return;
    p->hitsN++; if (dmg > 0) p->hitsDmg += dmg; else p->zeroN++;
    if (GetCurrentThreadId() != g_GameThreadId) return;
    int cur = -1, mx = -1, fl = -1, fl2 = -1;   // (f1.220) right after the hit: on the floor now?
    if (BpHP(u, &cur, &mx) && BpFloor(u, &fl, &fl2)) BpEval(*p, cur, mx, fl, fl2, "after a hit");
}
// enemies/22d BossTick (4 times a second, in a battle)
static void BossPhaseTick(ULONGLONG now)
{   // f1.227: runs with TargetLog off too (the phase guard lives here) - only the 1-second BOSSHP / BOSSIDLE lines need it
    const bool lg = g_Cfg.targetLog;
    for (auto& b : g_Boss)
    {   // a new boss: followed by a weak reference
        if (!b.unit || !b.boss || !b.enemy || now - b.seen > 1000 || BpOfUnit(b.unit)) continue;
        for (auto& p : g_Bp)
        {
            if (p.unit) continue;
            p = BossPhase(); int idx, ser;
            if (!MakeWeak(b.enemy, &idx, &ser)) break;
            p.idx = idx; p.ser = ser; p.unit = b.unit; p.made = p.lastScript = now; NameOf(b.enemy, p.name, sizeof(p.name));
            if (lg) CoopLog("BOSSHP: following %s (HP once a second, every target write, a quiet script)", p.name);
            break;
        }
    }
    for (auto& p : g_Bp)
    {   // (f1.220) 4 times a second: on the game's HP floor? (a weak-reference check first - no stale unit reads)
        if (!p.unit || GetCurrentThreadId() != g_GameThreadId || !ResolveWeak(p.idx, p.ser)) continue;
        int cur = -1, mx = -1, fl = -1, fl2 = -1;
        if (BpHP(p.unit, &cur, &mx) && BpFloor(p.unit, &fl, &fl2)) BpEval(p, cur, mx, fl, fl2, "tick");
    }
    for (auto& p : g_Bp)
    {   // (f1.220) a locked boss's target goes back on the leader once (as in the normal game) - 4 times a second
        if (!p.unit || !p.locked || p.lockSet || !Orig_UnitSetTarget || GetCurrentThreadId() != g_GameThreadId) continue;
        p.lockSet = true;
        void* e = ResolveWeak(p.idx, p.ser); if (!e) continue;
        void* lead = LeaderChar(); int lhp = 0, lmx = 0;
        if (!lead || !EnemyHP(lead, &lhp, &lmx) || lhp <= 0) { CoopLog("BOSSTARGET: %s - the leader is down, its target stays", p.name); continue; }
        unsigned char* u = (unsigned char*)p.unit;
        void* h = Readable(u + 0x38, 8) ? *(void**)(u + 0x38) : nullptr;
        if (TsCharOfHandle(h) == lead) continue;
        Orig_UnitSetTarget(p.unit, lead);   // the plain game function (not through the mod's hook)
        char tg[48]; BpTargetName(p.unit, tg, sizeof(tg));
        CoopLog("BOSSTARGET: %s's target put back on the leader for its phase change (now %s)", p.name, tg);
    }
    static ULONGLONG at = 0;
    if (now - at < 1000) return;
    at = now;
    for (auto& p : g_Bp)
    {
        if (!p.unit) continue;
        void* e = ResolveWeak(p.idx, p.ser);
        if (!e) { if (lg) CoopLog("BOSSHP: %s is gone (its object was freed)", p.name); p = BossPhase(); continue; }
        unsigned char* u = Readable((unsigned char*)e + OFF_BATTLE_UNIT, 8) ? *(unsigned char**)((unsigned char*)e + OFF_BATTLE_UNIT) : nullptr;
        if (u != (unsigned char*)p.unit) { if (lg) CoopLog("BOSSHP: %s - its battle unit changed (%p -> %p)", p.name, p.unit, u); if (!u) { p = BossPhase(); continue; } p.unit = u; }
        if (!lg) continue;   // f1.227: the rest is logging
        bool inList = false; for (auto& b : g_Boss) if (b.unit == p.unit && now - b.seen <= 1000) inList = true;
        int cur = -1, mx = -1; BpHP(p.unit, &cur, &mx);
        int st = Readable(u + 0x611, 1) ? u[0x611] : -1;
        char tg[48]; BpTargetName(p.unit, tg, sizeof(tg));
        bool changed = cur != p.lastCur || mx != p.lastMax || st != p.st611 || inList != p.inList || p.hitsN > 0;
        if (mx != p.lastMax && p.lastMax > 0)
            CoopLog("BOSSHP: %s max HP %d -> %d (the game reset it / a phase%s)", p.name, p.lastMax, mx, mx < p.lastMax ? " - the mod's HP multiplier is gone" : "");
        {   // (f1.220) the game's HP floor (the phase line its script set) - every change logged
            int fl = -1, fl2 = -1;
            if (BpFloor(p.unit, &fl, &fl2) && (fl != p.lastFloor || fl2 != p.lastFloor2))
            {
                CoopLog("BOSSHP: %s - the game's HP floor %s%d (%.1f%% of max %d) | other floor (passive damage / AI attacks) %d | HP now %d",
                    p.name, fl < 0 ? "CLEARED " : "set to ", fl, mx > 0 && fl >= 0 ? 100.0 * fl / mx : 0.0, mx, fl2, cur);
                p.lastFloor = fl; p.lastFloor2 = fl2;
            }
        }
        if (changed || now - p.lastLine >= 5000)
        {
            const char* note = "";
            if (p.hitsDmg > 0 && cur == p.lastCur && mx == p.lastMax) note = " - NOT going down (hits landed, its HP didn't move: a phase lock / immune?)";
            CoopLog("BOSSHP: %s HP %d / %d (%.1f%%) | since the last line %d hit(s) for %lld (%d for 0) | unit state 0x%02X | target %s | in the enemy list %s%s",
                p.name, cur, mx, mx > 0 ? 100.0 * cur / mx : 0.0, p.hitsN, p.hitsDmg, p.zeroN, st & 0xFF, tg, inList ? "yes" : "NO", note);
            p.lastLine = now; p.hitsN = 0; p.hitsDmg = 0; p.zeroN = 0;
        }
        p.lastCur = cur; p.lastMax = mx; p.st611 = st; p.inList = inList;
        if (InBattleNow() && cur > 0 && now - p.lastScript >= 5000 && (!p.idleAt || now - p.idleAt >= 10000))
        {
            p.idleAt = now;
            CoopLog("BOSSIDLE: %s's script hasn't asked for a target for %.0f s - HP %d / %d (%.1f%%), unit state 0x%02X, target %s, in the enemy list %s",
                p.name, (now - p.lastScript) / 1000.0, cur, mx, mx > 0 ? 100.0 * cur / mx : 0.0, st & 0xFF, tg, inList ? "yes" : "NO");
        }
        if (now - p.lastScript < 5000) p.idleAt = 0;
    }
}
static void BossPhaseBattleEnd()
{
    for (auto& p : g_Bp) if (p.unit) { CoopLog("BOSSHP: %s - battle over (last HP %d / %d)", p.name, p.lastCur, p.lastMax); p = BossPhase(); }
}
// ---- f1.231 A BOSS'S PHASE CHANGE NEEDS THE PLAYER'S HIT (S14, 2026-10-30: the Ghoul sat on its HP floor 1 min 51 s - player
// 2's Tifa had pushed it there (f1.229 lets their hits past the phase-start line), player 1 and the AI teammate were down
// most of that time, and the phase change began 0.5 s after player 1's FIRST hit at 20:29:15 (unit state 0x08). The same
// in every Ghoul fight logged: f1.220 14:00:40 and f1.221 14:24:02 - player 1's hit on the floor, phase at once; f1.219
// 13:26:43 - only player 2 hit it on the floor, the phase never came (the "boss immune" bug f1.220 was built for). The
// boss's script starts its phase from OnTakeDamage(... useAbilityUCPC) - a hit by the character the player controls. In the
// normal game AI teammates never start it; in co-op players 2 / 3 must.) Before the boss's OnTakeDamage handler runs
// (core/06, ProcessEvent), a hit by player 2 / 3's character (not the leader, not a heal) gets useAbilityUCPC = true.
// f1.233 (S15 20:54:28: player 2's hit took the Ghoul to its floor with the f1.231 flag set - no phase; player 1's hit 11 s
// later started it at once): the flag alone isn't what its script checks. Its OnTakeDamage handler (and OnTakeDamageOnce /
// OnIsHitDamage) now also runs with "the leader" = the one who hit it: the Blueprint's GetLeader (exec 0x17B32B0 -> party
// GetLeader 0x15F1CE0, passes/07 Hook_GetLeader) answers player 2 / 3's character for the length of that handler only.
// Logs: each player's first 5 hits on a boss with the game's flag, and how often the handler asked for the leader (if it
// never asks, its script checks the hitter some other way).
static void* g_BossEvWho = nullptr; static int g_BossEvFc = 0; static const char* g_BossEvName = nullptr;
static bool BossEventPre(int fc, void* fn, void* ctrl, unsigned char* prm)
{
    if (!fn || !ctrl || !prm || GetCurrentThreadId() != g_GameThreadId || !BossAnyBoss() || g_LeaderOverride) return false;
    void* pawn = CtrlPawn(ctrl);
    BossState* b = pawn ? BossOfEnemy(pawn) : nullptr;
    if (!b) return false;
    // f1.273 (code review #50): each boss class has its own handler functions (up to 3 per boss) and the old 12-entry
    // cache was never cleared - after ~4 - 5 bosses in one game launch every later boss's events returned false (P2 / P3
    // couldn't start its phase any more) and this line was written on every hit. Now 48 entries, the oldest replaced when
    // full; the line only when an event's offsets differ from the last ones logged for it (every log so far: the same).
    struct Offs { void* fn; int chara, ucpc, heal; };
    static Offs cache[48]; static int n = 0, next = 0;
    Offs* o = nullptr;
    for (int i = 0; i < n; ++i) if (cache[i].fn == fn) { o = &cache[i]; break; }
    if (!o)
    {
        const char* cp = fc == 5 ? "InChara" : fc == 15 ? "InCauserChara" : "causerChara";
        Offs x{fn, ParamOff(fn, cp), fc == 5 ? ParamOff(fn, "useAbilityUCPC") : -1, fc == 5 ? ParamOff(fn, "InHeal") : -1};
        static int seen[3][3] = {{-9, -9, -9}, {-9, -9, -9}, {-9, -9, -9}};   // last logged per event (5 / 15 / 16)
        int* sv = seen[fc == 5 ? 0 : fc == 15 ? 1 : 2];
        if (sv[0] != x.chara || sv[1] != x.ucpc || sv[2] != x.heal)
        {
            sv[0] = x.chara; sv[1] = x.ucpc; sv[2] = x.heal;
            CoopLog("BOSSTARGET: %s params (boss) - causer +%d, useAbilityUCPC +%d, heal +%d%s", fc == 5 ? "OnTakeDamage" : fc == 15 ? "OnTakeDamageOnce" : "OnIsHitDamage",
                x.chara, x.ucpc, x.heal, x.chara < 0 ? " - causer NOT found" : "");
        }
        if (n < 48) { cache[n++] = x; o = &cache[n - 1]; }
        else { cache[next] = x; o = &cache[next]; next = (next + 1) % 48; }
    }
    if (o->chara < 0) return false;
    void* who = *(void**)(prm + o->chara);
    if (!who || (o->heal >= 0 && prm[o->heal])) return false;
    bool lead = who == LeaderChar();
    bool human = !lead && (who == g_AttnHuman[0] || who == g_AttnHuman[1]);
    if (!lead && !human) return false;
    int was = o->ucpc >= 0 ? prm[o->ucpc] : -1;
    static int logs[2] = {0, 0};
    if (fc == 5 && logs[human ? 1 : 0] < 5)
    {   // the first few per side: what the game said (player 1's lines show what the flag normally is)
        ++logs[human ? 1 : 0]; char cn[24] = "?"; TlShort(who, cn, sizeof(cn));
        CoopLog("BOSSTARGET: %s hit by %s (%s) - the game's player-controlled flag %d%s", b->name, cn, lead ? "player 1" : "player 2 / 3", was,
            human ? " -> 1, and for its script's handler the leader is them (f1.231 / f1.233)" : "");
    }
    if (!human) return false;
    if (o->ucpc >= 0 && !prm[o->ucpc]) prm[o->ucpc] = 1;   // f1.231
    g_LeaderOverride = who; g_LeaderOverrideAsked = 0; g_BossEvWho = who; g_BossEvFc = fc; g_BossEvName = b->name;   // f1.233
    return true;
}
static void BossEventPost()
{
    int asked = g_LeaderOverrideAsked;
    g_LeaderOverride = nullptr; g_LeaderOverrideAsked = 0;
    static int logs = 0; static int asks = 0, events = 0; static ULONGLONG at = 0;
    ++events; asks += asked;
    ULONGLONG now = GetTickCount64();
    if (logs < 6 || (asked && logs < 20) || now - at >= 30000)
    {
        ++logs; at = now; char cn[24] = "?"; TlShort(g_BossEvWho, cn, sizeof(cn));
        CoopLog("BOSSTARGET: %s - %s by %s ran with them as the leader: its script asked for the leader %d time(s) (so far: %d asks in %d of these events)",
            g_BossEvName ? g_BossEvName : "?", g_BossEvFc == 5 ? "OnTakeDamage" : g_BossEvFc == 15 ? "OnTakeDamageOnce" : "OnIsHitDamage", cn, asked, asks, events);
    }
}
