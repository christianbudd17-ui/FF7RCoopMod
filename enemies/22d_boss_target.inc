// =================================================================================
// BOSS TARGETS - f1.213, reworked in f1.214 (the author: "I think bosses should generally be regulated the same way that
// the normal enemies are with the settings." - "Yes").
// Found (f1.212 TARGETSET log): bosses keep NO hate scores; their own script (Blueprint) calls
// EndBattleAIController::SetTarget (exec thunk 0x17BC4C0 -> vfunc +0xA18 = 0x922310 -> tail jump to UnitSetTarget 0x96BFA0;
// return address 0x17BC580) with THE LEADER after its actions. In the normal game the leader is whoever the player
// controls; in co-op it is always player 1, so the mod's hate-score rules never reached bosses.
// f1.214: each boss gets a hate table of its own, kept by the mod, run with the game's own rotation rule and the same
// enemy settings as every other enemy (claude/FF7R_Enemy_Targeting_RE.md):
//  - start values 800 / 600 / 400 by party order; at each script pick ("after each action", at most every 1.5 s): its
//    current target -200, everyone else +200 + random 0 - 100 (players 2 / 3's rise x FocusOnPlayer2), kept within 0 - 1000;
//    the highest score is the target (a tie goes to the leader, as in the game);
//  - EnemiesTreatHumansLikePlayer1 (the mod's party-size set): on = the people playing take part; off = the normal game
//    (the script's leader);
//  - StayOnHitChance: a landed hit on its target may lock it on them (score 1000, everyone else at most 400 - as HateFocus);
//  - TurnOnHeavyDamage: one person's damage of that share of its max HP within ~4 s (half-life 4 s) and 1.5x the current
//    target's -> it turns on them at its next pick; for bosses hits while it is staggered don't count and one hit counts at
//    most 1 % of its max HP (the author: limit breaks / staggered hits);
//  - PreferWeakTargets / WeakTargetHealth: at a new pick, that chance of going after the weakest player below the line.
// Bosses = enemies whose name starts with "EB" + a digit (f1.228: or "SU" + a digit - the summons in VR fights). Only the script's "target the leader" call is changed; a
// knocked-out character is never picked. [General] BossTargetsAITeammates (TESTING): AI teammates take part as if played.
// (f1.213's damage-share pick and 10 - 20 s hold - and [Balance] BossTargetSpread - are gone.)
// =================================================================================
static constexpr ULONGLONG RVA_BP_SETTARGET_RET = 0x17BC580;   // return address of the BP SetTarget exec thunk's vfunc call
static constexpr ULONGLONG BOSS_PICK_GAP_MS = 1500;            // script picks closer than this = the same action
static constexpr uintptr_t RVA_BP_BYTYPE_RET = 0x9226E4;  // f1.220 (named "target the nearest" until f1.225): return address in its script's
                                                           // SetTargetByType (0x9223A0 - player-controlled / farthest / nearest; enemies/22c)
static constexpr uintptr_t RVA_BP_SETPLAYER_RET = 0x17BC1EA;  // f1.225: SetTargetPlayer's exec thunk (0x17BC0C0, a named character) -> vfunc +0xA18
static constexpr ULONGLONG BOSS_SPECIAL_HOLD_MS = 15000;          // f1.220: after that, the leader for this long
static constexpr int BOSS_DOWN = -200, BOSS_UP = 200, BOSS_UP_RANDOM = 100;   // the game's row-0 rates (f1.91 TARGETLOG)
static constexpr int BOSS_START[3] = {800, 600, 400};
static constexpr float BOSS_HEAVY_HALFLIFE_S = 4.f;
static constexpr float BOSS_HIT_CAP_FRACTION = 0.01f;   // one hit counts for at most 1 % of its max HP
struct BossState
{
    void* unit = nullptr; void* enemy = nullptr; bool boss = false; ULONGLONG seen = 0;
    char name[96] = "?";   // f1.230 (review #2): its name, read while the object is surely alive (battle-end lines use this)
    void* who[3] = {nullptr, nullptr, nullptr};
    int score[3] = {0, 0, 0};
    ULONGLONG lastOn[3] = {0, 0, 0};   // f1.222: when each was last its pick (0 = never) - a tie goes to the one left alone longest
    float heavy[3] = {0, 0, 0}; ULONGLONG dmgT = 0;
    void* pick = nullptr; ULONGLONG pickAt = 0;
    void* turnTo = nullptr;
    ULONGLONG ownPickAt = 0;   // f1.220: its script last picked a target itself ("target the nearest", 0x9226E4)
    int leaderCalls = 0;       // f1.224: its script's "target the leader" calls this battle (0 = a "nearest" boss, e.g. Eligor)
    ULONGLONG firstSeen = 0;   // f1.224: when it was first seen this battle
    ULONGLONG askAt = 0;       // f1.236: its script's last target command (SetTarget / SetTargetByType / SetTargetPlayer)
    ULONGLONG stayRollAt = 0;  // f1.236: its last StayOnHitChance roll (one per 3 s - a multi-hit attack is one roll)
    ULONGLONG lockLogAt = 0;   // f1.238: last "a switch is due - waiting for its target lock" line
    void* stayedOn = nullptr;  // f1.238: StayOnHitChance already kept it on this pick once
    bool afterPhase = false;   // f1.240: its phase guard just let go (enemies/22f BpEval) - the next pick is a random one
    bool nearestLogged = false;
    // f1.261 MINIONS (the author, Abzu & Shoats 2026-11-01: "Go ahead and build for the shoats"): a normal enemy (not EB / SU)
    // whose AI controller keeps NO hate scores - its script picks its targets with the same commands as a boss (the Shoats,
    // EN2013 ApsBaby: SetTargetByType "player-controlled" = the leader). The mod's hate rules can't reach it, so it got the
    // boss rotation nowhere: Shoats P1 86 % / P2 3 % / AI 1 % (log 12:02 - 12:10). Now it gets the boss rotation for those
    // script calls, the quiet-script switch, StayOnHit / TurnOnHeavyDamage - not the boss HP handling, phase guard or
    // BOSS... research logs.
    bool minion = false, wasMinion = false;
    ULONGLONG emptySince = 0;   // its hate table has been empty since (0 = has scores / no table read)
    TlEnemy tl;                 // enemies/22b: its controller (cached)
    // f1.225 command catalogue (BOSSCMD at the battle's end): which target commands its script used
    int cLeader = 0, cByType[4] = {0, 0, 0, 0}, cPlayer = 0, cChanged = 0;   // cByType: player-controlled / farthest / nearest / unknown
    unsigned char unknownOp = 0; bool unknownLogged = false;
    struct { uintptr_t rva; int n; } cOther[4] = {}, cDirect[4] = {};
    int cOtherMore = 0, cDirectMore = 0;
};
static BossState g_Boss[32];   // f1.284 (code review #51): was 12 - every enemy takes a slot since f1.261 (minions), dead ones keep it 30 s;
                               // more than 12 enemies in a fight (adds, waves) left a later boss without its rotation / phase guard
