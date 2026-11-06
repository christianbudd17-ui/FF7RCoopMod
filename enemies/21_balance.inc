// ---- v5.8: co-op balance (enemy HP, stagger rate, enemy focus on player 2) ----
// Runs on the game thread, over the enemy list the P2 targeting already keeps (refreshed every 100 ms) - f1.151: with
// nobody but player 1 playing, over the mod's own enemy scan (BalEnemyList, only while a value is away from the game's
// normal). Everything is checked before it's touched. (Before f1.151: only while player 2 / 3 was in.)
//  HP:      enemy battle unit (chara+0xCB0) -> status object (unit+0x5E8); its vtable: +0x48 GetHP,
//           +0x58 GetHPMax, +0x60 SetHPMax. Current HP goes through the game's own setter 0x96C4E0
//           (UnitSetHP, the one SetHPPercent uses). Applied once per enemy (re-applied if the game
//           resets the max, at most 10 times).
//  Stagger: enemy units (vtable slot +0x810 = 0x8947C0, SetBPDamageCoefficient) keep the stagger
//           (BP) damage coefficient at unit+0xD14, multiplied into every stagger hit by 0x966EE0.
//           We keep it at (whatever the game/script set) x StaggerRate.
//  Focus:   enemy AI controllers keep a hate table (TMap<char type, int32>) at ctrl+0x1AC8; the
//           game raises it faster for the character the player controls (UCPC) than for AI party
//           members (AIPC). Increases of player 2's entry are multiplied by FocusOnPlayer2.
//           The controller is APawn::Controller (by reflection); it must have ApplyHate and its
//           character weak ref (+0x758) must point back at the enemy before we touch it.
static constexpr uintptr_t RVA_UNIT_SET_HP = 0x96C4E0, RVA_ENEMY_SET_BPCOEF = 0x8947C0;
typedef void (*UnitSetHP_t)(void* unit, int hp);
typedef int  (*StatGetI_t)(void* status);
typedef void (*StatSetI_t)(void* status, int v);
struct BalEnemy
{
    int idx = -1, ser = 0;
    int origMax = 0, ourMax = 0, hpTries = 0; ULONGLONG hpCheck = 0;
    float coefWritten = -1.f;
    int hateLast[2] = {-1, -1};   // per human player
    void* ctrl = nullptr; bool ctrlChecked = false;
    signed char boss = -1;   // f1.227: -1 = not looked at yet, 1 = a boss ("EB" + digit) - its HP is left alone
};
static BalEnemy g_Bal[64];
// f1.228 (the author: "Yeah I guess you should" - the summon / VR fights): a boss is an enemy whose object name starts
// with "EB" + a digit (the story bosses) or "SU" + a digit (the summons - Ifrit SU0000, Shiva SU0002, Leviathan SU0004,
// Bahamut SU0005, Ramuh SU1000 ... - fought in Chadley's VR battles). Only enemies are ever checked (the enemy list), so
// the party's own summons in normal battles are never treated as bosses.
static bool BossNameText(const char* nm)
{
    return nm && ((nm[0] == 'E' && nm[1] == 'B') || (nm[0] == 'S' && nm[1] == 'U')) && nm[2] >= '0' && nm[2] <= '9';
}
static bool BossStatusObj(void* status);   // enemies/22d (f1.227): this HP status belongs to a boss
static bool BossUnitObj(void* unit);
// the enemies the balance code works on. While player 2 / 3 plays = the targeting's list (g_EnemyIdx, refreshed by
// their movement pass); with player 1 alone that list isn't refreshed, so the balance code asks the game itself (the same
// CollectEnemies call, every 250 ms) - only while an active value is away from the game's normal (solo + defaults = the
// mod stays out of the way, as always).
static int g_BalEIdx[64], g_BalESer[64], g_BalEN = 0;
static void BalEnemyList(bool anyHuman)
{
    if (anyHuman) { g_BalEN = g_EnemyN; memcpy(g_BalEIdx, g_EnemyIdx, sizeof(int) * g_EnemyN); memcpy(g_BalESer, g_EnemySer, sizeof(int) * g_EnemyN); return; }
    if (!BalanceActive(g_Cfg)) { g_BalEN = 0; return; }
    static ULONGLONG at = 0; ULONGLONG now = GetTickCount64();
    if (at && now - at < 250) return;
    at = now;
    void* e[64]; int n = CollectEnemies(e, 64), m = 0;
    for (int i = 0; i < n; ++i) if (MakeWeak(e[i], &g_BalEIdx[m], &g_BalESer[m])) ++m;
    g_BalEN = m;
    static int logs = 0;
    if (m > 0 && logs < 3) { ++logs; CoopLog("PARTYBAL: player 1 alone - the enemy settings use the mod's own enemy list (%d enemies)", m); }
}

