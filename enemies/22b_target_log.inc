// =================================================================================
// DIAG (BUG-3 step A, the author 2026-10-22: "Okay, do that"): log-only - how enemies really pick targets.
// RE (claude/FF7R_Enemy_Targeting_RE.md): each enemy AI controller keeps a hate score per party member (ctrl+0x1AC8,
// TMap<char type, int32>, 0..1000). After each of its actions the game re-scores (0x93ED90: the current target goes
// down, everyone else up, plus a little random) and targets the highest score (0x93FE20). Nothing here changes the game:
//  - every target change of every enemy, with everyone's hate score (TARGETLOG, capped)
//  - every 30 s in battle and at the end of each battle: the share of time enemies spent on P1 / P2 / P3 / AI teammates
//  - once per battle: the real hate rates the game uses (the game's own parameter lookup 0xB7A500, read only)
// [General] TargetLog=0 switches it off. Removal checklist: delete with the BUG-3 fix (or when the answer is in).
// BACK (removed in f1.99). The author 2026-10-23: "keep the enemy targeting logging going to see which players are
// being attacked ... on a longer play through ... It seems that if P2 and P3 are standing around and not doing much that P1
// gets targeted more heavily." Added: how much of the time each player's character was moving (P1 / P2 / P3, from their
// position 10x a second) next to the 30 s and per-battle shares, and a running total for the whole session at each
// battle's end. Target-change lines capped at 1500 per session (was 600).
// =================================================================================
static constexpr uintptr_t RVA_HATE_PARAM = 0xB7A500;     // int GetParam(const entry*, int default) - lookup by name
static constexpr uintptr_t RVA_HATE_TABLE = 0x5835720;    // 3 rows x 0x58: initial values, the 4 rates, their random ranges
typedef int (*HateParam_t)(const void* entry, int def);
struct TlEnemy { int idx = -1, ser = 0; void* ctrl = nullptr; bool ctrlChecked = false; void* lastTgt = nullptr; ULONGLONG since = 0; char lastName[24] = "nobody"; int lastWho = 4; };
static TlEnemy g_Tl[32];
static double g_TlTime[5] = {0, 0, 0, 0, 0}, g_TlBattle[5] = {0, 0, 0, 0, 0}, g_TlSession[5] = {0, 0, 0, 0, 0};   // enemy-seconds on P1, P2, P3, AI teammate, nobody
static double g_TlMove[3][2] = {}, g_TlMoveB[3][2] = {}, g_TlMoveS[3][2] = {};   // per player [seconds moving, seconds sampled] - 30 s / battle / session
static int g_TlBattles = 0;
static int g_TlLines = 0;
static void TlShort(void* ch, char* out, size_t n)
{   // "PC0001_00_Barret_Standard_C_0" -> "Barret"
    char nm[96] = "?"; NameOf(ch, nm, sizeof(nm));
    const char* p = nm; int us = 0;
    while (*p && us < 2) { if (*p == '_') ++us; ++p; }
    if (!*p) p = nm;
    size_t k = 0; while (p[k] && p[k] != '_' && k + 1 < n) { out[k] = p[k]; ++k; } out[k] = 0;
}
// 0 = player 1 (the leader), 1 = player 2, 2 = player 3, 3 = AI teammate, 4 = nobody / not a party member
static int TlWho(void* ch, void* const* humans)
{
    if (!ch) return 4;
    if (humans[0] && ch == humans[0]) return 1;
    if (humans[1] && ch == humans[1]) return 2;
    if (ch == LeaderChar()) return 0;
    for (int i = 0; i < 3 && GetPartyMember; ++i) if (GetPartyMember(i) == ch) return 3;
    return 4;
}
static const char* const kTlWho[5] = {"P1", "P2", "P3", "AI", "-"};
static unsigned char* TlCtrl(void* enemy, TlEnemy& t)
{
    if (t.ctrlChecked)
    {   // still this enemy's controller? (its weak reference back to the enemy)
        unsigned char* c = (unsigned char*)t.ctrl;
        if (c && Readable(c + 0x758, 8) && ResolveWeak(*(int*)(c + 0x758), *(int*)(c + 0x75C)) == enemy) return c;
        if (c) t.ctrlChecked = false; else return nullptr;
    }
    t.ctrlChecked = true; t.ctrl = nullptr;
    static int ctrlOff = -2, aiOff = -2;
    if (ctrlOff == -2)
    {
        void* cls = *(void**)((unsigned char*)enemy + 0x10);
        ctrlOff = cls ? PropOff(cls, "Controller") : -1;
        aiOff = cls ? PropOff(cls, "ActiveAiController") : -1;
    }
    int offs[2] = {ctrlOff, aiOff};
    for (int k = 0; k < 2 && !t.ctrl; ++k)
    {
        if (offs[k] <= 0) continue;
        unsigned char* ctrl = *(unsigned char**)((unsigned char*)enemy + offs[k]);
        if (!ctrl || !Readable(ctrl, 0x1B30)) continue;
        if (!FindFunc(ctrl, "ApplyHate")) continue;                                       // not a battle AI controller
        if (ResolveWeak(*(int*)(ctrl + 0x758), *(int*)(ctrl + 0x75C)) != enemy) continue;  // layout check
        t.ctrl = ctrl;
    }
    return (unsigned char*)t.ctrl;
}
// the party's hate scores in this enemy's table, as "Cloud 640, Barret 820, Tifa 300"
static void TlHate(unsigned char* ctrl, char* out, size_t n)
{
    snprintf(out, n, "no hate table");
    if (!ctrl || !Readable(ctrl + 0x1AC8, 0x30)) return;
    unsigned char* elems = *(unsigned char**)(ctrl + 0x1AC8);
    int num = *(int*)(ctrl + 0x1AD0), numBits = *(int*)(ctrl + 0x1AF0);
    unsigned int* bits = *(unsigned int**)(ctrl + 0x1AE8);
    if (!bits) bits = (unsigned int*)(ctrl + 0x1AD8);
    if (!elems || num <= 0 || num > 32 || numBits < num || !Readable(elems, num * 16) || !Readable(bits, 4)) return;
    unsigned char* mgr = PartyMgr();
    size_t len = 0; out[0] = 0;
    for (int i = 0; i < num; ++i)
    {
        if (!(bits[i >> 5] & (1u << (i & 31)))) continue;
        unsigned char* e = elems + i * 16;
        void* ch = CharByType(mgr, e[0]);
        char nm[24] = "?"; if (ch) TlShort(ch, nm, sizeof(nm)); else snprintf(nm, sizeof(nm), "type%d", e[0]);
        int w = snprintf(out + len, n - len, "%s%s %d", len ? ", " : "", nm, *(int*)(e + 4));
        if (w < 0 || (size_t)w >= n - len) break;
        len += w;
    }
    if (!len) snprintf(out, n, "empty (this enemy keeps no hate scores - the mod's hate rules can't steer it)");
}
// once per battle: the hate rates the game really uses (row = the enemy unit's difficulty byte, as 0x93EFE8 picks it)
static void TlLogRates(void* enemy)
{
    static int ok = -1;
    if (ok < 0)
    {   // this exe: the rate table address at 0x93F28D (lea rcx, [rip+0x4EF648C]) and GetParam's first bytes
        static const unsigned char lea[] = {0x48, 0x8D, 0x0D, 0x8C, 0x64, 0xEF, 0x04};
        static const unsigned char fn[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
        ok = (memcmp((void*)(g_Base + 0x93F28D), lea, sizeof(lea)) == 0 && memcmp((void*)(g_Base + RVA_HATE_PARAM), fn, sizeof(fn)) == 0) ? 1 : 0;
        if (!ok) CoopLog("TARGETLOG: hate rates can't be read (this game version's code doesn't match)");
    }
    if (!ok) return;
    unsigned char* unit = *(unsigned char**)((unsigned char*)enemy + OFF_BATTLE_UNIT);
    unsigned char* d = (unit && Readable(unit + 0x5B8, 8)) ? *(unsigned char**)(unit + 0x5B8) : nullptr;
    if (!d || !Readable(d + 0x16C, 1)) return;
    int row = (int)d[0x16C] - 1; if (row < 0 || row > 2) row = 0;
    unsigned char* t = (unsigned char*)(g_Base + RVA_HATE_TABLE) + row * 0x58;
    if (!Readable(t, 0x58)) return;
    HateParam_t gp = (HateParam_t)(g_Base + RVA_HATE_PARAM);
    CoopLog("TARGETLOG: hate rates (row %d) - the enemy's target: %d if player-1-like / %d if AI teammate (+ random 0-%d / 0-%d); "
            "everyone else: %+d if player-1-like / %+d if AI teammate (+ random 0-%d / 0-%d); start values %d %d %d",
        row, gp(t + 0x18, -200), gp(t + 0x20, -1000), gp(t + 0x28, 50), gp(t + 0x30, 50),
        gp(t + 0x38, 200), gp(t + 0x40, 100), gp(t + 0x48, 50), gp(t + 0x50, 50), gp(t + 0x00, 1000), gp(t + 0x08, 1000), gp(t + 0x10, 1000));
}
// f1.211: hits taken per player (enemies/21 Hook_HPChange counts per party order slot) - " | hits taken: P1 Cloud 12 hits /
// 1500 HP, P2 Barret 3 / 200, P3 Tifa 0" (slot -> player at the time of the line)
static void TlHits(const int* n, const long long* hp, void* const* humans, char* out, size_t sz)
{
    unsigned char* mgr = PartyMgr();
    size_t len = snprintf(out, sz, " | hits taken:");
    bool any = false;
    for (int i = 0; i < 3 && len < sz; ++i)
    {
        void* ch = mgr ? CharByType(mgr, OrderType(mgr, i)) : nullptr;
        if (!ch) continue;
        char nm[24] = "?"; TlShort(ch, nm, sizeof(nm));
        len += snprintf(out + len, sz - len, "%s %s %s %d hit(s) / %lld HP", any ? "," : "", kTlWho[TlWho(ch, humans)], nm, n[i], hp[i]);
        any = true;
    }
    if (!any) snprintf(out, sz, " | hits taken: -");
}
static int g_HitS[5] = {0, 0, 0, 0, 0};          // the session, per P1 / P2 / P3 / AI (mapped at each battle's end)
static long long g_HitHPS[5] = {0, 0, 0, 0, 0};
static void TlShares(const double* s, char* out, size_t n)
{
    double tot = s[0] + s[1] + s[2] + s[3];
    if (tot <= 0.0) { snprintf(out, n, "no enemy had a target"); return; }
    snprintf(out, n, "P1 %.0f%%, P2 %.0f%%, P3 %.0f%%, AI teammates %.0f%% (%.0f enemy-seconds with a target, %.0f without)",
        100.0 * s[0] / tot, 100.0 * s[1] / tot, 100.0 * s[2] / tot, 100.0 * s[3] / tot, tot, s[4]);
}
static void TlMoving(const double (*m)[2], char* out, size_t n)
{   // " | moving: P1 40%, P2 12%, P3 -" (- = not in / no sample)
    size_t len = snprintf(out, n, " | moving:");
    for (int p = 0; p < 3 && len < n; ++p)
    {
        if (m[p][1] > 0.5) len += snprintf(out + len, n - len, " P%d %.0f%%", p + 1, 100.0 * m[p][0] / m[p][1]);
        else len += snprintf(out + len, n - len, " P%d -", p + 1);
    }
}
static void TsSummary();   // enemies/22c (f1.212)
static void TargetLogTick(void* const* humans)
{
    if (!g_Cfg.targetLog || !g_Base || GetCurrentThreadId() != g_GameThreadId) return;
    static bool wasBattle = false; static ULONGLONG lastT = 0, sumAt = 0, scanAt = 0, seenAt = 0; static bool ratesDone = false;
    ULONGLONG now = GetTickCount64();
    if (InBattleNow()) seenAt = now;
    bool battle = seenAt && now - seenAt < 3000;   // a battle ends only after 3 s without battle state (it can flicker)
    if (!battle)
    {
        if (wasBattle)
        {   // the battle just ended: its totals (+ moving, + the whole session so far)
            char s[200], mv[80], ht[240]; TlShares(g_TlBattle, s, sizeof(s)); TlMoving(g_TlMoveB, mv, sizeof(mv));
            TlHits(g_HitNB, g_HitHPB, humans, ht, sizeof(ht));
            CoopLog("TARGETLOG: battle over - time enemies spent on each: %s%s%s", s, mv, ht);
            {
                unsigned char* mgr = PartyMgr();
                for (int i = 0; i < 3; ++i)
                {
                    void* ch = mgr ? CharByType(mgr, OrderType(mgr, i)) : nullptr;
                    int w = ch ? TlWho(ch, humans) : 4;
                    if (w < 4) { g_HitS[w] += g_HitNB[i]; g_HitHPS[w] += g_HitHPB[i]; }
                    g_HitNB[i] = 0; g_HitHPB[i] = 0; g_HitN[i] = 0; g_HitHP[i] = 0;
                }
            }
            ++g_TlBattles;
            for (int k = 0; k < 5; ++k) g_TlSession[k] += g_TlBattle[k];
            for (int p = 0; p < 3; ++p) for (int k = 0; k < 2; ++k) g_TlMoveS[p][k] += g_TlMoveB[p][k];
            TlShares(g_TlSession, s, sizeof(s)); TlMoving(g_TlMoveS, mv, sizeof(mv));
            CoopLog("TARGETLOG: session so far (%d battle(s)) - time enemies spent on each: %s%s | hits taken: P1 %d / %lld HP, P2 %d / %lld, P3 %d / %lld, AI %d / %lld",
                g_TlBattles, s, mv, g_HitS[0], g_HitHPS[0], g_HitS[1], g_HitHPS[1], g_HitS[2], g_HitHPS[2], g_HitS[3], g_HitHPS[3]);
            for (double& v : g_TlBattle) v = 0;
            for (double& v : g_TlTime) v = 0;
            for (auto& r : g_TlMoveB) r[0] = r[1] = 0;
            for (auto& r : g_TlMove) r[0] = r[1] = 0;
            for (auto& t : g_Tl) t = TlEnemy();
        }
        wasBattle = false; ratesDone = false; lastT = 0;
        return;
    }
    if (!wasBattle)
    {
        wasBattle = true; sumAt = now + 30000; lastT = now; CoopLog("TARGETLOG: battle - logging enemy target changes (log only)");
        for (int i = 0; i < 3; ++i) { g_HitN[i] = 0; g_HitHP[i] = 0; g_HitNB[i] = 0; g_HitHPB[i] = 0; }   // (hits before the battle state - field damage - don't count)
    }
    if (now - scanAt < 100 || now - seenAt > 300) return;   // 10 times a second, while the battle state is on
    double dt = (lastT && now > lastT) ? (now - lastT) / 1000.0 : 0.0; if (dt > 0.5) dt = 0.5;
    lastT = now; scanAt = now;
    {   // is each player's character moving? (> 1.5 m/s over this sample; P1 = the leader's character)
        static float lastPos[3][3]; static void* lastCh[3] = {nullptr, nullptr, nullptr};
        void* chs[3] = {LeaderChar(), humans[0], humans[1]};
        for (int p = 0; p < 3; ++p)
        {
            float pos[3];
            if (!chs[p] || dt <= 0.0 || !ActorLoc(chs[p], pos)) { lastCh[p] = nullptr; continue; }
            if (lastCh[p] == chs[p])
            {
                float dx = pos[0] - lastPos[p][0], dy = pos[1] - lastPos[p][1];
                bool moving = sqrtf(dx * dx + dy * dy) > 150.f * (float)dt;
                g_TlMove[p][1] += dt; g_TlMoveB[p][1] += dt;
                if (moving) { g_TlMove[p][0] += dt; g_TlMoveB[p][0] += dt; }
            }
            lastCh[p] = chs[p]; memcpy(lastPos[p], pos, sizeof(pos));
        }
    }
    void* en[24]; int ne = CollectEnemies(en, 24);
    unsigned char* mgr = PartyMgr();
    for (int i = 0; i < ne; ++i)
    {
        void* e = en[i];
        if (!e || !Readable((unsigned char*)e + OFF_BATTLE_UNIT, 8)) continue;
        unsigned char* unit = *(unsigned char**)((unsigned char*)e + OFF_BATTLE_UNIT);
        if (!unit || !Readable(unit + OFF_UNIT_TARGET, 8)) continue;
        int idx, ser; if (!MakeWeak(e, &idx, &ser)) continue;
        TlEnemy* t = nullptr; TlEnemy* fr = nullptr;
        for (auto& s : g_Tl) { if (s.idx == idx && s.ser == ser) { t = &s; break; } if (!fr && (s.idx < 0 || !ResolveWeak(s.idx, s.ser))) fr = &s; }
        if (!t) { if (!fr) continue; *fr = TlEnemy(); fr->idx = idx; fr->ser = ser; fr->since = now; t = fr; }
        if (!ratesDone) { ratesDone = true; TlLogRates(e); }
        // the target: unit+0x38 = the target's handle = that character's +0xCB8
        void* h = *(void**)(unit + OFF_UNIT_TARGET);
        void* tgt = nullptr;
        for (int k = 0; h && k < 3 && !tgt; ++k)
        {
            void* c = mgr ? CharByType(mgr, OrderType(mgr, k)) : nullptr;
            if (c && Readable((unsigned char*)c + 0xCB8, 8) && *(void**)((unsigned char*)c + 0xCB8) == h) tgt = c;
        }
        int who = tgt ? TlWho(tgt, humans) : 4;
        g_TlTime[who] += dt; g_TlBattle[who] += dt;
        if (tgt != t->lastTgt)
        {
            if (g_TlLines < 1500)
            {
                ++g_TlLines;
                char en_[96] = "?", a[24] = "nobody", hate[200];
                NameOf(e, en_, sizeof(en_));
                if (tgt) TlShort(tgt, a, sizeof(a));
                TlHate(TlCtrl(e, *t), hate, sizeof(hate));
                CoopLog("TARGETLOG: %s -> %s (%s), was %s (%s) for %.1f s | hate %s", en_, a, kTlWho[who], t->lastName, kTlWho[(t->lastWho >= 0 && t->lastWho <= 4) ? t->lastWho : 4],
                    (now - t->since) / 1000.0, hate);
                if (g_TlLines == 1500) CoopLog("TARGETLOG: 1500 target changes logged - no more this session (the 30 s shares go on)");
            }
            t->lastTgt = tgt; t->since = now; t->lastWho = who;
            if (tgt) TlShort(tgt, t->lastName, sizeof(t->lastName)); else snprintf(t->lastName, sizeof(t->lastName), "nobody");
        }
    }
    if (now >= sumAt)
    {
        sumAt = now + 30000;
        char s[200], mv[80], ht[240]; TlShares(g_TlTime, s, sizeof(s)); TlMoving(g_TlMove, mv, sizeof(mv));
        TlHits(g_HitN, g_HitHP, humans, ht, sizeof(ht));
        CoopLog("TARGETLOG: last 30 s - time enemies spent on each: %s%s%s", s, mv, ht);
        TsSummary();   // f1.212: which code wrote the enemies' targets (enemies/22c)
        for (int i = 0; i < 3; ++i) { g_HitN[i] = 0; g_HitHP[i] = 0; }
        {   // f1.211 (the Airbuster's target-change lines showed an EMPTY hate list): each enemy's hate scores every 30 s - "empty"
            // = it keeps none, so StayOnHitChance / TurnOnHeavyDamage / EnemiesTreatHumansLikePlayer1 (all hate-score rules)
            // can't steer it; its own script picks the target
            int shown = 0;
            for (auto& t : g_Tl)
            {
                if (t.idx < 0 || shown >= 6) continue;
                void* e = ResolveWeak(t.idx, t.ser);
                if (!e) continue;
                char en_[96] = "?", hate[200]; NameOf(e, en_, sizeof(en_));
                TlHate(TlCtrl(e, t), hate, sizeof(hate));
                CoopLog("TARGETLOG: %s hate scores now: %s (target %s)", en_, hate, t.lastName);
                ++shown;
            }
        }
        for (double& v : g_TlTime) v = 0;
        for (auto& r : g_TlMove) r[0] = r[1] = 0;
    }
}