static BossState* BossOfUnit(void* unit)
{
    for (auto& b : g_Boss) if (b.unit && b.unit == unit) return &b;
    return nullptr;
}
static BossState* BossOfEnemy(void* enemy)
{
    unsigned char* u = (enemy && Readable((unsigned char*)enemy + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)((unsigned char*)enemy + OFF_BATTLE_UNIT) : nullptr;
    BossState* b = u ? BossOfUnit(u) : nullptr;
    return (b && b->boss) ? b : nullptr;
}
// f1.261: a boss OR a minion (enemies/22 StayOnHit / TurnOnHeavyDamage - the rotation's inputs)
static BossState* BossRotOfEnemy(void* enemy)
{
    unsigned char* u = (enemy && Readable((unsigned char*)enemy + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)((unsigned char*)enemy + OFF_BATTLE_UNIT) : nullptr;
    BossState* b = u ? BossOfUnit(u) : nullptr;
    return (b && (b->boss || b->minion)) ? b : nullptr;
}
// f1.261: how many party members have a hate score in this enemy's table (-1 = no controller / table not readable)
static int HateEntries(void* enemy, TlEnemy& t)
{
    unsigned char* ctrl = TlCtrl(enemy, t);
    if (!ctrl || !Readable(ctrl + 0x1AC8, 0x30)) return -1;
    unsigned char* elems = *(unsigned char**)(ctrl + 0x1AC8);
    int num = *(int*)(ctrl + 0x1AD0), numBits = *(int*)(ctrl + 0x1AF0);
    unsigned int* bits = *(unsigned int**)(ctrl + 0x1AE8);
    if (!bits) bits = (unsigned int*)(ctrl + 0x1AD8);
    if (num == 0) return 0;
    if (!elems || num < 0 || num > 32 || numBits < num || !Readable(bits, 4)) return -1;
    int k = 0;
    for (int i = 0; i < num; ++i) if (bits[i >> 5] & (1u << (i & 31))) ++k;
    return k;
}
static constexpr ULONGLONG MINION_EMPTY_MS = 2000;   // f1.261: an empty hate table for this long = a script-run enemy
// f1.245 (seats/13 LEADERSCRIPT log): the name of a boss in this battle, or nullptr
static const char* BossFirstName()
{
    for (auto& b : g_Boss) if (b.boss && b.unit) return b.name;
    return nullptr;
}
// f1.250 (seats/24b): is a boss whose object name starts with idPrefix (e.g. "EB0010_" = the Failed Experiment) in this
// battle? (names stored when first seen; the table is cleared when the battle ends)
static bool BossFightIs(const char* idPrefix)
{
    size_t L = strlen(idPrefix);
    for (auto& b : g_Boss) if (b.boss && b.unit && !strncmp(b.name, idPrefix, L)) return true;
    return false;
}
// f1.227 (enemies/21 EnemyHP for bosses): is this battle unit / HP status a boss's? (g_Boss is filled 4 times a second)
static bool BossUnitObj(void* unit)
{
    if (!unit) return false;
    for (auto& b : g_Boss) if (b.boss && b.unit == unit) return true;
    return false;
}
static bool BossStatusObj(void* status)
{
    if (!status) return false;
    for (auto& b : g_Boss)
    {
        if (!b.boss || !b.unit) continue;
        unsigned char* u = (unsigned char*)b.unit;
        if (Readable(u + 0x5E8, 8) && *(void**)(u + 0x5E8) == status) return true;
    }
    return false;
}
static const char* BossNameOf(void* unit)   // f1.230: the stored name (no object read)
{
    BossState* b = BossOfUnit(unit);
    return b ? b->name : "?";
}
static bool BossAnyBoss()   // f1.230 (review #6): a cheap "no boss in this battle" exit for the log hooks
{
    for (auto& b : g_Boss) if (b.boss && b.unit) return true;
    return false;
}
static bool BossIsName(void* enemy)
{
    char nm[96] = ""; NameOf(enemy, nm, sizeof(nm));
    return BossNameText(nm);   // f1.228: "EB" + digit, or "SU" + digit (a summon fought in VR) - enemies/21
}
// the people playing (+ AI teammates when testing) who can be picked now: alive and in the party
static int BossCands(void* c[3])
{
    int n = 0;
    auto add = [&](void* ch) {
        if (!ch || n >= 3) return;
        for (int i = 0; i < n; ++i) if (c[i] == ch) return;
        int hp = 0, mx = 0; if (!EnemyHP(ch, &hp, &mx) || hp <= 0) return;
        c[n++] = ch;
    };
    add(LeaderChar());
    add(g_AttnHuman[0]); add(g_AttnHuman[1]);
    if (g_Cfg.bossTestAI) for (int i = 0; i < 3 && GetPartyMember; ++i) add(GetPartyMember(i));
    return n;
}
// f1.218: how many people take part (standing or not) - one = the normal game. (f1.214 decided that by the STANDING ones:
// with player 1 and player 2 knocked out the boss got the leader - player 1, down - for 86 s; f1.217 log 07:19:02.)
static int BossPeople()
{
    void* p[5]; int n = 0;
    auto add = [&](void* ch) { if (!ch || n >= 5) return; for (int i = 0; i < n; ++i) if (p[i] == ch) return; p[n++] = ch; };
    add(LeaderChar()); add(g_AttnHuman[0]); add(g_AttnHuman[1]);
    if (g_Cfg.bossTestAI) for (int i = 0; i < 3 && GetPartyMember; ++i) add(GetPartyMember(i));
    return n;
}
// this character's slot in the boss's table (a new one starts at its party-order start value)
static int BossSlot(BossState& b, void* who)
{
    for (int i = 0; i < 3; ++i) if (b.who[i] == who) return i;
    int s = -1; for (int i = 0; i < 3 && s < 0; ++i) if (!b.who[i]) s = i;
    if (s < 0)
    {   // f1.273 (code review #53): full (3 characters) - replace one who is no longer in the party (a story swap mid-battle),
        // not always slot 0 (the newcomer was overwritten by the next one and never picked; the one who left kept a slot)
        for (int i = 0; i < 3 && s < 0; ++i)
        {
            bool inParty = false;
            for (int k = 0; k < 3 && GetPartyMember; ++k) if (GetPartyMember(k) == b.who[i]) inParty = true;
            if (!inParty) s = i;
        }
        if (s < 0) s = 0;
    }
    int order = 2;
    for (int i = 0; i < 3 && GetPartyMember; ++i) if (GetPartyMember(i) == who) order = i;
    b.who[s] = who; b.score[s] = BOSS_START[order]; b.heavy[s] = 0.f; b.lastOn[s] = 0;
    return s;
}
static void BossFocus(BossState& b, void* who)   // as HateFocus: who -> 1000, everyone else at most 400
{
    int s = BossSlot(b, who);
    for (int i = 0; i < 3; ++i) if (b.who[i]) b.score[i] = i == s ? 1000 : (b.score[i] > 400 ? 400 : b.score[i]);
}
// 4 times a second (enemies/22 EnemyBalanceTick): which battle units belong to bosses
static void BossIdleTick(ULONGLONG now);   // below (f1.236)
static void BossRangeTick(ULONGLONG now, double dt);   // enemies/22e (f1.217, log only)
static void BossRangeBattleEnd();
static void BossPhaseTick(ULONGLONG now);   // enemies/22f (f1.219, log only)
static void BossPhaseBattleEnd();
static bool BossLocked(void* unit);   // enemies/22f (f1.220): its HP is locked - a phase change
static void BossCatEnd();   // f1.225 (below): each boss's command catalogue
static void BossActTick(ULONGLONG now);   // enemies/22g (f1.226, log only)
static void BossActBattleEnd();
static void BossTick()
{
    static ULONGLONG at = 0; ULONGLONG now = GetTickCount64();
    if (now - at < 250) return;
    double dt = at ? (now - at) / 1000.0 : 0.25; if (dt > 0.5) dt = 0.5;
    at = now;
    if (!InBattleNow()) { BossRangeBattleEnd(); BossPhaseBattleEnd(); BossActBattleEnd(); BossCatEnd(); for (auto& b : g_Boss) b = BossState(); return; }
    void* en[24]; int ne = CollectEnemies(en, 24);
    for (int i = 0; i < ne; ++i)
    {
        void* e = en[i];
        unsigned char* u = (e && Readable((unsigned char*)e + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)((unsigned char*)e + OFF_BATTLE_UNIT) : nullptr;
        if (!u) continue;
        BossState* b = BossOfUnit(u);
        // (f1.337 pre-release review, NOT changed for the release: an entry kept 30 s for a gone enemy whose battle unit address a new
        //  enemy gets carries its flags over - after release. The crash part - log lines reading the stale enemy pointer's name - is
        //  fixed: they use the stored name)
        if (!b)
        {
            BossState* fr = nullptr;
            for (auto& x : g_Boss) if (!x.unit || now - x.seen > 30000) { fr = &x; break; }
            if (!fr) continue;
            *fr = BossState(); fr->unit = u; fr->enemy = e; fr->boss = BossIsName(e); fr->firstSeen = now; b = fr;
            NameOf(e, b->name, sizeof(b->name));   // f1.337: every entry (was bosses only) - the log lines use it, not the pointer
            if (b->boss) { const char* nm = b->name; CoopLog("BOSSTARGET: %s is a boss - its \"target the leader\" calls get a hate table run with the enemy settings%s", nm,
                g_Cfg.humanAggro ? "" : " (EnemiesTreatHumansLikePlayer1 off: left to the game - the leader)"); }
        }
        b->seen = now; b->enemy = e;
        if (!b->boss)
        {   // f1.261: a minion = a normal enemy whose hate table stays empty (its script picks its targets)
            int he = HateEntries(e, b->tl);
            if (he == 0) { if (!b->emptySince) b->emptySince = now; }
            else b->emptySince = 0;
            bool m = b->emptySince && now - b->emptySince >= MINION_EMPTY_MS;
            if (m && !b->minion)
            {
                if (!b->wasMinion) NameOf(e, b->name, sizeof(b->name));
                static LogRate rate;
                if (LogOk(rate, 30)) CoopLog("BOSSTARGET: %s keeps no hate scores - a minion: its script's \"target the leader\" / \"player-controlled\" picks get the boss rotation (f1.261)%s", b->name,
                    g_Cfg.humanAggro ? "" : " (EnemiesTreatHumansLikePlayer1 off: left to the game)");
                b->wasMinion = true;
            }
            else if (!m && b->minion && he > 0)
            {
                static LogRate rate;
                if (LogOk(rate, 10)) CoopLog("BOSSTARGET: %s has hate scores now (%d) - back to the normal enemy rules", b->name, he);
            }
            b->minion = m;
        }
    }
    BossRangeTick(now, dt);
    BossPhaseTick(now);
    BossActTick(now);   // f1.226
    BossIdleTick(now);  // f1.236
}
// enemies/22 (the boss took damage): TurnOnHeavyDamage
static void BossDmgNote(void* self, void* who, int dmg)
{
    if (g_Cfg.turnHeavy <= 0.f || !self || !who || dmg <= 0) return;
    BossState* b = BossRotOfEnemy(self);   // f1.261: minions too
    if (!b) return;
    void* c[3]; int n = BossCands(c);
    bool cand = false; for (int i = 0; i < n; ++i) if (c[i] == who) cand = true;
    if (!cand) return;
    { float sn, sm; bool burst = false; if (StaggerRead(self, &sn, &sm, &burst) && burst) return; }   // staggered: doesn't count
    int hp = 0, mx = 0; if (!EnemyHP(self, &hp, &mx) || mx <= 0) return;
    ULONGLONG now = GetTickCount64();
    if (b->dmgT && now > b->dmgT) { float k = powf(0.5f, (now - b->dmgT) / 1000.f / BOSS_HEAVY_HALFLIFE_S); for (float& v : b->heavy) v *= k; }
    b->dmgT = now;
    float d = (float)dmg; if (d > BOSS_HIT_CAP_FRACTION * mx) d = BOSS_HIT_CAP_FRACTION * mx;   // a limit break / big special = one strong hit
    int s = BossSlot(*b, who);
    b->heavy[s] += d;
    if (who == b->pick) return;   // already after them
    float cur = 0.f; for (int i = 0; i < 3; ++i) if (b->who[i] && b->who[i] == b->pick) cur = b->heavy[i];
    if (b->heavy[s] >= g_Cfg.turnHeavy * mx && b->heavy[s] > 1.5f * cur && b->turnTo != who)
    {
        b->turnTo = who;
        char a[96], w[96]; NameOf(self, a, sizeof(a)); NameOf(who, w, sizeof(w));
        static LogRate rate;
        if (LogOk(rate, 20)) CoopLog("BOSSTARGET: %s - heavy damage by %s (%.0f lately, %.1f%% of its HP - TurnOnHeavyDamage) - turns on them at its next pick", a, w, b->heavy[s], 100.f * b->heavy[s] / mx);
        for (float& v : b->heavy) v = 0.f;
    }
}
// enemies/22 (the boss landed a hit on its target and the StayOnHitChance roll said stay): true = it was a boss
// f1.238: returns 0 = not a boss, 1 = it stays on them (focus), 2 = a boss that already stayed on this pick once (no focus).
// One stay per pick: the Guard Scorpion's script picks a target only every minute or two, so the switch between its moves
// (BossIdleTick, every 10 s) is its rotation - and each landed hit put its target back to 1000 / everyone else at most 400
// (f1.236 phase 2: hit on Cloud every few seconds, 7 "stays on them" in 90 s, Cloud 124.5 s straight). A stay now keeps
// its target for one more rotation, not for as long as it keeps hitting them.
static int BossStayOnHit(void* self, void* tgt)
{
    BossState* b = BossRotOfEnemy(self);   // f1.261: minions too
    if (!b) return 0;
    if (tgt != b->pick) return 1;
    if (b->stayedOn == tgt) return 2;
    b->stayedOn = tgt;
    BossFocus(*b, tgt);
    return 1;
}
static void* BossRotate(BossState* b, void* ch, const char* insteadOf);   // below (f1.236)
static unsigned BossRand() { static unsigned s = (unsigned)GetTickCount() ^ 0x9E3779B9u; s = s * 1103515245u + 12345u; return s >> 8; }
// enemies/22c Hook_UnitSetTarget: the character to pass on (= ch unless a boss's script asks for the leader)
static void* BossTargetPick(void* unit, void* ch, uintptr_t outer)
{
    if ((outer == RVA_BP_SETTARGET_RET || outer == RVA_BP_BYTYPE_RET || outer == RVA_BP_SETPLAYER_RET) && GetCurrentThreadId() == g_GameThreadId)
    {   // f1.236: its script picked a target just now (BossIdleTick waits for a quiet script)
        BossState* q = BossOfUnit(unit);
        if (q) q->askAt = GetTickCount64();
    }
    bool nearestRoutine = false;   // f1.224: a "nearest" boss's normal pick, rotated (BossSpreadNearest - since f1.225 only when the mode is unknown)
    bool byTypeLeader = false;     // f1.225: SetTargetByType "the player-controlled character" - the leader, rotated like SetTarget
    if (outer == RVA_BP_BYTYPE_RET && GetCurrentThreadId() == g_GameThreadId)
    {   // f1.220: its script picks a target by type itself - noted, left alone (a special move; 15 s hold below)
        // f1.225 (the author: "Can you find every boss and see if they all have their own attack codes?" - "Yes, build it"):
        // 0x9226E4 is SetTargetByType - all three of its modes. The mode is read from its script (enemies/22c Hook_ByType):
        //  - player-controlled (UCPC = the leader: S13 Eligor - Cloud 96 % of its time, every pick onto whoever player 1
        //    played): the same as "target the leader" - rotated, no hold;
        //  - farthest / nearest: a deliberate choice by distance - left to the game, with f1.220's 15 s hold;
        //  - unknown (not a fixed value in its script): left to the game and held - or, with [General] BossSpreadNearest=1
        //    (f1.224, TESTING), rotated when the boss has made no "target the leader" call in its first 20 s.
        BossState* bs = BossOfUnit(unit);
        if (!bs || !(bs->boss || bs->minion)) return ch;   // f1.261: minions too
        ULONGLONG nowN = GetTickCount64();
        int mode = g_ByTypeMode;
        if (mode == 0) byTypeLeader = true;
        else if (mode < 0)
        {
            if (!bs->unknownLogged) { bs->unknownLogged = true; char a[96]; strcpy_s(a, sizeof(a), bs->name); CoopLog("BOSSTARGET: %s - SetTargetByType with a mode its script doesn't give as a fixed value (bytecode 0x%02X) - left to the game%s", a, g_ByTypeOp,
                g_Cfg.bossNearest ? " unless it never targets the leader (BossSpreadNearest)" : ""); }
            // f1.224 (the author, S13 Eligor: "Make it an optional setting to test"): a boss whose script has made NO "target the
            // leader" call in its first 20 s uses this as its normal routine - with BossSpreadNearest=1 it gets the rotation too
            if (!g_Cfg.bossNearest || !g_Cfg.humanAggro || bs->leaderCalls > 0 || nowN - bs->firstSeen < 20000 || !ch) { bs->ownPickAt = nowN; return ch; }
            if (!bs->nearestLogged) { bs->nearestLogged = true; char a[96]; strcpy_s(a, sizeof(a), bs->name); CoopLog("BOSSTARGET: %s picks by type as its normal routine (no \"target the leader\" call) - rotated like the others (BossSpreadNearest)", a); }
            nearestRoutine = true;
        }
        else { bs->ownPickAt = nowN; return ch; }   // farthest / nearest
    }
    if (!g_Cfg.humanAggro || (outer != RVA_BP_SETTARGET_RET && !nearestRoutine && !byTypeLeader) || !ch || GetCurrentThreadId() != g_GameThreadId) return ch;
    BossState* b = BossOfUnit(unit);
    if (!nearestRoutine)
    {
        if (b && (b->boss || b->minion)) b->leaderCalls++;   // f1.224 (f1.225: SetTargetByType "player-controlled" counts too; f1.261 minions)
        if (ch != LeaderChar()) return ch;   // the script picked someone else itself - leave it
    }
    if (!b || !(b->boss || b->minion)) return ch;
    if (BossPeople() < 2) return ch;   // one person (and no testing): the normal game
    {   // f1.220 (the author: "I'm inclined to think that our script IS interrupting the boss's script. Perhaps we can change the
        // targeting in a more passive way, that wouldn't interrupt its normal behavior?"): whenever the boss is doing anything
        // out of the ordinary its script's own choice - the leader - goes through, as in the normal game:
        //  - its HP on the game's HP floor (a phase change - f1.218 / f1.219: the mod changed the Ghoul's target right after
        //    and the phase change never came), - a cutscene, - the game's camera on a scripted shot,
        //  - it is out of the enemy list (vanished / untargetable), - its script picked a target itself ("target the
        //    nearest") in the last 15 s (a special move in progress).
        ULONGLONG nowG = GetTickCount64();
        const char* why = BossLocked(unit) ? "its HP is on the game's floor (phase change)" : g_InCutscene ? "a cutscene" : g_ScriptedShot ? "a scripted camera shot"
            : nowG - b->seen > 1000 ? "it is out of the enemy list" : (b->ownPickAt && nowG - b->ownPickAt < BOSS_SPECIAL_HOLD_MS) ? "its own special move (it picked by distance / by type)" : nullptr;
        int lhp = 0, lmx = 0;
        if (why && EnemyHP(ch, &lhp, &lmx) && lhp > 0)
        {
            b->pick = ch; b->pickAt = nowG;
            static LogRate rate;
            if (LogOk(rate, 30)) { char a[96]; strcpy_s(a, sizeof(a), b->name); CoopLog("BOSSTARGET: %s -> the leader (its own choice) - %s", a, why); }
            return ch;
        }
    }
    const char* insteadOf = nearestRoutine ? "its by-type pick" : byTypeLeader ? "the leader (by type: player-controlled)" : "the leader";
    return BossRotate(b, ch, insteadOf);
}
// f1.236: the game's rotation over the people playing (its pick down, everyone else up; heavy damage / weak players), for
// a script's "target the leader" (BossTargetPick) and for a quiet script between its moves (BossIdleTick). ch = what
// goes on when the mod has no pick; insteadOf = for the log line.
static void* BossRotate(BossState* b, void* ch, const char* insteadOf)
{
    void* c[3]; int n = BossCands(c);
    if (n == 0) return ch;   // nobody standing (ch: what goes on when the mod has no pick)
    // f1.218: one standing person is still picked (below) - the leader may be the one knocked out (KOLEAD keeps player 1
    // the leader; in the normal game the game would have switched the leader)
    ULONGLONG now = GetTickCount64();
    auto isCand = [&](void* x) { for (int i = 0; i < n; ++i) if (c[i] == x) return true; return false; };
    if (b->afterPhase)
    {   // f1.240 (the author, boss rush: most fights about 55 / 45 for player 1 - during a phase change the game points the
        // boss at the leader, and it stayed there until the next rotation; "52/48 is a good split ... more unpredictability
        // is better"): the first pick after the phase guard lets go is a random one among the people standing; the
        // rotation goes on from it (BossFocus, as a StayOnHit / heavy-damage turn does).
        b->afterPhase = false;
        for (int i = 0; i < n; ++i) BossSlot(*b, c[i]);
        void* pick = c[BossRand() % n];
        BossFocus(*b, pick); b->turnTo = nullptr;
        char p[24] = "?"; TlShort(pick, p, sizeof(p));
        CoopLog("BOSSTARGET: %s -> %s (%s) - random pick after its phase change (1 of %d standing; was %s)", b->name, p, kTlWho[TlWho(pick, g_AttnHuman)], n, insteadOf);
        if (pick != b->pick) b->stayedOn = nullptr;
        b->pick = pick; b->pickAt = now;
        for (int k = 0; k < 3; ++k) if (b->who[k] == pick) b->lastOn[k] = now;
        return pick;
    }
    if (b->pick && isCand(b->pick) && now - b->pickAt < BOSS_PICK_GAP_MS) return b->pick;   // the same action
    for (int i = 0; i < n; ++i) BossSlot(*b, c[i]);
    const char* why = "rotation";
    if (b->turnTo && isCand(b->turnTo)) { BossFocus(*b, b->turnTo); why = "heavy damage (TurnOnHeavyDamage)"; }
    else
    {   // the game's rotation, after each action: its target down, everyone else up (players 2 / 3 x FocusOnPlayer2)
        for (int k = 0; k < 3; ++k)
        {
            if (!b->who[k] || !isCand(b->who[k])) continue;
            int d;
            if (b->who[k] == b->pick) d = BOSS_DOWN;
            else
            {
                d = BOSS_UP + (int)(BossRand() % (BOSS_UP_RANDOM + 1));
                if (b->who[k] != LeaderChar() && (b->who[k] == g_AttnHuman[0] || b->who[k] == g_AttnHuman[1])) d = (int)(d * g_Cfg.balFocusP2);
            }
            int v = b->score[k] + d; b->score[k] = v < 0 ? 0 : v > 1000 ? 1000 : v;
        }
    }
    b->turnTo = nullptr;
    int best = -1;
    for (int k = 0; k < 3; ++k)
    {
        if (!b->who[k] || !isCand(b->who[k])) continue;
        if (best < 0 || b->score[k] > b->score[best]) best = k;
        else if (b->score[k] == b->score[best] && b->lastOn[k] < b->lastOn[best]) best = k;
        // f1.222 (the author, f1.221 Ghoul: "Cloud was attacking most of the time"): a tie goes to the one it left alone the
        // longest - was to the leader (the game's rule for normal enemies). With the 1000 cap nearly every pick is a tie, so the
        // leader got every other pick (f1.221 log: Cloud 20 of 40, the pattern Cloud / Tifa / Cloud / Aerith ...).
    }
    if (best < 0) return ch;
    void* pick = b->who[best];
    if (g_Cfg.balWeakChance > 0.f && pick != b->pick)
    {   // PreferWeakTargets: at a new pick, maybe the weakest player below the line instead
        void* weak = nullptr; float wf = 2.f;
        for (int i = 0; i < n; ++i) { int hp = 0, mx = 0; if (EnemyHP(c[i], &hp, &mx) && mx > 0 && hp > 0) { float f = (float)hp / mx; if (f < g_Cfg.balWeakHealth && f < wf) { wf = f; weak = c[i]; } } }
        if (weak && weak != pick && (float)(BossRand() % 10000) / 10000.f < g_Cfg.balWeakChance) { pick = weak; BossFocus(*b, weak); why = "weakened player (PreferWeakTargets)"; }
    }
    if (pick != b->pick)
    {
        char a[96], p[24] = "?", sc[160] = "";
        strcpy_s(a, sizeof(a), b->name); TlShort(pick, p, sizeof(p));   // f1.337: the stored name (b->enemy can be stale until the next BossTick)
        size_t len = 0;
        for (int k = 0; k < 3 && len < sizeof(sc); ++k)
        {
            if (!b->who[k] || !isCand(b->who[k])) continue;
            char cn[24] = "?"; TlShort(b->who[k], cn, sizeof(cn));
            len += snprintf(sc + len, sizeof(sc) - len, "%s%s %d", len ? ", " : "", cn, b->score[k]);
        }
        static LogRate rate;
        int lhp = 0, lmx = 0; bool leaderDown = EnemyHP(ch, &lhp, &lmx) && lhp <= 0;
        if (LogOk(rate, 40)) CoopLog("BOSSTARGET: %s -> %s (%s) instead of %s%s - %s | scores: %s", a, p, kTlWho[TlWho(pick, g_AttnHuman)], insteadOf, leaderDown ? " (knocked out)" : "", why, sc);
    }
    if (pick != b->pick) b->stayedOn = nullptr;   // f1.238: a new pick can be stayed on once again
    b->pick = pick; b->pickAt = now;
    for (int k = 0; k < 3; ++k) if (b->who[k] == pick) b->lastOn[k] = now;
    return pick;
}
// f1.236 (the author, boss rush on f1.235, Guard Scorpion: "only seems to be targeting Cloud"): its script picks a target
// only now and then - 6 times in 3.5 min (BOSSIDLE "hasn't asked for a target for 211 s"), each pick went through the
// rotation, but between them it kept whatever it had: Cloud 198.8 s straight (23:08:53 - 23:12:12), Barret 7.7 s. In the
// normal game the game re-scores normal enemies after each action. Now a boss whose script targets the leader, when its
// script has made no target pick for BOSS_IDLE_RETARGET_MS, gets the same rotation between its moves (no ability holding
// its target - the setter's own lock, +0x310), at most once per BOSS_IDLE_RETARGET_MS. Not during its phase change (HP
// floor / line), a cutscene, a scripted camera shot, its own special pick (by distance / a named character) or with one
// person playing. The plain game setter is used (not through the mod's hook).
static constexpr ULONGLONG BOSS_IDLE_RETARGET_MS = 10000;
static void BossIdleTick(ULONGLONG now)
{
    if (!g_Cfg.humanAggro || !Orig_UnitSetTarget || GetCurrentThreadId() != g_GameThreadId || BossPeople() < 2 || g_InCutscene || g_ScriptedShot) return;
    for (auto& b : g_Boss)
    {
        // f1.263 (the author, Valkyrie on f1.261 - P1 64 %, the first 40.6 s on the leader: "Yes, add the tweak"): a boss whose
        // script hasn't asked for a target yet counts as quiet from when it was first seen - the switch between its moves
        // starts 10 s into the fight (was: only after its script's first ask; the game puts every boss on the leader at the
        // start). Bosses only (not minions).
        bool neverAsked = b.boss && !b.askAt && b.firstSeen;
        if (!(b.boss || b.minion) || !b.unit || !b.enemy || now - b.seen > 1000 || (b.leaderCalls <= 0 && !neverAsked)) continue;   // f1.261: minions too
        ULONGLONG quietFrom = b.askAt ? b.askAt : (neverAsked ? b.firstSeen : 0);
        if (!b.afterPhase && (!quietFrom || now - quietFrom < BOSS_IDLE_RETARGET_MS || now - b.pickAt < BOSS_IDLE_RETARGET_MS)) continue;   // f1.240: right after a phase - no quiet wait
        if (b.ownPickAt && now - b.ownPickAt < BOSS_SPECIAL_HOLD_MS) continue;
        if (BossLocked(b.unit)) continue;
        unsigned char* u = (unsigned char*)b.unit;
        if (!Readable(u + OFF_UNIT_TARGETLOCK, 4)) continue;
        // +0x310 is a bit mask of target locks - LockTargetCharaBase(type, lock) (exec 0x17BBED0: [unit+0x310] |= / &= ~(1 <<
        // type)), EEndBattleTargetCharaBaseLockType Ability 0 / Combo 1 / AI 2; UnitSetTarget does nothing while one is set.
        // f1.238 (the author, f1.237 run: "it looked okay. Let's do the cleanup"): f1.237's OnChangeLeader call for a locked
        // target is gone - 7 tries, all during an ability (0x1), its script kept its target every time; every switch came
        // from the setter. A due switch now waits for the lock to clear (checked 4 times a second) and happens then; one
        // "waiting" line per 10 s names the lock bits.
        int lk = *(int*)(u + OFF_UNIT_TARGETLOCK);
        if (lk != 0)
        {
            if (now - b.lockLogAt >= 10000)
            {
                b.lockLogAt = now;
                static LogRate rate;
                if (LogOk(rate, 40)) CoopLog("BOSSTARGET: %s - a switch is due (its script quiet for %.0f s) - waiting for its target lock to clear (0x%X:%s%s%s)", b.name, (now - quietFrom) / 1000.0, lk,
                    (lk & 1) ? " ability" : "", (lk & 2) ? " combo" : "", (lk & 4) ? " AI" : "");
            }
            continue;
        }
        void* cur = TsCharOfHandle(Readable(u + 0x38, 8) ? *(void**)(u + 0x38) : nullptr);
        if (!cur) continue;
        if (b.minion && !IsPartyChar(cur)) continue;   // f1.261: a minion after a non-party target (Shoats go back to Abzu) - leave it
        b.pick = cur;   // its target now is the one the rotation turns down
        char why[80]; if (b.askAt) snprintf(why, sizeof(why), "its current target (its script quiet for %.0f s)", (now - b.askAt) / 1000.0);
        else snprintf(why, sizeof(why), "its current target (its script hasn't picked yet, %.0f s in)", (now - b.firstSeen) / 1000.0);
        void* pick = BossRotate(&b, cur, why);
        b.pickAt = now;   // (BossRotate set it; also when it kept the same one - the next try in BOSS_IDLE_RETARGET_MS)
        if (pick && pick != cur)
        {
            Orig_UnitSetTarget(b.unit, pick);
            ++b.cChanged;
        }
    }
}
// enemies/22 (a boss's hit landed on its target, before the StayOnHitChance roll): true = skip this roll - a boss gets one
// roll per BOSS_STAY_ROLL_MS (f1.236: the Guard Scorpion's machine gun / missiles land 4 - 5 hits each, so nearly every
// attack "stayed" and the focus kept its target at 1000)
static constexpr ULONGLONG BOSS_STAY_ROLL_MS = 3000;
static bool BossStayRollSkip(void* self)
{
    BossState* b = BossRotOfEnemy(self);   // f1.261: minions too
    if (!b) return false;
    ULONGLONG now = GetTickCount64();
    if (b->stayRollAt && now - b->stayRollAt < BOSS_STAY_ROLL_MS) return true;
    b->stayRollAt = now;
    return false;
}
// ---- f1.225 COMMAND CATALOGUE (the author: "Can you find every boss and see if they all have their own attack codes?").
// Bosses share the same target commands (AEndBattleAIController); what differs is which ones each boss's script uses. Per
// boss, every target write on it this battle, by command - one BOSSCMD line at the battle's end, so a catalogue builds up
// as the bosses are played (claude/FF7R_Boss_Target_Commands.md):
//  - SetTarget (0x17BC580 - the script passes "the leader"), - SetTargetByType by mode, - SetTargetPlayer (a named
//    character), - any other code calling UnitSetTarget on it (e.g. 0x93F722, the hate update), - direct writes (e.g. 0x9BB09D).
static const char* ByTypeName(int m) { return m == 0 ? "player-controlled" : m == 1 ? "farthest" : m == 2 ? "nearest" : "unknown"; }
static void BossCatAdd(decltype(BossState::cOther)& arr, int& more, uintptr_t rva)
{
    if (!rva) { ++more; return; }   // f1.230 (review #8): 0 marks a free slot
    for (auto& x : arr) if (x.rva == rva) { ++x.n; return; }
    for (auto& x : arr) if (!x.rva) { x.rva = rva; x.n = 1; return; }
    ++more;
}
// enemies/22c Hook_UnitSetTarget (game thread): every UnitSetTarget call
static void BossCatCall(void* unit, uintptr_t outer, void* asked, void* given)
{
    BossState* b = BossOfUnit(unit);
    if (!b || !(b->boss || b->wasMinion)) return;   // f1.261: minions' commands too (MINIONCMD)
    if (outer == RVA_BP_SETTARGET_RET) ++b->cLeader;
    else if (outer == RVA_BP_BYTYPE_RET) { int m = g_ByTypeMode; ++b->cByType[m >= 0 && m <= 2 ? m : 3]; if (m < 0) b->unknownOp = g_ByTypeOp; }
    else if (outer == RVA_BP_SETPLAYER_RET) ++b->cPlayer;
    else BossCatAdd(b->cOther, b->cOtherMore, outer);
    if (given != asked) ++b->cChanged;
}
// enemies/22c Hook_SetUnitTarget (game thread): a target written on it directly (not through UnitSetTarget)
static void BossCatDirect(void* unit, uintptr_t caller)
{
    BossState* b = BossOfUnit(unit);
    if (!b || !b->boss) return;
    BossCatAdd(b->cDirect, b->cDirectMore, caller);
}
static void BossCatEnd()
{
    ULONGLONG now = GetTickCount64();
    for (auto& b : g_Boss)
    {
        if (!b.unit || !(b.boss || b.wasMinion)) continue;   // f1.261: + one MINIONCMD line per minion
        const char* nm = b.name;   // f1.230: not NameOf(b.enemy) - it may have left the enemy list (freed) long ago
        char oth[160] = "none", dir[160] = "none"; size_t lo = 0, ld = 0;
        for (auto& x : b.cOther) if (x.rva && lo < sizeof(oth)) lo += snprintf(oth + lo, sizeof(oth) - lo, "%s0x%llX x%d", lo ? ", " : "", (unsigned long long)x.rva, x.n);
        if (b.cOtherMore && lo < sizeof(oth)) snprintf(oth + lo, sizeof(oth) - lo, " + %d more", b.cOtherMore);
        for (auto& x : b.cDirect) if (x.rva && ld < sizeof(dir)) ld += snprintf(dir + ld, sizeof(dir) - ld, "%s0x%llX x%d", ld ? ", " : "", (unsigned long long)x.rva, x.n);
        if (b.cDirectMore && ld < sizeof(dir)) snprintf(dir + ld, sizeof(dir) - ld, " + %d more", b.cDirectMore);
        char unk[32] = ""; if (b.cByType[3]) snprintf(unk, sizeof(unk), " (bytecode 0x%02X)", b.unknownOp);
        CoopLog("%s: %s - its target commands this battle (%.0f s): SetTarget \"the leader\" %d | SetTargetByType player-controlled %d, farthest %d, nearest %d, unknown %d%s | SetTargetPlayer %d | other UnitSetTarget code: %s | direct writes: %s | changed by the mod: %d",
            b.boss ? "BOSSCMD" : "MINIONCMD", nm, b.firstSeen ? (now - b.firstSeen) / 1000.0 : 0.0, b.cLeader, b.cByType[0], b.cByType[1], b.cByType[2], b.cByType[3], unk, b.cPlayer, oth, dir, b.cChanged);
    }
}