// ---- f1.151: enemy settings per party size ----
// The author 2026-10-25: "We don't want a situation where when there is just a single party member the game is extremely
// difficult, but when there are 3 it is too easy." Three sets ([Balance1] / [Balance2] / [Balance3], settings/03
// BalanceSet). Which one: the number of characters in the party (PartyCount). The two- and three-character sets have
// OnlyWithHumanPlayers (default 1): then they count only the people playing - 3 characters with 2 people playing = the
// two-character set (the author: "2-character settings"), player 1 alone = the one-character set.
// GUARDRAIL (the author: "so the health of enemies doesn't change when a controller drops out and a player becomes AI"):
// the set never goes DOWN during a battle (f1.269: it goes UP at once when more people's characters join mid-battle - see
// PartyBalanceTick). A change seen in a battle waits until the battle has been over for 3 s of play
// (the count restarts after a pause / loading gap and during cutscenes - wave breaks and in-battle scenes don't end it).
// Outside battles the new set applies once the party has looked the same for 1 s. Enemies keep the HP they were given
// (BalanceHP scales each enemy once). F6 outside a battle re-reads the active set's values at once; an F6 in a
// battle stores them (settings/03 g_BalReloadWaiting) and they apply here when the battle is over.
// (code review): InBattleNow() follows the battle command menu (ui state 8 / 9) - an ambush / battle intro before
// that menu is up, or a stretch where it pauses (summon, wave break) without a cutscene flag, would not count as a battle.
// So enemies the game lists for the battle (CollectEnemies, every 250 ms) count too - only when a party-size set is away
// from the game's normal (otherwise all sets are the same and nothing needs guarding).
static bool BalSetIsNormal(const BalanceSet& b)
{
    BalanceSet n;
    return b.hp == n.hp && b.damage == n.damage && b.attackRate == n.attackRate && b.stagger == n.stagger && b.focusP2 == n.focusP2 &&
           b.stayOnHit == n.stayOnHit && b.turnHeavy == n.turnHeavy && b.weakChance == n.weakChance && b.weakHealth == n.weakHealth;
}
static bool BalEnemiesPresent()
{
    if (BalSetIsNormal(g_Cfg.balSet[0]) && BalSetIsNormal(g_Cfg.balSet[1]) && BalSetIsNormal(g_Cfg.balSet[2]) && BalSetIsNormal(g_Cfg.balSet[3])) return false;
    static ULONGLONG at = 0; static bool v = false; ULONGLONG now = GetTickCount64();
    if (!at || now - at >= 250) { at = now; void* e[8]; v = CollectEnemies(e, 8) > 0; }
    return v;
}
static void PartyBalanceTick(void* const* human)
{
    ULONGLONG now = GetTickCount64();
    static ULONGLONG lastCall = 0, outSince = 0, candSince = 0;
    bool& inB = g_BalBattle;   // shared with LoadConfig (F6 in a battle waits too)
    static bool first = true;
    static int cand = -1, waiting = -1;
    bool gap = lastCall && now - lastCall > 1000;   // the game was paused / loading (this tick didn't run)
    lastCall = now;
    int n = PartyCount();
    if (n < 1) return;   // no party (title screen, loading)
    int people = 1 + (human[0] ? 1 : 0) + (human[1] ? 1 : 0);
    if (people > n) people = n;
    int pick = n;
    if (pick == 3 && g_Cfg.balSet[2].humansOnly && people < 3) pick = people;
    if (pick == 2 && g_Cfg.balSet[1].humansOnly && people < 2) pick = 1;
    int want = g_Cfg.dynamicDifficulty ? pick - 1 : 3;   // f1.272: DynamicDifficulty=0 -> always [BalanceGlobal]
    static int lastMode = -1; static bool modePending = false;   // f1.274: a Same / Dynamic switch seen (EnemyChangesApplyNow)
    int mode = g_Cfg.dynamicDifficulty ? 1 : 0;
    if (lastMode >= 0 && mode != lastMode) modePending = true;
    lastMode = mode;
    bool menu = InBattleNow(), foes = !menu && BalEnemiesPresent();   // + enemies listed for a battle
    bool b = menu || foes;
    static ULONGLONG quietSince = 0;   // f1.277: when the battle menu / enemies went away (a scripted shot holds the end only 60 s)
    if (b) { if (!inB) CoopLog("PARTYBAL: battle%s - the %s enemy settings stay until it is over", menu ? "" : " (enemies present, battle menu not up yet)", BalSetName(g_BalSetIdx)); inB = true; outSince = 0; quietSince = 0; }
    else if (inB)
    {
        // f1.277 (long play 2026-11-02): a scripted camera shot that never ended (the f1.276 normal-FOV bug: every walking
        // view counted as one) kept the battle open from 20:24:32 to the end - Cloud alone in the colosseum got the two-character
        // set. A scripted shot now holds the end back for at most 60 s; a cutscene or a pause / loading gap still restarts it.
        if (gap || !quietSince) quietSince = now;
        bool shotHold = g_ScriptedShot && now - quietSince < 60000;
        if (gap || g_InCutscene || shotHold || !outSince) outSince = now;
        else if (now - outSince >= 3000)
        {
            inB = false;
            CoopLog("PARTYBAL: battle over%s", g_ScriptedShot ? " (a scripted camera shot held the end back for 60 s - not waiting longer)" : "");
            quietSince = 0;
        }
    }
    if (want != cand) { cand = want; candSince = now; }
    if (g_BalReloadWaiting && !inB)
    {   // an F6 during the battle changed enemy values - now they apply
        g_BalReloadWaiting = false;
        ApplyBalanceSet(g_Cfg, g_BalSetIdx);
        CoopLog("PARTYBAL: the enemy settings reloaded during the battle apply now (%s set: enemy HP %.0f%%, damage %.0f%%, attack rate %.0f%%, stagger rate %.0f%%)",
            BalSetName(g_BalSetIdx), g_Cfg.balHP * 100.f, g_Cfg.balEnemyDamage * 100.f, g_Cfg.balAttackRate * 100.f, g_Cfg.balStagger * 100.f);
    }
    if (first && !inB)   // (code review): the first look applies at once only outside a battle - in one it waits like any change
    {
        first = false;
        if (want != g_BalSetIdx) { g_BalSetIdx = want; ApplyBalanceSet(g_Cfg, want); }
        CoopLog("PARTYBAL: party of %d, %d %s playing -> the %s enemy settings (enemy HP %.0f%%, damage %.0f%%, attack rate %.0f%%, stagger rate %.0f%%)",
            n, people, people == 1 ? "person" : "people", BalSetName(want), g_Cfg.balHP * 100.f, g_Cfg.balEnemyDamage * 100.f, g_Cfg.balAttackRate * 100.f, g_Cfg.balStagger * 100.f);
        return;
    }
    first = false;
    if (want == g_BalSetIdx)
    {
        modePending = false;
        if (waiting >= 0) { CoopLog("PARTYBAL: the party is back to the %s enemy settings - nothing to change", BalSetName(want)); waiting = -1; }
        return;
    }
    // f1.273 (code review #52): only a bigger PARTY-SIZE set (Dynamic, sets 0 - 2) goes up mid-battle. The Global set is index
    // 3 - not "bigger": a switch between Dynamic and Global (F6 in a battle) waits for the battle's end like any change.
    bool up = want <= 2 && g_BalSetIdx <= 2 && want > g_BalSetIdx;
    // f1.274 (testing, [General] EnemyChangesApplyNow): a Same / Dynamic switch (F6) applies mid-battle too
    if (inB && g_Cfg.enemyNow && modePending && now - candSince >= 1000)
    {
        int old = g_BalSetIdx;
        g_BalSetIdx = want;
        ApplyBalanceSet(g_Cfg, want);
        modePending = false; waiting = -1;
        CoopLog("PARTYBAL: TESTING (EnemyChangesApplyNow) - difficulty %s: the %s enemy settings NOW, during the battle (was %s; enemies already here keep their health) - damage %.0f%%, attack rate %.0f%%, stagger rate %.0f%%",
            g_Cfg.dynamicDifficulty ? "follows how many people play" : "the same for any number of people", BalSetName(want), BalSetName(old),
            g_Cfg.balEnemyDamage * 100.f, g_Cfg.balAttackRate * 100.f, g_Cfg.balStagger * 100.f);
        return;
    }
    if (!inB) modePending = false;
    if (inB && up && now - candSince >= 1000)
    {   // f1.269 (the author, the Rufus -> Arsenal fight 22:32: the game runs both as ONE battle, so the Arsenal kept the
        // one-character settings although player 2's Aerith had joined - "that should adjust for two party size in that
        // scenario"; the Sephiroth fight swaps the party mid-fight too): MORE people's characters in the party during a battle
        // (stable for 1 s) -> the bigger set applies at once. Damage / attack rate / stagger / focus are read live, a boss's
        // EnemyHP is a live damage factor (f1.227), and enemies that appear from now on get its HP; enemies already scaled
        // keep theirs (BalanceHP: ourMax). FEWER (a solo stretch, a controller dropping out) still waits for the battle's end.
        int old = g_BalSetIdx;
        g_BalSetIdx = want;
        ApplyBalanceSet(g_Cfg, want);
        CoopLog("PARTYBAL: party of %d, %d %s playing -> the %s enemy settings NOW, during the battle (was %s; more people's characters joined) - enemy HP %.0f%% (bosses and new enemies), damage %.0f%%, attack rate %.0f%%, stagger rate %.0f%%; it goes down again only after the battle",
            n, people, people == 1 ? "person" : "people", BalSetName(want), BalSetName(old), g_Cfg.balHP * 100.f, g_Cfg.balEnemyDamage * 100.f, g_Cfg.balAttackRate * 100.f, g_Cfg.balStagger * 100.f);
        waiting = -1;
        return;
    }
    if (inB)
    {
        if (up) return;   // f1.269: more people - applies above once it has looked the same for 1 s
        if (waiting != want)
        {
            waiting = want;
            CoopLog("PARTYBAL: party of %d, %d %s playing -> the %s enemy settings WAIT until this battle is over (enemy settings don't change during a battle)",
                n, people, people == 1 ? "person" : "people", BalSetName(want));
        }
        return;
    }
    if (now - candSince < 1000) return;   // the party must look the same for 1 s (a join / leave settling)
    int old = g_BalSetIdx;
    g_BalSetIdx = want;
    ApplyBalanceSet(g_Cfg, want);
    CoopLog("PARTYBAL: party of %d, %d %s playing -> the %s enemy settings (was %s%s) - enemy HP %.0f%%, damage %.0f%%, attack rate %.0f%%, stagger rate %.0f%%",
        n, people, people == 1 ? "person" : "people", BalSetName(want), BalSetName(old), waiting >= 0 ? ", waited for the battle to end" : "",
        g_Cfg.balHP * 100.f, g_Cfg.balEnemyDamage * 100.f, g_Cfg.balAttackRate * 100.f, g_Cfg.balStagger * 100.f);
    waiting = -1;
}
static int g_CtrlOff = -2;   // APawn::Controller offset (-2 = not looked up yet)
static bool InText(void* p) { uintptr_t a = (uintptr_t)p; return a >= g_Base + 0x1000 && a < g_Base + 0x4100000; }
static BalEnemy* BalSlot(void* enemy)
{
    int idx, ser; if (!MakeWeak(enemy, &idx, &ser)) return nullptr;
    BalEnemy* freeS = nullptr;
    for (auto& b : g_Bal)
    {
        if (b.idx == idx && b.ser == ser) return &b;
        if (!freeS && (b.idx < 0 || !ResolveWeak(b.idx, b.ser))) freeS = &b;
    }
    if (!freeS) return nullptr;
    *freeS = BalEnemy(); freeS->idx = idx; freeS->ser = ser;
    return freeS;
}
static void BalanceHP(void* enemy, unsigned char* unit, BalEnemy& b)
{
    if (g_Cfg.balHP == 1.f || b.hpTries >= 10) return;
    ULONGLONG now = GetTickCount64();
    if (now < b.hpCheck) return;
    b.hpCheck = now + 1000;
    if (b.boss < 0)
    {   // f1.227 (the author: "Go ahead and do the fixes now"): a boss keeps the game's own max HP - its phase lines are fixed HP
        // numbers worked out from it (S13: the multiplier's re-applies rescaled Eligor's HP past a phase floor). Its EnemyHP is
        // applied as less damage taken instead (Hook_HPChange / Hook_HPAfterDmg below).
        char bn[96] = ""; NameOf(enemy, bn, sizeof(bn));
        b.boss = BossNameText(bn) ? 1 : 0;   // f1.228: + the summons (SU)
        if (b.boss) CoopLog("BALANCE: %s is a boss - its HP stays as the game set it; it takes x%.2f damage instead (EnemyHP %.0f%%)", bn, 1.0 / g_Cfg.balHP, g_Cfg.balHP * 100.f);
    }
    if (b.boss) return;
    unsigned char* st = Readable(unit + 0x5E8, 8) ? *(unsigned char**)(unit + 0x5E8) : nullptr;
    if (!st || !Readable(st, 0x40)) return;
    void** vt = *(void***)st;
    if (!vt || !Readable(vt, 0x68) || !InText(vt[0x48 / 8]) || !InText(vt[0x58 / 8]) || !InText(vt[0x60 / 8])) return;
    int cur = ((StatGetI_t)vt[0x48 / 8])(st);
    int mx = ((StatGetI_t)vt[0x58 / 8])(st);
    if (mx <= 0 || cur <= 0 || mx == b.ourMax) return;   // dead, odd, or already ours
    double nm = (double)mx * g_Cfg.balHP;
    if (nm > 2.0e9) nm = 2.0e9;
    int newMax = (int)nm; if (newMax < 1) newMax = 1;
    int newCur = (int)((double)cur * newMax / mx); if (newCur < 1) newCur = 1; if (newCur > newMax) newCur = newMax;
    ((StatSetI_t)vt[0x60 / 8])(st, newMax);
    ((UnitSetHP_t)(g_Base + RVA_UNIT_SET_HP))(unit, newCur);
    b.origMax = mx; b.ourMax = newMax; b.hpTries++;
    char nm2[96] = "?"; NameOf(enemy, nm2, sizeof(nm2));
    CoopLog("BALANCE: %s HP %d/%d -> %d/%d (x%.2f)%s", nm2, cur, mx, newCur, newMax, g_Cfg.balHP, b.hpTries > 1 ? " - re-applied, the game had reset it" : "");
}
static void BalanceStagger(unsigned char* unit, BalEnemy& b)
{
    if (g_Cfg.balStagger == 1.f && b.coefWritten < 0.f) return;
    void** vt = *(void***)unit;
    if (!vt || !Readable(vt, 0x818) || vt[0x810 / 8] != (void*)(g_Base + RVA_ENEMY_SET_BPCOEF)) return;   // not the enemy unit layout
    if (!Readable(unit + 0xD14, 4)) return;
    float* coef = (float*)(unit + 0xD14);
    float c = *coef;
    if (!(c >= 0.f && c < 100.f)) return;
    if (b.coefWritten >= 0.f && fabsf(c - b.coefWritten) < 1e-6f) return;   // still ours
    float base = c;                     // the game's (or a script's) value
    float want = base * g_Cfg.balStagger;
    *coef = want; b.coefWritten = want;
    static int logs = 0;
    if (logs < 20) { logs++; CoopLog("BALANCE: stagger coefficient %.2f -> %.2f", base, want); }
}
static void BalanceFocus(void* enemy, unsigned char* p2unit, BalEnemy& b, int w)
{
    if (g_Cfg.balFocusP2 == 1.f || !p2unit) return;
    if (!b.ctrlChecked)
    {
        b.ctrlChecked = true; b.ctrl = nullptr;
        static int aiOff = -2;
        if (g_CtrlOff == -2)
        {
            void* cls = *(void**)((unsigned char*)enemy + 0x10);
            g_CtrlOff = cls ? PropOff(cls, "Controller") : -1;
            aiOff = cls ? PropOff(cls, "ActiveAiController") : -1;
            CoopLog("BALANCE: Pawn.Controller offset %d, ActiveAiController offset %d", g_CtrlOff, aiOff);
        }
        int offs[2] = {g_CtrlOff, aiOff};
        for (int k = 0; k < 2 && !b.ctrl; ++k)
        {
            if (offs[k] <= 0) continue;
            unsigned char* ctrl = *(unsigned char**)((unsigned char*)enemy + offs[k]);
            if (!ctrl || !Readable(ctrl, 0x1B00)) continue;
            if (!FindFunc(ctrl, "ApplyHate")) continue;                                      // not a battle AI controller
            if (ResolveWeak(*(int*)(ctrl + 0x758), *(int*)(ctrl + 0x75C)) != enemy) continue; // layout check
            b.ctrl = ctrl;
        }
        static int logs = 0;
        if (logs < 5) { logs++; char nm[96] = "?"; NameOf(enemy, nm, sizeof(nm)); CoopLog("BALANCE: %s - hate table %s", nm, b.ctrl ? "found" : "NOT found (focus setting can't apply to this enemy)"); }
    }
    unsigned char* ctrl = (unsigned char*)b.ctrl;
    if (!ctrl || !Readable(ctrl + 0x1AC8, 0x30)) return;
    unsigned char* elems = *(unsigned char**)(ctrl + 0x1AC8);
    int num = *(int*)(ctrl + 0x1AD0);
    int numBits = *(int*)(ctrl + 0x1AF0);
    unsigned int* bits = *(unsigned int**)(ctrl + 0x1AE8);
    if (!bits) bits = (unsigned int*)(ctrl + 0x1AD8);
    if (!elems || num <= 0 || num > 32 || numBits < num || !Readable(elems, num * 16) || !Readable(bits, 4)) return;
    unsigned char p2type = p2unit[0x58];
    for (int i = 0; i < num; ++i)
    {
        if (!(bits[i >> 5] & (1u << (i & 31)))) continue;
        unsigned char* e = elems + i * 16;
        if (e[0] != p2type) continue;
        int* hate = (int*)(e + 4);
        int h = *hate;
        if (b.hateLast[w] >= 0 && h > b.hateLast[w])
        {
            double nh = b.hateLast[w] + (double)(h - b.hateLast[w]) * g_Cfg.balFocusP2;
            if (nh > 1.0e9) nh = 1.0e9;
            if (nh < 0) nh = 0;
            *hate = (int)nh; h = (int)nh;
        }
        b.hateLast[w] = h;
        return;
    }
    b.hateLast[w] = -1;   // no entry for this player yet
}

