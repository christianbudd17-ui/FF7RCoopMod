// ---- enemy attention: who enemies go after ----
// (BUG-3 fix, the author 2026-10-22: "Let's go with option 2. Leave default at the normal values, but allow for a
// change"). RE: claude/FF7R_Enemy_Targeting_RE.md. Enemies pick targets NATIVELY: each enemy AI controller keeps a hate
// score per party member (ctrl+0x1AC8, TMap<char type, int32>, 0..1000). After each of its actions the game re-scores
// (0x93ED90: the current target -200, everyone else +200 if played like player 1 / +50 if an AI teammate, + random) and
// targets the highest score (0x93FE20). With EnemiesTreatHumansLikePlayer1 the people playing already share the enemies
// (f1.91 log: P1 37 / P2 37 / P3 23 %), but enemies move on after almost every attack, whoever hits whom.
// Two optional rules change the scores (both 0 = the game's normal):
//  - [Balance] StayOnHitChance (%): when an enemy lands a hit on the player it is after, it stays on that player for its
//    next attack - that player's score goes to 1000 and everyone else's down to at most 400, so after the game's re-score
//    (800 vs at most 700) it picks the same player. A miss (dodge, perfect guard, no hit) changes nothing, so the game's
//    own rotation moves it on - "stay while hitting, look again after a miss".
//  - [Balance] TurnOnHeavyDamage (% of the enemy's max HP): a player who deals the enemy at least that much damage in a
//    few seconds (half-life 4 s), and more than 1.5x what its current target dealt, becomes its target at its next
//    re-score (score 1000, everyone else at most 400, and the game's "re-score" flag ctrl+0x1A60 set).
//  - [Balance] PreferWeakTargets + WeakTargetHealth (f1.93, the author: "make that a feature, and then give a customizable
//    % of health range"): each time an enemy picks a new target (its target handle changes), with the level's chance
//    (Low 25 / Medium 50 / High 75 / Extreme 100 %) it goes for the weakest player below WeakTargetHealth instead -
//    same score push + re-score flag as a heavy-damage turn (it turns before its next attack). Not re-rolled for the
//    switch it caused, nor while it is already after that player.
// Only the people playing (the leader + joined players 2 / 3) are ever pushed up; AI teammates are left to the game.
static void* g_AttnHuman[2] = {nullptr, nullptr};
static void* g_AttnHumanRef(int k) { return (k == 0 || k == 1) ? g_AttnHuman[k] : nullptr; }   // f1.229 (enemies/21, included earlier)
static void BossDmgNote(void* self, void* who, int dmg);   // enemies/22d (f1.213)
static int BossStayOnHit(void* self, void* tgt);    // enemies/22d (f1.214; f1.238: 0 not a boss, 1 stays, 2 already stayed on this pick)
static bool BossStayRollSkip(void* self);           // enemies/22d (f1.236): one roll per 3 s for a boss
static void BossTick();   // enemies/22d (f1.213)
static void BossRangeContact(void* self, void* tgt, int dmg, bool dodged);   // enemies/22e (f1.217, log only)
static void BossPhaseHit(void* self, int dmg);   // enemies/22f (f1.219, log only)
static ULONGLONG g_AttnLogAt = 0;
struct AttnState
{
    void* ctrl; ULONGLONG used;
    void* who[3]; float dmg[3]; ULONGLONG dmgT;   // recent damage taken from each player (decays)
    void* turnWho; ULONGLONG turnAt;               // a heavy-damage turn waiting for the enemy's re-score (no repeats)
};
static AttnState g_Attn[48];
// the people playing who can be picked: the leader + joined players 2 / 3 who aren't KO'd
static int AttnCands(void* c[3])
{
    void* leader = LeaderChar();   // (L1): the leader, not the first in the party order (player 1 D-pad-switched / KO'd)
    if (!leader) return 0;
    int n = 0; c[n++] = leader;
    for (int k = 0; k < 2; ++k)
    {
        void* h = g_AttnHuman[k];
        if (!h || h == leader) continue;
        int hp = 0, mx = 0; if (EnemyHP(h, &hp, &mx) && hp <= 0) continue;   // KO'd
        c[n++] = h;
    }
    return n;
}
static int AttnIndexOf(void* ch, void* const* c, int n) { for (int i = 0; i < n; ++i) if (c[i] == ch) return i; return -1; }
static AttnState* AttnFind(void* ctrl, bool create)
{
    AttnState* oldest = &g_Attn[0];
    for (auto& a : g_Attn) { if (a.ctrl == ctrl) return &a; if (a.used < oldest->used) oldest = &a; }
    if (!create) return nullptr;
    memset(oldest, 0, sizeof(*oldest)); oldest->ctrl = ctrl;
    return oldest;
}
static const char* AttnName(void* ch, char* buf, size_t n) { NameOf(ch, buf, n); return buf; }
static void* CtrlPawn(void* ctrl)
{
    unsigned char* k = (unsigned char*)ctrl;
    if (!k || !Readable(k + 0x758, 8)) return nullptr;
    return ResolveWeak(*(int*)(k + 0x758), *(int*)(k + 0x75C));
}
static bool IsPartyChar(void* ch) { for (int i = 0; i < 3 && GetPartyMember; ++i) if (GetPartyMember(i) == ch) return true; return false; }
// a party character's type (the hate table's key): its battle unit +0x58 (RE: 0x93EF65 / 0x94007A)
static int CharTypeOf(void* ch)
{
    unsigned char* u = (ch && Readable((unsigned char*)ch + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)((unsigned char*)ch + OFF_BATTLE_UNIT) : nullptr;
    return (u && Readable(u + 0x58, 1)) ? u[0x58] : -1;
}
// the enemy's current target character (its battle unit +0x38 = the target's handle = that character's +0xCB8)
static bool IsEnemyTarget(void* enemy, void* ch)
{
    unsigned char* u = (enemy && Readable((unsigned char*)enemy + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)((unsigned char*)enemy + OFF_BATTLE_UNIT) : nullptr;
    if (!u || !ch || !Readable(u + OFF_UNIT_TARGET, 8) || !Readable((unsigned char*)ch + 0xCB8, 8)) return false;
    void* h = *(void**)(u + OFF_UNIT_TARGET);
    return h && h == *(void**)((unsigned char*)ch + 0xCB8);
}
// the score of `type` -> 1000, everyone else's -> at most 400 (same table checks as enemies/21 BalanceFocus)
static bool HateFocus(unsigned char* ctrl, int type)
{
    if (!ctrl || type < 0 || !Readable(ctrl + 0x1AC8, 0x30)) return false;
    unsigned char* elems = *(unsigned char**)(ctrl + 0x1AC8);
    int num = *(int*)(ctrl + 0x1AD0), numBits = *(int*)(ctrl + 0x1AF0);
    unsigned int* bits = *(unsigned int**)(ctrl + 0x1AE8);
    if (!bits) bits = (unsigned int*)(ctrl + 0x1AD8);
    if (!elems || num <= 0 || num > 32 || numBits < num || !Readable(elems, num * 16) || !Readable(bits, 4)) return false;
    bool found = false;
    for (int i = 0; i < num; ++i)
        if ((bits[i >> 5] & (1u << (i & 31))) && elems[i * 16] == (unsigned char)type) found = true;
    if (!found) return false;   // that player has no score with this enemy yet - leave the table alone
    for (int i = 0; i < num; ++i)
    {
        if (!(bits[i >> 5] & (1u << (i & 31)))) continue;
        unsigned char* e = elems + i * 16;
        int* h = (int*)(e + 4);
        if (e[0] == (unsigned char)type) *h = 1000;
        else if (*h > 400) *h = 400;
    }
    return true;
}
// RESEARCH (detailed log only; the author, f1.180 test: "I couldn't get P3 to actually stagger an enemy. I bet ...
// an AI player can't trigger a stagger, only P1 can"): per hit by a person playing, the enemy's stagger gauge (unit +0xCF8
// now / +0xCFC max, +0xD18 staggered - pip_hud/36 RE, trusted only when the unit's getters are those plain reads) and how
// much it grew since that enemy's last hit; a 30 s sum per player (hits, gauge gained, staggers they finished).
static bool StaggerRead(void* ch, float* now, float* mx, bool* burst)
{
    unsigned char* u = (ch && Readable((unsigned char*)ch + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)((unsigned char*)ch + OFF_BATTLE_UNIT) : nullptr;
    if (!u || !Readable(u, 0xD20)) return false;
    void** vt = *(void***)u; if (!vt || !Readable(vt + 0x7A8 / 8, 8)) return false;
    if ((uintptr_t)vt[0x7A0 / 8] != g_Base + 0x8945A0 || (uintptr_t)vt[0x7A8 / 8] != g_Base + 0x8945B0) return false;
    *now = *(float*)(u + 0xCF8); *mx = *(float*)(u + 0xCFC); *burst = u[0xD18] != 0;
    return true;
}
struct StDiagE { void* en; float last; bool burst; ULONGLONG hitAt; };
static StDiagE g_StE[16]; static int g_StEN = 0;
static int g_StHits[4], g_StBursts[4]; static float g_StGain[4]; static ULONGLONG g_StT0 = 0;   // [3] = AI teammates
static void StaggerDiagHit(void* en, void* who, int dmg, void* const* cand, int nc)
{
    int pi = -1;   // 0 = player 1 (the leader), 1 / 2 = players 2 / 3
    if (who == LeaderChar()) pi = 0;
    else for (int k = 0; k < 2; ++k) if (who && who == g_AttnHuman[k]) pi = k + 1;
    (void)cand; (void)nc;
    if (pi < 0) { if (IsPartyChar(who)) pi = 3; else return; }   // AI teammates too (the author: "AI seemed to reset the enemy stagger bar")
    float now = 0, mx = 0; bool burst = false;
    if (!StaggerRead(en, &now, &mx, &burst)) return;
    StDiagE* e = nullptr;
    for (int i = 0; i < g_StEN; ++i) if (g_StE[i].en == en) e = &g_StE[i];
    if (!e) { e = &g_StE[g_StEN < 16 ? g_StEN++ : (int)(GetTickCount64() % 16)]; e->en = en; e->last = now; e->burst = burst; }
    float gain = (!burst && !e->burst && now > e->last) ? now - e->last : 0.f;
    bool started = burst && !e->burst;
    e->last = now; e->burst = burst;
    ULONGLONG t = GetTickCount64(); if (!g_StT0) g_StT0 = t;
    g_StHits[pi]++; g_StGain[pi] += gain; if (started) g_StBursts[pi]++;
    static LogRate logs; char a[96], b[96];
    char whoTag[24]; if (pi == 3) snprintf(whoTag, sizeof(whoTag), "AI teammate"); else snprintf(whoTag, sizeof(whoTag), "player %d", pi + 1);
    if (LogOk(logs, 60)) { CoopLog("STAGGERLOG: %s hit by %s (%s) for %d - stagger gauge %.1f / %.1f (+%.1f since its last hit)%s",
                                     AttnName(en, a, 96), AttnName(who, b, 96), whoTag, dmg, now, mx, gain, started ? " - STAGGERED now" : burst ? " (staggered)" : ""); }
    e->hitAt = GetTickCount64();
    if (t - g_StT0 >= 30000)
    {
        CoopLog("STAGGERLOG: last 30 s - player 1: %d hits, gauge +%.1f, %d stagger(s) | player 2: %d hits, +%.1f, %d | player 3: %d hits, +%.1f, %d | AI teammates: %d hits, +%.1f, %d",
                g_StHits[0], g_StGain[0], g_StBursts[0], g_StHits[1], g_StGain[1], g_StBursts[1], g_StHits[2], g_StGain[2], g_StBursts[2],
                g_StHits[3], g_StGain[3], g_StBursts[3]);
        for (int i = 0; i < 4; ++i) { g_StHits[i] = 0; g_StGain[i] = 0; g_StBursts[i] = 0; }
        g_StT0 = t;
    }
}
// a sudden fall of an enemy's gauge with no hit just before (and no stagger) - every 250 ms over the battle's enemies
static void StaggerDropTick()
{
    if (!g_DetailedLog) return;
    static ULONGLONG next = 0; ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 250;
    struct Smp { void* en; float g; bool b; };
    static Smp smp[32]; static int ns = 0;
    for (int i = 0; i < g_EnemyN && i < 32; ++i)
    {
        void* en = ResolveWeak(g_EnemyIdx[i], g_EnemySer[i]); if (!en) continue;
        float g = 0, mx = 0; bool b = false; if (!StaggerRead(en, &g, &mx, &b)) continue;
        Smp* sp = nullptr; for (int k = 0; k < ns; ++k) if (smp[k].en == en) sp = &smp[k];
        if (!sp) { if (ns >= 32) ns = 0; sp = &smp[ns++]; sp->en = en; sp->g = g; sp->b = b; continue; }
        if (!b && !sp->b && sp->g - g >= 2.f)
        {
            ULONGLONG hitAt = 0; for (int k = 0; k < g_StEN; ++k) if (g_StE[k].en == en) hitAt = g_StE[k].hitAt;
            static LogRate lr; char a[96];
            if (LogOk(lr, 20)) CoopLog("STAGGERLOG: %s's gauge fell %.1f -> %.1f / %.1f without a stagger (last logged hit %.1f s ago)",
                                       AttnName(en, a, 96), sp->g, g, mx, hitAt ? (now - hitAt) / 1000.f : -1.f);
        }
        sp->g = g; sp->b = b;
    }
}
static int g_AttnEv[6] = {0, 0, 0, 0, 0, 0};   // landed hits on the player it was after, of them made it stay, misses, heavy-damage turns, give-damage events, take-damage events
// fc 4 OnGiveDamage, 5 OnTakeDamage, 6 OnBeginAbility, 7 OnCompleteAbility - called on an AI controller (after the BP's own handler)
static void AttnEvent(int fc, void* fn, void* ctrl, unsigned char* prm)
{
    bool stDiag = g_DetailedLog && fc == 5;   // the stagger research below needs the take-damage events
    bool bossLog = g_Cfg.targetLog && fc == 4;   // f1.230 (review #1): the boss contact logs (BOSSRANGE / BOSSACT) need the give-damage events
    if (((g_Cfg.stayOnHit <= 0.f && g_Cfg.turnHeavy <= 0.f) && !stDiag && !bossLog) || fc < 4 || fc > 5) return;
    if (!prm || !ctrl || GetCurrentThreadId() != g_GameThreadId) return;
    {   // only battle AI controllers (other classes have events with the same names, e.g. battle scenes)
        static void* clsKey[64]; static bool clsOk[64]; static int clsN = 0;
        void* cls = *(void**)((unsigned char*)ctrl + 0x10); int i = 0;
        for (; i < clsN; ++i) if (clsKey[i] == cls) break;
        if (i == clsN) { bool ok = FindFunc(ctrl, "ApplyHate") != nullptr; if (clsN < 64) { clsKey[clsN] = cls; clsOk[clsN] = ok; ++clsN; } if (!ok) return; }
        else if (!clsOk[i]) return;
    }
    static int oTgt = -2, oDmg = -2, oDodge = -2, oJust = -2, oHeal = -2, tDmg = -2, tChara = -2, tHeal = -2;
    if (fc == 4 && oTgt == -2) { oTgt = ParamOff(fn, "InTarget"); oDmg = ParamOff(fn, "Damage"); oDodge = ParamOff(fn, "InDodge"); oJust = ParamOff(fn, "InJustGuard"); oHeal = ParamOff(fn, "heal");
        CoopLog("ATTENTION: OnGiveDamage params target +%d damage +%d dodge +%d justguard +%d heal +%d", oTgt, oDmg, oDodge, oJust, oHeal); }
    if (fc == 5 && tChara == -2) { tChara = ParamOff(fn, "InChara"); tDmg = ParamOff(fn, "Damage"); tHeal = ParamOff(fn, "InHeal");
        CoopLog("ATTENTION: OnTakeDamage params causer +%d damage +%d heal +%d", tChara, tDmg, tHeal); }
    void* self = CtrlPawn(ctrl);
    if (!self || IsPartyChar(self)) return;   // a party member's controller - not an enemy
    void* c[3]; int n = AttnCands(c);
    ULONGLONG now = GetTickCount64();
    char a[96], b[96];
    if (fc == 4)
    {   // the enemy dealt damage
        g_AttnEv[4]++;
        if (oTgt >= 0 && oDmg >= 0 && !(oHeal >= 0 && prm[oHeal]))   // f1.217: a boss's contacts (BOSSHIT, log only)
            BossRangeContact(self, *(void**)(prm + oTgt), *(int*)(prm + oDmg), (oDodge >= 0 && prm[oDodge]) || (oJust >= 0 && prm[oJust]));
        if (g_Cfg.stayOnHit <= 0.f || oTgt < 0 || oDmg < 0) return;
        void* tgt = *(void**)(prm + oTgt); int dmg = *(int*)(prm + oDmg);
        bool dodged = (oDodge >= 0 && prm[oDodge]) || (oJust >= 0 && prm[oJust]);
        bool heal = oHeal >= 0 && prm[oHeal];
        if (heal || AttnIndexOf(tgt, c, n) < 0) return;   // not one of the people playing
        if (dodged) { g_AttnEv[2]++; return; }               // a miss: the game's own rotation moves it on
        if (dmg <= 0 || !IsEnemyTarget(self, tgt)) return;   // only a hit on the player it is after
        g_AttnEv[0]++;
        if (BossStayRollSkip(self)) return;   // f1.236: a boss's multi-hit attack is one roll
        static unsigned seed2 = (unsigned)GetTickCount() ^ 0x5bd1e995u; seed2 = seed2 * 1103515245u + 12345u;
        float r = (float)((seed2 >> 8) % 10000) / 10000.f;
        if (r >= g_Cfg.stayOnHit) return;   // not this time - the game decides as usual
        int bst = BossStayOnHit(self, tgt);   // f1.214: a boss keeps the mod's own hate table (enemies/22d)
        if (bst == 2) return;               // f1.238: it already stayed on this pick once
        if (bst)
        {
            g_AttnEv[1]++;
            static LogRate blogs;
            if (LogOk(blogs, 20)) { CoopLog("ATTENTION: boss %s hit %s (%d) - stays on them (StayOnHitChance)", AttnName(self, a, 96), AttnName(tgt, b, 96), dmg); }
            return;
        }
        if (!HateFocus((unsigned char*)ctrl, CharTypeOf(tgt))) return;
        g_AttnEv[1]++;
        static LogRate logs;
        if (LogOk(logs, 20)) { CoopLog("ATTENTION: %s hit %s (%d) - stays on them (StayOnHitChance)", AttnName(self, a, 96), AttnName(tgt, b, 96), dmg); }
        return;
    }
    // fc == 5: the enemy took damage
    g_AttnEv[5]++;
    if (tChara < 0 || tDmg < 0) return;
    void* who = *(void**)(prm + tChara); int dmg = *(int*)(prm + tDmg);
    if (!(tHeal >= 0 && prm[tHeal])) BossPhaseHit(self, dmg);   // f1.219: every hit on a boss, 0 damage too (log only)
    if ((tHeal >= 0 && prm[tHeal]) || dmg <= 0 || !who) return;
    if (stDiag) StaggerDiagHit(self, who, dmg, c, n);
    {   // who gets the credit for damage (answers "menu commands - whose attack is it?")
        static LogRate logs;
        if (LogOk(logs, 20)) { CoopLog("DAMAGE: %s took %d from %s", AttnName(self, a, 96), dmg, AttnName(who, b, 96)); }
    }
    BossDmgNote(self, who, dmg);   // f1.214: bosses' TurnOnHeavyDamage (enemies/22d)
    if (g_Cfg.turnHeavy <= 0.f || AttnIndexOf(who, c, n) < 0) return;
    AttnState* st = AttnFind(ctrl, true);
    st->used = now;
    float decay = st->dmgT ? powf(0.5f, (float)(now - st->dmgT) / 4000.f) : 0.f;   // half-life 4 s
    for (int i = 0; i < 3; ++i) st->dmg[i] *= decay;
    st->dmgT = now;
    int slot = -1;
    for (int i = 0; i < 3; ++i) if (st->who[i] == who) slot = i;
    if (slot < 0) { slot = 0; for (int i = 1; i < 3; ++i) if (st->dmg[i] < st->dmg[slot]) slot = i; st->who[slot] = who; st->dmg[slot] = 0.f; }
    st->dmg[slot] += (float)dmg;
    if (IsEnemyTarget(self, who)) { st->turnWho = nullptr; return; }   // already after them
    if (st->turnWho == who && now - st->turnAt < 5000) return;          // already turning to them (log once, write once)
    float curDmg = 0.f;
    for (int i = 0; i < 3; ++i) if (st->who[i] && i != slot && IsEnemyTarget(self, st->who[i])) curDmg = st->dmg[i];
    int hp = 0, mx = 0;
    if (!EnemyHP(self, &hp, &mx) || mx <= 0) return;
    if (st->dmg[slot] >= g_Cfg.turnHeavy * mx && st->dmg[slot] > 1.5f * curDmg)
    {
        if (!HateFocus((unsigned char*)ctrl, CharTypeOf(who))) return;
        ((unsigned char*)ctrl)[0x1A60] = 1;   // the game's "re-score" flag: it re-picks at its next chance (between actions)
        st->turnWho = who; st->turnAt = now;
        g_AttnEv[3]++;
        static LogRate logs;
        if (LogOk(logs, 20)) { CoopLog("ATTENTION: %s turns on %s (%.0f damage lately, %.0f%% of its HP - TurnOnHeavyDamage)", AttnName(self, a, 96), AttnName(who, b, 96), st->dmg[slot], 100.f * st->dmg[slot] / mx); }
        for (int i = 0; i < 3; ++i) st->dmg[i] = 0.f;   // start counting again
    }
}
// an enemy's battle AI controller (APawn::Controller / ActiveAiController that has ApplyHate and points back at it)
static unsigned char* EnemyCtrlOf(void* enemy)
{
    static int ctrlOff = -2, aiOff = -2;
    if (ctrlOff == -2)
    {
        void* cls = *(void**)((unsigned char*)enemy + 0x10);
        ctrlOff = cls ? PropOff(cls, "Controller") : -1;
        aiOff = cls ? PropOff(cls, "ActiveAiController") : -1;
    }
    int offs[2] = {ctrlOff, aiOff};
    for (int k = 0; k < 2; ++k)
    {
        if (offs[k] <= 0) continue;
        unsigned char* ctrl = *(unsigned char**)((unsigned char*)enemy + offs[k]);
        if (!ctrl || !Readable(ctrl, 0x1B30) || !FindFunc(ctrl, "ApplyHate")) continue;
        if (CtrlPawn(ctrl) == enemy) return ctrl;
    }
    return nullptr;
}
// PreferWeakTargets - at each new target pick, maybe the weakest player instead (10 times a second in battle)
struct WeakEnemy { int idx = -1, ser = 0; void* lastH = nullptr; void* forced = nullptr; ULONGLONG forcedAt = 0; };
static WeakEnemy g_Weak[32];
static int g_WeakEv[2] = {0, 0};   // new picks while someone was weakened, turned to the weakened player
static void WeakTargetTick()
{
    if (g_Cfg.balWeakChance <= 0.f || GetCurrentThreadId() != g_GameThreadId || !InBattleNow()) return;
    static ULONGLONG scanAt = 0; ULONGLONG now = GetTickCount64();
    if (now - scanAt < 100) return;
    scanAt = now;
    void* c[3]; int n = AttnCands(c);
    void* weak = nullptr; float weakF = 2.f;   // the weakest player below the line (KO'd players are not candidates)
    for (int i = 0; i < n; ++i)
    {
        int hp = 0, mx = 0;
        if (!EnemyHP(c[i], &hp, &mx) || mx <= 0 || hp <= 0) continue;
        float f = (float)hp / (float)mx;
        if (f < g_Cfg.balWeakHealth && f < weakF) { weakF = f; weak = c[i]; }
    }
    void* en[24]; int ne = CollectEnemies(en, 24);
    for (int i = 0; i < ne; ++i)
    {
        void* e = en[i];
        unsigned char* u = (e && Readable((unsigned char*)e + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)((unsigned char*)e + OFF_BATTLE_UNIT) : nullptr;
        if (!u || !Readable(u + OFF_UNIT_TARGET, 8)) continue;
        int idx, ser; if (!MakeWeak(e, &idx, &ser)) continue;
        WeakEnemy* w = nullptr; WeakEnemy* fr = nullptr;
        for (auto& s : g_Weak) { if (s.idx == idx && s.ser == ser) { w = &s; break; } if (!fr && (s.idx < 0 || !ResolveWeak(s.idx, s.ser))) fr = &s; }
        void* h = *(void**)(u + OFF_UNIT_TARGET);
        if (!w) { if (!fr) continue; *fr = WeakEnemy(); fr->idx = idx; fr->ser = ser; fr->lastH = h; continue; }   // first sight: no "new pick" yet
        if (h == w->lastH) continue;
        w->lastH = h;   // the enemy just picked a new target
        if (w->forced && now - w->forcedAt < 5000 && IsEnemyTarget(e, w->forced)) { w->forced = nullptr; continue; }   // the switch we caused
        if (!weak || IsEnemyTarget(e, weak)) continue;
        g_WeakEv[0]++;
        static unsigned seed3 = (unsigned)GetTickCount() ^ 0x27d4eb2du; seed3 = seed3 * 1103515245u + 12345u;
        float r = (float)((seed3 >> 8) % 10000) / 10000.f;
        if (r >= g_Cfg.balWeakChance) continue;
        unsigned char* ctrl = EnemyCtrlOf(e);
        if (!ctrl || !HateFocus(ctrl, CharTypeOf(weak))) continue;
        ctrl[0x1A60] = 1;   // the game's "re-score" flag: it re-picks before its next attack
        w->forced = weak; w->forcedAt = now;
        g_WeakEv[1]++;
        static LogRate logs;
        if (LogOk(logs, 20)) { char a[96], b[96]; CoopLog("ATTENTION: %s goes after the weakened %s (%.0f%% health - PreferWeakTargets)", AttnName(e, a, 96), AttnName(weak, b, 96), weakF * 100.f); }
    }
}
static void AttentionTick(void* const* humans)
{
    g_AttnHuman[0] = humans[0]; g_AttnHuman[1] = humans[1];
    WeakTargetTick();
    StaggerDropTick();   // research
    ULONGLONG now = GetTickCount64();
    if (now - g_AttnLogAt < 30000) return;
    g_AttnLogAt = now;
    if (g_AttnEv[4] || g_AttnEv[5])
        CoopLog("ATTENTION: last 30 s - enemies landed %d hits on the player they were after (%d made them stay, StayOnHitChance %.0f%%), %d misses; %d heavy-damage turns (TurnOnHeavyDamage %.0f%%); events: give %d, take %d",
            g_AttnEv[0], g_AttnEv[1], g_Cfg.stayOnHit * 100.f, g_AttnEv[2], g_AttnEv[3], g_Cfg.turnHeavy * 100.f, g_AttnEv[4], g_AttnEv[5]);
    if (g_WeakEv[0])
        CoopLog("ATTENTION: last 30 s - %d new target picks while a player was weakened, %d turned to them (PreferWeakTargets %.0f%%, below %.0f%% health)",
            g_WeakEv[0], g_WeakEv[1], g_Cfg.balWeakChance * 100.f, g_Cfg.balWeakHealth * 100.f);
    for (int& v : g_AttnEv) v = 0;
    for (int& v : g_WeakEv) v = 0;
}
static void TargetLogTick(void* const* humans);   // (enemies/22b): enemy target changes + hate scores, log only
static void EnemyBalanceTick(void* const* humans, int nh)
{
    AttentionTick(humans);   // (hate-score rules)
    TargetLogTick(humans);   // (back from f1.91): who enemies go after, log only - [General] TargetLog
    BossTick();   // f1.213: which battle units are bosses (enemies/22d)
    PartyBalanceTick(humans);   // which party-size enemy set is active (never changes during a battle)
    BalEnemyList(humans[0] || humans[1]);   // the enemies to balance (player 1 alone: the mod's own scan)
    if (g_Cfg.balHP == 1.f && g_Cfg.balStagger == 1.f && g_Cfg.balFocusP2 == 1.f) return;
    static int ok = -1;
    if (ok < 0)
    {
        static const unsigned char want[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};
        ok = memcmp((void*)(g_Base + RVA_UNIT_SET_HP), want, sizeof(want)) == 0 ? 1 : 0;
        CoopLog("BALANCE: %s", ok ? "ready" : "DISABLED - this game version doesn't match (UnitSetHP bytes)");
    }
    if (!ok) return;
    unsigned char* hu[2] = {nullptr, nullptr};
    for (int k = 0; k < nh && k < 2; ++k) hu[k] = humans[k] ? *(unsigned char**)((unsigned char*)humans[k] + OFF_BATTLE_UNIT) : nullptr;
    // (no longer stops when only player 1 plays - HP / stagger follow the party-size set; the focus needs player 2 / 3)
    for (int i = 0; i < g_BalEN; ++i)
    {
        void* e = ResolveWeak(g_BalEIdx[i], g_BalESer[i]);
        if (!e || !Readable((unsigned char*)e + OFF_BATTLE_UNIT, 8)) continue;
        unsigned char* unit = *(unsigned char**)((unsigned char*)e + OFF_BATTLE_UNIT);
        if (!unit || !Readable(unit, 0xD20)) continue;
        BalEnemy* b = BalSlot(e);
        if (!b) continue;
        BalanceHP(e, unit, *b);
        BalanceStagger(unit, *b);
        for (int k = 0; k < 2; ++k) BalanceFocus(e, hu[k], *b, k);
    }
}
