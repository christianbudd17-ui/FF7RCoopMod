// =================================================================================
// f1.217 BOSSRANGE / BOSSHIT - LOG ONLY (the author, after the f1.214 Ghoul fight: Aerith was its target 37 % of the time
// but took the fewest hits - "it would be BAD if the ghoul was attempting physical attacks at a character who was not in
// range"; "A log would probably help us by giving us actual data"). The mod gives a boss's script a person instead of the
// leader (enemies/22d); if some of its attack choices still look at the leader, it would pick an attack for player 1's
// distance and aim it at someone out of reach. Nothing is changed here. Measured:
//  - 4 times a second for each boss: its actual target (unit +0x38, whoever wrote it), the flat distance to it and whether
//    the boss faces it (within 45 degrees);
//  - per target stint (one line when the target changes / the battle ends): how long, the distance at the start, the
//    closest it got (and after how long), at the end; the share of the time within 3 m / 3 - 8 m / over 8 m; facing it;
//    its landed contacts (OnGiveDamage: a hit, or a dodged / guarded one) on the target and on someone else;
//  - every contact (BOSSHIT): on whom, dodged / guarded or for how much, its target then, both distances;
//  - every 30 s and at the battle's end: per character as its target - seconds, average distance, within 3 m, facing,
//    contacts on them.
// Remove once the boss range answer is in (removal checklist).
// =================================================================================
static void BossActContact(void* unit, void* tgt, bool dodged);   // enemies/22g (f1.226, log only)
static constexpr float BR_NEAR_CM = 300.f, BR_MID_CM = 800.f, BR_FACE_DEG = 45.f;
struct BrTally { double t = 0, dist = 0, nearT = 0, faceT = 0; int contacts = 0; };   // per party slot, as the target
struct BossRange
{
    void* unit = nullptr; void* enemy = nullptr;
    void* tgt = nullptr; ULONGLONG since = 0;               // the current stint
    char tgtName[24] = "nobody";                            // f1.239: read when the stint starts (no reads of a freed character later)
    float d0 = -1, dMin = -1, dLast = -1; ULONGLONG minAt = 0;
    double t = 0, nearT = 0, midT = 0, farT = 0, faceT = 0;
    int onTgt = 0, onTgtMiss = 0, onOther = 0;
    BrTally w[3], b[3];                                     // 30 s window, battle
    int otherW = 0, otherB = 0;                             // contacts on someone other than its target
};
static BossRange g_Br[4];
static int BrSlot(void* ch)   // party order slot 0 - 2 of a character, or -1
{
    for (int i = 0; i < 3 && GetPartyMember; ++i) if (GetPartyMember(i) == ch) return i;
    return -1;
}
static bool BrFlat(void* a, void* b, float* d, float* faceErr)   // flat distance a -> b (cm), |a's facing - direction to b|
{
    float pa[3], pb[3];
    if (!ActorLoc(a, pa) || !ActorLoc(b, pb)) return false;
    float dx = pb[0] - pa[0], dy = pb[1] - pa[1];
    *d = sqrtf(dx * dx + dy * dy);
    if (faceErr) *faceErr = fabsf(AngleDiff(atan2f(dy, dx) * RAD_TO_DEG, ActorYaw(a)));
    return true;
}
static BossRange* BrOf(void* unit, void* enemy, bool make)
{
    for (auto& r : g_Br) if (r.unit == unit) { if (enemy) r.enemy = enemy; return &r; }
    if (!make) return nullptr;
    for (auto& r : g_Br) if (!r.unit) { r = BossRange(); r.unit = unit; r.enemy = enemy; return &r; }
    return nullptr;
}
static void BrStintEnd(BossRange& r, ULONGLONG now)
{
    if (!r.tgt || r.t <= 0.01) return;
    const char* en = BossNameOf(r.unit); const char* tn = r.tgtName;   // f1.230 / f1.239: the stored names
    double tt = r.t;
    static LogRate rate;
    if (LogOk(rate, 40)) CoopLog("BOSSRANGE: %s on %s (%s) for %.1f s - distance at start %.1f m, closest %.1f m (after %.1f s), at end %.1f m; within 3 m %.0f%%, 3 - 8 m %.0f%%, over 8 m %.0f%%; facing them %.0f%% | its contacts: on them %d hit(s) + %d dodged / guarded, on someone else %d",
        en, tn, kTlWho[TlWho(r.tgt, g_AttnHuman)], (now - r.since) / 1000.0, r.d0 / 100.f, r.dMin / 100.f, r.minAt > r.since ? (r.minAt - r.since) / 1000.0 : 0.0, r.dLast / 100.f,
        100.0 * r.nearT / tt, 100.0 * r.midT / tt, 100.0 * r.farT / tt, 100.0 * r.faceT / tt, r.onTgt, r.onTgtMiss, r.onOther);
}
static void BrSummary(BossRange& r, bool battle)
{
    BrTally* w = battle ? r.b : r.w;
    const char* en = BossNameOf(r.unit);   // f1.230: the stored name
    char s[640]; size_t len = 0; double all = 0;
    for (int i = 0; i < 3; ++i) all += w[i].t;
    if (all <= 0.01) return;
    for (int i = 0; i < 3 && len < sizeof(s); ++i)
    {
        if (w[i].t <= 0.01 || !GetPartyMember) continue;
        char cn[24] = "?"; TlShort(GetPartyMember(i), cn, sizeof(cn));
        len += snprintf(s + len, sizeof(s) - len, "%s%s %.0f s (%.0f%%) avg %.1f m, within 3 m %.0f%%, facing %.0f%%, %d contact(s)", len ? " | " : "", cn,
            w[i].t, 100.0 * w[i].t / all, w[i].dist / w[i].t / 100.0, 100.0 * w[i].nearT / w[i].t, 100.0 * w[i].faceT / w[i].t, w[i].contacts);
    }
    CoopLog("BOSSRANGE: %s %s - as its target: %s | contacts on someone else than its target: %d", battle ? "battle" : "last 30 s", en, s, battle ? r.otherB : r.otherW);
    if (!battle) { for (auto& x : r.w) x = BrTally(); r.otherW = 0; }
}
// enemies/22d BossTick (4 times a second, in a battle): sample every boss
static void BossRangeTick(ULONGLONG now, double dt)
{
    if (!g_Cfg.targetLog) return;
    static ULONGLONG lastSum = 0;
    for (auto& b : g_Boss)
    {
        if (!b.unit || !b.boss || !b.enemy || now - b.seen > 1000) continue;
        BossRange* r = BrOf(b.unit, b.enemy, true);
        if (!r) continue;
        unsigned char* u = (unsigned char*)b.unit;
        void* h = Readable(u + 0x38, 8) ? *(void**)(u + 0x38) : nullptr;
        void* tgt = TsCharOfHandle(h);
        if (tgt != r->tgt)
        {
            BrStintEnd(*r, now);
            void* keepE = r->enemy; BrTally w[3], bb[3]; memcpy(w, r->w, sizeof(w)); memcpy(bb, r->b, sizeof(bb)); int ow = r->otherW, ob = r->otherB;
            *r = BossRange(); r->unit = b.unit; r->enemy = keepE; memcpy(r->w, w, sizeof(w)); memcpy(r->b, bb, sizeof(bb)); r->otherW = ow; r->otherB = ob;
            r->tgt = tgt; r->since = now;
            if (tgt) TlShort(tgt, r->tgtName, sizeof(r->tgtName));   // f1.239: alive now (just resolved from its handle)
        }
        if (!tgt) continue;
        float d = 0, fe = 180; if (!BrFlat(b.enemy, tgt, &d, &fe)) continue;
        if (r->d0 < 0) r->d0 = d;
        if (r->dMin < 0 || d < r->dMin) { r->dMin = d; r->minAt = now; }
        r->dLast = d;
        r->t += dt; if (d <= BR_NEAR_CM) r->nearT += dt; else if (d <= BR_MID_CM) r->midT += dt; else r->farT += dt;
        bool face = fe <= BR_FACE_DEG; if (face) r->faceT += dt;
        int s = BrSlot(tgt);
        if (s >= 0)
        {
            BrTally* xs[2] = {&r->w[s], &r->b[s]};
            for (BrTally* x : xs) { x->t += dt; x->dist += d * dt; if (d <= BR_NEAR_CM) x->nearT += dt; if (face) x->faceT += dt; }
        }
    }
    if (now - lastSum >= 30000) { lastSum = now; for (auto& r : g_Br) if (r.unit) BrSummary(r, false); }
}
// enemies/22d BossTick when the battle is over (before g_Boss is cleared)
static void BossRangeBattleEnd()
{
    ULONGLONG now = GetTickCount64();
    for (auto& r : g_Br) if (r.unit) { BrStintEnd(r, now); BrSummary(r, false); BrSummary(r, true); }
    for (auto& r : g_Br) r = BossRange();
}
// enemies/22 (OnGiveDamage - an enemy's attack connected: a hit, or a dodge / guard)
static void BossRangeContact(void* self, void* tgt, int dmg, bool dodged)
{
    if (!g_Cfg.targetLog || !self || !tgt) return;
    BossState* b = BossOfEnemy(self);
    if (!b) return;
    BossRange* r = BrOf(b->unit, self, true);
    if (!r) return;
    BossActContact(b->unit, tgt, dodged);   // f1.226: the move in progress
    bool onTgt = tgt == r->tgt;
    if (onTgt) { if (dodged) r->onTgtMiss++; else r->onTgt++; }
    else { r->onOther++; r->otherW++; r->otherB++; }
    int s = BrSlot(tgt);
    if (s >= 0 && onTgt) { r->w[s].contacts++; r->b[s].contacts++; }
    float dHit = -1, dTgt = -1; BrFlat(self, tgt, &dHit, nullptr); if (r->tgt) BrFlat(self, r->tgt, &dTgt, nullptr);
    char en[96] = "?", vn[24] = "?"; NameOf(self, en, sizeof(en)); TlShort(tgt, vn, sizeof(vn)); const char* tn = r->tgt ? r->tgtName : "nobody";   // f1.239: stored
    static LogRate rate;
    if (LogOk(rate, 60))
    {
        char what[32]; if (dodged) snprintf(what, sizeof(what), "dodged / guarded"); else snprintf(what, sizeof(what), "for %d", dmg);
        if (onTgt) CoopLog("BOSSHIT: %s reached %s (its target) %s - %.1f m away", en, vn, what, dHit / 100.f);
        else CoopLog("BOSSHIT: %s reached %s %s - %.1f m away; NOT its target (%s, %.1f m away)", en, vn, what, dHit / 100.f, tn, dTgt / 100.f);
    }
}