// ---- f1.58: [Balance] EnemyDamage - how much damage the party takes ----
// RE (exe 2026-10-21): 0x96E780 UnitHPAfterDamage(unit, curHP, damage) returns curHP - damage, held above the status's
// HP-min limits (status +0x118 / +0x108 = SetHPMinLimit...), then the caller sets it with UnitSetHP 0x96C4E0. Callers:
// call sites 0x978582 / 0x978990 / 0x978D1A (status ticks = damage over time, e.g. poison) and 0x9EF754 (an effect handler). Hooked: when the unit is a
// party member's (party member char +0xCB0) (co-op or not - the party-size set decides), damage x EnemyDamage (at least 1). Enemies' own
// damage is untouched. 100 = off.
// f1.227: EnemyHP for bosses = the damage they take / EnemyHP (at least 1) - their max HP is left alone (BalanceHP)
static int BossDmgScaled(int dmg, const char* how)
{
    float m = g_Cfg.balHP;
    if (!(m > 0.f) || m == 1.f || dmg <= 0) return dmg;
    double d = (double)dmg / m; if (d > 2.0e9) d = 2.0e9;
    int nd = (int)(d + 0.5); if (nd < 1) nd = 1;
    static LogRate rate;
    if (LogOk(rate, 10)) CoopLog("DAMAGE: a boss takes %d instead of %d%s (EnemyHP %.0f%% as less damage)", nd, dmg, how, m * 100.f);
    return nd;
}
typedef int (*HPAfterDmg_t)(void* unit, int cur, int dmg);
static HPAfterDmg_t Orig_HPAfterDmg = nullptr;
static int Hook_HPAfterDmg(void* unit, int cur, int dmg)
{
    float m = g_Cfg.balEnemyDamage;
    if (m != 1.f && dmg > 0 && unit && GetCurrentThreadId() == g_GameThreadId)   // also with player 1 alone (the party-size set decides)
    {
        unsigned char* mgr = PartyMgr(); int n = PartyCount(); int who = -1;
        for (int i = 0; i < n && who < 0; ++i)
        {
            void* ch = CharByType(mgr, OrderType(mgr, i));
            if (ch && Readable((unsigned char*)ch + OFF_BATTLE_UNIT, 8) && *(void**)((unsigned char*)ch + OFF_BATTLE_UNIT) == unit) who = i;
        }
        if (who >= 0)
        {
            double d = (double)dmg * m; if (d > 2.0e9) d = 2.0e9;
            int nd = (int)(d + 0.5); if (nd < 1) nd = 1;
            static int logs = 0;
            if (logs < 30) { ++logs; CoopLog("DAMAGE: party member %d takes %d instead of %d over time (EnemyDamage %.0f%%, HP %d)", who + 1, nd, dmg, m * 100.f, cur); }
            dmg = nd;
        }
    }
    if (dmg > 0 && unit && g_Cfg.balHP != 1.f && GetCurrentThreadId() == g_GameThreadId && BossUnitObj(unit)) dmg = BossDmgScaled(dmg, " over time");   // f1.227
    return Orig_HPAfterDmg(unit, cur, dmg);
}
// (code review): normal hits don't go through 0x96E780 (that is the damage-over-time / effect path). The main HP change
// is 0x979350 HPChange(int* amount, status, info, ...): it reads *amount, info+0x24 != 0 = a heal (goes to 0x979624), has its
// own copy of the HP-min-limit checks and calls UnitSetHP directly; it writes the dealt amount back through *amount (so the
// damage numbers show the scaled value). 8 arguments (4 registers + stack +0x28 / +0x30 / +0x38 / +0x40 = unit, may be 0) -
// all forwarded unchanged. Party check by status object: party member char +0xCB0 -> unit +0x5E8.
typedef uint64_t (*HPChange_t)(int* amount, void* status, void* info, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8);
// ---- f1.229 PLAYERS 2 / 3 CAN PUSH A BOSS INTO ITS PHASE (RE 2026-10-30; the author: "Do all of them") ----
// HPChange 0x979350 holds the target's HP above two lines (status +0x38 via vfunc +0x108 = the floor; +0x3C via vfunc
// +0x118 = the "passive damage / AI attacks" line where a boss's phase starts). The +0x3C line is SKIPPED only when its 6th
// argument a6 (or info +0x40 -> +0x50) is set and a7 / info +0x10 -> +0x178 & 0x20 are not: a6 = "the hit is the
// player-controlled character's" - the other caller 0x16F2074 passes exactly (causer char +0xCB0 unit +0x58 == game
// instance +0x233, the leader's type); the main one (0x979B3F, in 0x979750 TakeDamage(unit, causer char, ctx, info, a5) -
// called by the unit classes' damage vfuncs 0x891540 / 0x892D60 / 0x8A6A60) passes ctx +0x18. So in co-op only player 1's
// hits could push a boss past the line its phase starts at - players 2 / 3 (and AI teammates) stop 1 HP above it, and
// with player 1 knocked out (KOLEAD keeps player 1 the leader - in the normal game the leader would change) a boss could
// sit just above its phase line until player 1 is back. Hooked: 0x979750 notes the causer while it runs; in HPChange a
// hit on a non-party unit by player 2 / 3's character counts as the player-controlled character's (a6 = 1). AI teammates
// unchanged (as in the game). The 0x16F2074 path (its own a6) is left alone.
typedef uint64_t (*TakeDmg_t)(void* unit, void* causer, void* ctx, void* info, uint64_t a5);
static TakeDmg_t Orig_TakeDmg = nullptr;
static void* g_DmgCauser = nullptr;   // game thread: the causer while 0x979750 runs
static uint64_t Hook_TakeDmg(void* unit, void* causer, void* ctx, void* info, uint64_t a5)
{
    if (GetCurrentThreadId() != g_GameThreadId) return Orig_TakeDmg(unit, causer, ctx, info, a5);
    void* prev = g_DmgCauser; g_DmgCauser = causer;
    uint64_t r = Orig_TakeDmg(unit, causer, ctx, info, a5);
    g_DmgCauser = prev;
    return r;
}
static void* g_AttnHumanRef(int k);   // enemies/22 (g_AttnHuman - the characters players 2 / 3 play)
static HPChange_t Orig_HPChange = nullptr;
// f1.211 (the author, f1.208 Airbuster log: "I'd like to know the rate the enemy attacked each player"): every hit on a
// party member is counted per party order slot (0 - 2) - hits + HP lost - for the TARGETLOG 30 s / battle / session lines
// (enemies/22b maps the slot to P1 / P2 / P3 / AI there). Before, only the first 30 hits were logged (DAMAGE lines) and
// only with EnemyDamage != 100 %. Heals on party members (info +0x24 != 0) get a HEAL line (rate-limited) - for the
// author's "multi-use Cure casts once more after the menu closes" report: the extra cast shows as a HEAL after the menu's
// close line.
static int g_HitN[3] = {0, 0, 0};          // this 30 s window
static long long g_HitHP[3] = {0, 0, 0};
static int g_HitNB[3] = {0, 0, 0};         // this battle
static long long g_HitHPB[3] = {0, 0, 0};
static int PartySlotOfStatus(void* status)
{
    unsigned char* mgr = PartyMgr(); int n = PartyCount();
    for (int i = 0; i < n && i < 3; ++i)
    {
        unsigned char* ch = (unsigned char*)CharByType(mgr, OrderType(mgr, i));
        unsigned char* u = (ch && Readable(ch + OFF_BATTLE_UNIT, 8)) ? *(unsigned char**)(ch + OFF_BATTLE_UNIT) : nullptr;
        if (u && Readable(u + 0x5E8, 8) && *(void**)(u + 0x5E8) == status) return i;
    }
    return -1;
}
static uint64_t Hook_HPChange(int* amount, void* status, void* info, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
    float m = g_Cfg.balEnemyDamage;
    int who = -1; bool heal = false, known = false;
    if (amount && status && info && GetCurrentThreadId() == g_GameThreadId &&   // no co-op check (party-size set)
        Readable((unsigned char*)info + 0x24, 1) && *amount > 0)
    {
        known = true;
        heal = ((unsigned char*)info)[0x24] != 0;
        who = PartySlotOfStatus(status);
    }
    if (who >= 0 && !heal && m != 1.f)
    {
        int dmg = *amount;
        double d = (double)dmg * m; if (d > 2.0e9) d = 2.0e9;
        int nd = (int)(d + 0.5); if (nd < 1) nd = 1;
        static int logs = 0;
        if (logs < 30) { ++logs; CoopLog("DAMAGE: party member %d is hit for %d instead of %d (EnemyDamage %.0f%%)", who + 1, nd, dmg, m * 100.f); }
        *amount = nd;
    }
    const bool tgtBoss = known && who < 0 && !heal && BossStatusObj(status);   // f1.230 (review #6): looked up once
    if (tgtBoss && *amount > 0 && g_Cfg.balHP != 1.f)
        *amount = BossDmgScaled(*amount, "");   // f1.227: a boss's EnemyHP as less damage taken
    if (known && who < 0 && !heal && !(a6 & 0xFF) && g_DmgCauser && g_DmgCauser != LeaderChar() &&   // f1.230 (review #4): never the leader's own hits
        (g_DmgCauser == g_AttnHumanRef(0) || g_DmgCauser == g_AttnHumanRef(1)))
    {   // f1.229: player 2 / 3's hit counts as the player-controlled character's (the phase-start line doesn't stop it)
        a6 = (a6 & ~0xFFull) | 1;
        static int logs = 0;
        if (logs < 5) { ++logs; char cn[96] = "?"; NameOf(g_DmgCauser, cn, sizeof(cn)); CoopLog("DAMAGE: %s's hit counts as the player-controlled character's (a boss's phase-start line doesn't stop it)", cn); }
    }
    int bossAsk = tgtBoss ? *amount : -1;   // f1.229: a hit on a boss cut by its HP lines (log)
    // f1.230 (review #3): only when one of its lines is set (the game also cuts a killing blow to the HP left - not a line)
    void** svt = (tgtBoss && Readable(status, 8)) ? *(void***)status : nullptr;
    bool svtOk = svt && Readable(svt, 0x120) && InText(svt[0x48 / 8]) && InText(svt[0x108 / 8]) && InText(svt[0x118 / 8]);
    bool lineSet = svtOk && (((StatGetI_t)svt[0x108 / 8])(status) >= 0 || ((StatGetI_t)svt[0x118 / 8])(status) > 0);
    int asked = (who >= 0 && amount) ? *amount : 0;
    uint64_t r = Orig_HPChange(amount, status, info, a4, a5, a6, a7, a8);
    if (bossAsk > 0 && lineSet && Readable(amount, 4) && *amount >= 0 && *amount < bossAsk && ((StatGetI_t)svt[0x48 / 8])(status) > 0)
    {
        static LogRate rate;
        if (LogOk(rate, 10)) { char cn[96] = "-"; if (g_DmgCauser) NameOf(g_DmgCauser, cn, sizeof(cn));
            CoopLog("DAMAGE: a hit on a boss by %s was cut from %d to %d by its HP lines (phase-start line / floor) - player-controlled flag %d", cn, bossAsk, *amount, (int)(a6 & 0xFF)); }
    }
    if (who >= 0)
    {
        int dealt = (Readable(amount, 4) && *amount >= 0) ? *amount : asked;   // the game writes the dealt amount back
        if (!heal) { ++g_HitN[who]; g_HitHP[who] += dealt; ++g_HitNB[who]; g_HitHPB[who] += dealt; }
        else
        {
            static LogRate rate;
            if (LogOk(rate, 40))
            {
                char nm[96] = "?"; NameOf(CharByType(PartyMgr(), OrderType(PartyMgr(), who)), nm, sizeof(nm));
                CoopLog("HEAL: %s (party member %d) healed +%d", nm, who + 1, dealt);
            }
        }
    }
    return r;
}
static void InstallEnemyDamageHook()
{
    {   // the main hit path
        static const unsigned char ex2[16] = {0x40,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xF9};
        void* t2 = MakeInlineHook(g_Base + 0x979350, (void*)Hook_HPChange, 16, ex2, "HP change (EnemyDamage)");
        if (t2) Orig_HPChange = (HPChange_t)t2;
        CoopLog("DAMAGE: EnemyDamage hit hook %s", t2 ? "installed" : "NOT installed - only damage over time is scaled");
    }
    {   // f1.229: TakeDamage (mov r11,rsp; push rbp/rbx/rsi/r12/r13; lea rbp,[r11-178h] - 17 bytes, nothing RIP-relative)
        static const unsigned char exT[17] = {0x4C,0x8B,0xDC,0x55,0x53,0x56,0x41,0x54,0x41,0x55,0x49,0x8D,0xAB,0x88,0xFE,0xFF,0xFF};
        void* t3 = MakeInlineHook(g_Base + 0x979750, (void*)Hook_TakeDmg, 17, exT, "TakeDamage (players 2 / 3 count as player-controlled)");
        if (t3) Orig_TakeDmg = (TakeDmg_t)t3;
        CoopLog("DAMAGE: players 2 / 3 count as the player-controlled character against phase lines: %s", t3 ? "on" : "NOT installed");
    }
    static const unsigned char ex[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
    void* t = MakeInlineHook(g_Base + 0x96E780, (void*)Hook_HPAfterDmg, 15, ex, "HP after damage (EnemyDamage)");
    if (t) Orig_HPAfterDmg = (HPAfterDmg_t)t;
    CoopLog("DAMAGE: EnemyDamage hook %s", t ? "installed" : "NOT installed - EnemyDamage does nothing");
}

// ---- f1.62: [Balance] EnemyAttackRate - how often enemies use their abilities (movement unchanged) ----
// RE (claude/FF7R_Enemy_Attack_Rate_RE.md): an enemy's action request (0x923AC0) is refused while an ability's interval
// (+0x14) > its elapsed time (+0x10), or while the after-ability delay ctrl+0x5F4 (copy +0x634) is above 0. The elapsed
// times are advanced by dt only in 0x91DB30 UpdateActionTimers(ctrl, dt) (one caller 0x91D200). Hooked: after the game's
// own +dt, for ENEMY controllers (pawn ctrl+0x758 in the mod's enemy list) (co-op or not), abilities (type 1) and combos
// (0xFF) get dt * (rate - 1) more, and the delays count down that much faster. Move / turn / confront timers untouched.
// 100 = untouched (the hook still logs a summary per enemy, max 20, so a test at 100 shows the timers).
typedef void (*AiUpdTimers_t)(void* ctrl, float dt);
static AiUpdTimers_t Orig_AiUpdTimers = nullptr;
// (f1.63 test: "How often they attack didn't seem to affect anything" - the log had no ATTACKRATE line from the
// hook at all): the enemy check only worked on the game thread (off it: cached answers only, and the cache was only
// filled on the game thread from inside the hook) - if the AI timers run on another thread, nothing was ever sped up.
// Now: the hook (any thread) only LOOKS UP the controller in a table of single 64-bit entries (pointer | 1 = enemy) and
// queues unknown controllers; the game thread (AttackRateTick, from the main tick) classifies the queue and re-checks
// the table every 2 s. Counters show where it stops (ATTACKRATE DIAG every 30 s while the hook is called).
static volatile LONG64 g_AiTab[64];                 // 0 = empty, else ctrl pointer | (1 = enemy)
static volatile LONG64 g_AiPend[32];                // controllers the hook saw but the table doesn't know yet
static volatile LONG g_ArCalls = 0, g_ArGameThread = 0, g_ArEnemy = 0, g_ArScaled = 0, g_ArUnknown = 0;
static int AiTabFind(void* ctrl)
{
    for (int i = 0; i < 64; ++i) { LONG64 v = g_AiTab[i]; if (v && (void*)(v & ~(LONG64)7) == ctrl) return (int)(v & 1); }
    return -1;
}
static void AiPendAdd(void* ctrl)
{
    for (int i = 0; i < 32; ++i) if ((void*)g_AiPend[i] == ctrl) return;
    for (int i = 0; i < 32; ++i) if (InterlockedCompareExchange64(&g_AiPend[i], (LONG64)ctrl, 0) == 0) return;
}
static bool AiCtrlIsEnemy(void* ctrl)   // game thread only
{
    unsigned char* k = (unsigned char*)ctrl;
    if (!Readable(k + 0x758, 8)) return false;
    void* pawn = ResolveWeak(*(int*)(k + 0x758), *(int*)(k + 0x75C));
    for (int i = 0; pawn && i < g_EnemyN; ++i) if (ResolveWeak(g_EnemyIdx[i], g_EnemySer[i]) == pawn) return true;
    for (int i = 0; pawn && i < g_BalEN; ++i) if (ResolveWeak(g_BalEIdx[i], g_BalESer[i]) == pawn) return true;   // player 1 alone
    return false;
}
static void AiLogSummary(void* ctrl)   // game thread: once per enemy controller (max 20)
{
    static void* seen[20]; static int nSeen = 0;
    for (int i = 0; i < nSeen; ++i) if (seen[i] == ctrl) return;
    if (nSeen >= 20) return;
    seen[nSeen++] = ctrl;
    unsigned char* k = (unsigned char*)ctrl;
    if (!Readable(k + 0x1768, 0x30) || !Readable(k + 0x5F4, 4)) return;
    unsigned char* el = *(unsigned char**)(k + 0x1768);
    int nBits = *(int*)(k + 0x1768 + 0x28), nAb = 0; float iv0 = -1.f;
    const unsigned* bits = *(const unsigned* const*)(k + 0x1768 + 0x20); if (!bits) bits = (const unsigned*)(k + 0x1768 + 0x10);
    if (el && nBits > 0 && nBits < 4096 && Readable(bits, (size_t)((nBits + 31) / 32) * 4) && Readable(el, (size_t)nBits * 0x18))
        for (int i = 0; i < nBits; ++i)
        {
            if (!(bits[i >> 5] & (1u << (i & 31)))) continue;
            unsigned char* a = *(unsigned char**)(el + (size_t)i * 0x18 + 8);
            if (!a || !Readable(a, 0x24)) continue;
            int type = *(int*)(a + 0x20);
            if (type != 1 && type != 0xFF) continue;
            ++nAb; if (iv0 < 0.f) iv0 = *(float*)(a + 0x14);
        }
    char nm[96] = "?"; void* pawn = ResolveWeak(*(int*)(k + 0x758), *(int*)(k + 0x75C)); if (pawn) NameOf(pawn, nm, sizeof(nm));
    CoopLog("ATTACKRATE: %s - %d ability / combo timer(s) (first interval %.2f s), after-ability delay now %.2f s, EnemyAttackRate %.0f%%",
        nm, nAb, iv0, *(float*)(k + 0x5F4), g_Cfg.balAttackRate * 100.f);
}
// main tick (game thread): classify the queued controllers, re-check the table every 2 s, log the counters every 30 s
static void AttackRateTick()
{
    if (!Orig_AiUpdTimers) return;
    for (int i = 0; i < 32; ++i)
    {
        void* c = (void*)g_AiPend[i]; if (!c) continue;
        bool en = AiCtrlIsEnemy(c);
        if (AiTabFind(c) < 0)
        {
            int slot = -1;
            for (int j = 0; j < 64 && slot < 0; ++j) if (!g_AiTab[j]) slot = j;
            if (slot < 0) { static int r = 0; slot = r++ & 63; }
            InterlockedExchange64(&g_AiTab[slot], (LONG64)c | (en ? 1 : 0));
        }
        if (en) AiLogSummary(c);
        InterlockedExchange64(&g_AiPend[i], 0);
    }
    ULONGLONG now = GetTickCount64();
    static ULONGLONG lastCheck = 0, lastLog = 0;
    if (now - lastCheck > 2000)
    {   // re-check: a controller can get a new pawn (or the battle ended - the enemy list is empty)
        lastCheck = now;
        for (int j = 0; j < 64; ++j)
        {
            LONG64 v = g_AiTab[j]; if (!v) continue;
            void* c = (void*)(v & ~(LONG64)7);
            bool en = Readable((unsigned char*)c, 8) && AiCtrlIsEnemy(c);
            if (en != (bool)(v & 1)) { InterlockedExchange64(&g_AiTab[j], (LONG64)c | (en ? 1 : 0)); if (en) AiLogSummary(c); }   // summary also when it turns out to be an enemy later
        }
    }
    if (!lastLog) lastLog = now;
    if (now - lastLog > 30000)
    {
        lastLog = now;
        LONG calls = InterlockedExchange(&g_ArCalls, 0), gt = InterlockedExchange(&g_ArGameThread, 0), en = InterlockedExchange(&g_ArEnemy, 0);
        LONG sc = InterlockedExchange(&g_ArScaled, 0), un = InterlockedExchange(&g_ArUnknown, 0);
        int nE = 0, nO = 0; for (int j = 0; j < 64; ++j) { LONG64 v = g_AiTab[j]; if (v) { if (v & 1) ++nE; else ++nO; } }
        if (calls) CoopLog("ATTACKRATE DIAG: last 30 s - %ld timer updates (%ld on the game thread), %ld for enemies, %ld sped up / slowed, %ld not known yet | known controllers: %d enemies, %d others (party AI) | enemy list %d | EnemyAttackRate %.0f%%",
            calls, gt, en, sc, un, nE, nO, g_EnemyN, g_Cfg.balAttackRate * 100.f);
    }
}
static void Hook_AiUpdTimers(void* ctrl, float dt)
{
    Orig_AiUpdTimers(ctrl, dt);
    if (!ctrl) return;
    InterlockedIncrement(&g_ArCalls);
    if (GetCurrentThreadId() == g_GameThreadId) InterlockedIncrement(&g_ArGameThread);
    int kind = AiTabFind(ctrl);
    if (kind < 0) { InterlockedIncrement(&g_ArUnknown); AiPendAdd(ctrl); return; }
    if (!kind) return;
    InterlockedIncrement(&g_ArEnemy);
    float r = g_Cfg.balAttackRate;
    if (r == 1.f || !(dt > 0.f)) return;   // also with player 1 alone (the party-size set decides; was co-op only)
    unsigned char* k = (unsigned char*)ctrl;
    if (!Readable(k + 0x1768, 0x30) || !Readable(k + 0x5F4, 0x44)) return;
    float extra = dt * (r - 1.f);
    unsigned char* el = *(unsigned char**)(k + 0x1768);
    int nBits = *(int*)(k + 0x1768 + 0x28);
    const unsigned* bits = *(const unsigned* const*)(k + 0x1768 + 0x20); if (!bits) bits = (const unsigned*)(k + 0x1768 + 0x10);
    if (el && nBits > 0 && nBits < 4096 && Readable(bits, (size_t)((nBits + 31) / 32) * 4) && Readable(el, (size_t)nBits * 0x18))
        for (int i = 0; i < nBits; ++i)
        {
            if (!(bits[i >> 5] & (1u << (i & 31)))) continue;
            unsigned char* a = *(unsigned char**)(el + (size_t)i * 0x18 + 8);
            if (!a) continue;   // (no per-action Readable - the game's own pass just used these pointers)
            int type = *(int*)(a + 0x20);
            if (type == 1 || type == 0xFF) *(float*)(a + 0x10) += extra;
        }
    float* d1 = (float*)(k + 0x5F4); if (*d1 > 0.f) *d1 -= extra;
    float* d2 = (float*)(k + 0x634); if (*d2 > 0.f) *d2 -= extra;
    InterlockedIncrement(&g_ArScaled);
}
static void InstallEnemyAttackRateHook()
{
    static const unsigned char ex[16] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55};
    void* t = MakeInlineHook(g_Base + 0x91DB30, (void*)Hook_AiUpdTimers, 16, ex, "AI action timers (EnemyAttackRate)");
    if (t) Orig_AiUpdTimers = (AiUpdTimers_t)t;
    CoopLog("ATTACKRATE: EnemyAttackRate hook %s", t ? "installed" : "NOT installed - EnemyAttackRate does nothing");
}
