static bool ActorLoc(void* actor, float out[3])
{
    unsigned char* a = (unsigned char*)actor;
    if (!a || !Readable(a + OFF_ROOT_COMPONENT, 8)) return false;
    unsigned char* root = *(unsigned char**)(a + OFF_ROOT_COMPONENT);
    if (!root || !Readable(root + OFF_REL_LOCATION, 0x18)) return false;
    memcpy(out, root + OFF_REL_LOCATION, 12);
    return true;
}
static float ActorYaw(void* actor)
{
    unsigned char* a = (unsigned char*)actor;
    unsigned char* root = (a && Readable(a + OFF_ROOT_COMPONENT, 8)) ? *(unsigned char**)(a + OFF_ROOT_COMPONENT) : nullptr;
    if (!root || !Readable(root + OFF_REL_ROTATION, 12)) return 0.f;
    return *(float*)(root + OFF_REL_ROTATION + 4);
}
static float AngleDiff(float a, float b) // smallest signed difference a-b in degrees, -180..180
{
    float d = a - b;
    while (d > 180.f) d -= 360.f;
    while (d < -180.f) d += 360.f;
    return d;
}

// the game's own lock-on filter (the author, f1.108 test: player 2 R3-locked EB0025_00_Projection_Standard_C_0 in the
// Airbuster fight - "not an enemy that can be targeted by P1"). RE 2026-10-23: player 1's lock-on / soft target picks from
// target objects, not characters - 0x9B7DE0 -> 0x9C9F50 -> 0x9FF910 builds the list: for every battle character with a unit,
// unit->vfunc[0](side, 0) (side = 0x96CA90(the player's unit), a bool) -> the character's own target (chara+0xCB8) + its
// parts that pass; else, if unit+0x543, only its parts (unit+0x5C0 -> +0x90 array of 16-byte shared pointers, each part's
// vfunc[0](side, 0)). IsTargetable 0x954C00 alone (what the mod used) lets things through that never get a target object.
// Same calls the game makes for every battle character (slot 0 of the unit / part vtables is that check for all classes).
typedef bool (*UnitSide_t)(void* unit);                       // 0x96CA90
typedef bool (*TgtCheck_t)(void* obj, unsigned char side, unsigned char flag);   // vtable slot 0 of units / part targets
static bool InGameCode(void* f) { uintptr_t a = (uintptr_t)f; return a > g_Base + 0x1000 && a < g_Base + 0x5000000; }
static bool LockableByPlayers(void* chara, unsigned char side)
{
    unsigned char* unit = *(unsigned char**)((unsigned char*)chara + OFF_BATTLE_UNIT);
    if (!unit || !Readable(unit, 0x5C8)) return true;   // can't tell - keep it (as before)
    void** vt = *(void***)unit;
    if (!vt || !Readable(vt, 8) || !InGameCode(vt[0])) return true;
    if (((TgtCheck_t)vt[0])(unit, side, 0)) return true;
    if (!unit[0x543]) return false;
    unsigned char* comp = *(unsigned char**)(unit + 0x5C0);
    if (!comp || !Readable(comp + 0x90, 0x10)) return false;
    unsigned char* arr = *(unsigned char**)(comp + 0x90); int n = *(int*)(comp + 0x98);
    if (!arr || n <= 0 || n > 64 || !Readable(arr, (size_t)n * 16)) return false;
    for (int i = 0; i < n; ++i)
    {
        void* obj = *(void**)(arr + i * 16); unsigned char* ctl = *(unsigned char**)(arr + i * 16 + 8);
        if (!obj || !ctl || !Readable(ctl, 0x10) || *(int*)(ctl + 8) <= 0 || !Readable(obj, 8)) continue;
        void** pv = *(void***)obj;
        if (!pv || !Readable(pv, 8) || !InGameCode(pv[0])) continue;
        if (((TgtCheck_t)pv[0])(obj, side, 0)) return true;
    }
    return false;
}
// (the author, f1.180 test: P3's reticle showed nothing for ~1 s after their target died; then "the reticle move
// should happen from the targets list, not the enemies list. Similar to ... the Airbuster fight"): player 1's own target
// pick (0x9B7DE0) doesn't use the enemy list. It asks 0x9C9F50(-, &list, side, -, 0) for the TARGET list (TWeakPtr
// entries: each enemy's own target object chara+0xCB8, plus its parts), then keeps only entries whose object says
// "targetable now": vfunc +8 (object, side, 0) (0x9B84F7; the game's reticle checks its current target the same way,
// 0x9B9376). A defeated enemy fails that at once - the game's reticle moves on immediately. Players 2/3 now pick from the
// same list with the same check (a part counts as its enemy: vfunc +0x20 = the character). -1 = the call isn't usable.
static bool EnemyHP(void* chara, int* hp, int* mx);   // (marker/19)
struct SpEntry { void* obj; unsigned char* ctl; };
struct SpArr { SpEntry* data; int num; int max; };
static bool SpPin(unsigned char* ctl)   // TSharedPtr strong reference +1 if it's still alive (as the game: cmpxchg on +8)
{
    if (!ctl || !Readable(ctl, 0x10)) return false;
    volatile LONG* sc = (volatile LONG*)(ctl + 8);
    for (int k = 0; k < 64; ++k) { LONG v = *sc; if (v <= 0) return false; if (InterlockedCompareExchange(sc, v + 1, v) == v) return true; }
    return false;
}
static void SpUnpin(unsigned char* ctl)   // strong -1; last one = the object goes (vfunc 0), then the weak share (as 0x9B940D)
{
    if (InterlockedDecrement((volatile LONG*)(ctl + 8)) != 0) return;
    void** vt = *(void***)ctl; ((void (*)(void*))vt[0])(ctl);
    if (InterlockedDecrement((volatile LONG*)(ctl + 0xC)) == 0) ((void (*)(void*, int))vt[1])(ctl, 1);
}
static void WpRelease(unsigned char* ctl)   // weak -1; last one = the counter block goes (vfunc 8, 1)
{
    if (!ctl || !Readable(ctl, 0x10)) return;
    if (InterlockedDecrement((volatile LONG*)(ctl + 0xC)) == 0) { void** vt = *(void***)ctl; ((void (*)(void*, int))vt[1])(ctl, 1); }
}
static int GameTargetChars(void** out, int maxOut, unsigned char side, int* nEntries)
{
    typedef void (*TgtList_t)(void*, SpArr*, unsigned char, void*, void*);
    static int ok = 0;
    if (ok == 0)
    {   // the code still looks like the RE'd version (first bytes of 0x9C9F50 / 0x9FF910)
        static const unsigned char ex1[9] = {0x48,0x89,0x5C,0x24,0x10,0x56,0x48,0x83,0xEC};
        static const unsigned char ex2[10] = {0x4C,0x89,0x4C,0x24,0x20,0x41,0x54,0xB8,0xB0,0x14};
        ok = (memcmp((void*)(g_Base + 0x9C9F50), ex1, 9) == 0 && memcmp((void*)(g_Base + 0x9FF910), ex2, 10) == 0) ? 1 : -1;
        CoopLog("TGTFILTER: the game's target list (0x9C9F50) %s", ok == 1 ? "found - players 2/3 pick from it, like player 1" : "NOT as expected (other game version?) - the enemy list is used");
    }
    if (ok != 1) return -1;
    SpArr arr = {nullptr, 0, 0};
    ((TgtList_t)(g_Base + 0x9C9F50))(nullptr, &arr, side, nullptr, nullptr);
    int n = 0;
    if (nEntries) *nEntries = arr.num;
    for (int i = 0; i < arr.num && arr.data; ++i)
    {
        SpEntry& e = arr.data[i];
        if (e.obj && SpPin(e.ctl))
        {
            void** vt = Readable(e.obj, 8) ? *(void***)e.obj : nullptr;
            if (vt && Readable(vt, 0x28) && InGameCode(vt[1]))
            {   // research (once per kind): which check each kind of target object has
                static void* seen[16]; static int ns = 0;
                bool known = false; for (int k = 0; k < ns; ++k) if (seen[k] == (void*)vt) known = true;
                if (!known && ns < 16) { seen[ns++] = (void*)vt; CoopLog("TGTFILTER: target object kind vtable 0x%llX - check 0x%llX, character 0x%llX",
                    (unsigned long long)((uintptr_t)vt - g_Base), (unsigned long long)((uintptr_t)vt[1] - g_Base), (unsigned long long)((uintptr_t)vt[4] - g_Base)); }
            }
            if (vt && Readable(vt, 0x28) && InGameCode(vt[1]) && InGameCode(vt[4]) && ((TgtCheck_t)vt[1])(e.obj, side, 0))
            {
                void* ch = ((void* (*)(void*))vt[4])(e.obj);   // the character (a part's own enemy)
                bool dup = false; for (int k = 0; k < n; ++k) if (out[k] == ch) dup = true;
                if (ch && !dup && n < maxOut) out[n++] = ch;
            }
            SpUnpin(e.ctl);
        }
        WpRelease(e.ctl);
    }
    if (arr.data) ((FMemoryFree_t)(g_Base + RVA_FMEMORY_FREE))(arr.data);
    return n;
}
// f1.248 (seats/24b QUIETLEAD): the battle's world is there (the same checks CollectEnemies starts with) - true also during
// a boss's scripted camera shot, when the battle menu is away and InBattleNow() reads false (f1.246 test 20:58:33)
static bool BattleWorldOn()
{
    void* gA = *(void**)(g_Base + RVA_BATTLE_GLOBAL_A);
    void* gB = *(void**)(g_Base + RVA_BATTLE_GLOBAL_B);
    if (!gA || !gB || !Readable(gA, 0x670) || !Readable((unsigned char*)gA + 0xDF10, 0x10) || !Readable((unsigned char*)gB + 0x170, 8)) return false;
    return *(void**)((unsigned char*)gB + 0x170) != nullptr;
}
// Fills 'out' with up to 'maxOut' targetable enemies. Returns the count (0 when not in battle).
static int CollectEnemies(void** out, int maxOut)
{
    void* gA = *(void**)(g_Base + RVA_BATTLE_GLOBAL_A);
    void* gB = *(void**)(g_Base + RVA_BATTLE_GLOBAL_B);
    if (!gA || !gB || !Readable(gA, 0x670) || !Readable((unsigned char*)gA + 0xDF10, 0x10) || !Readable((unsigned char*)gB + 0x170, 8)) return 0;
    if (!*(void**)((unsigned char*)gB + 0x170)) return 0;
    TArrayRaw arr{};
    ((GetEnemyMembers_t)(g_Base + RVA_GET_ENEMY_MEMBERS))(&arr);
    int n = 0, nAll = 0;
    IsTargetable_t isT = (IsTargetable_t)(g_Base + RVA_IS_TARGETABLE);
    // the side the players are on, from the leader's unit (0x96CA90, as the game's lock-on does)
    void* ld = LeaderChar(); unsigned char* lu = ld ? *(unsigned char**)((unsigned char*)ld + OFF_BATTLE_UNIT) : nullptr;
    unsigned char side = lu ? (unsigned char)((UnitSide_t)(g_Base + 0x96CA90))(lu) : 1;
    {   // the game's target list first (player 1's own rules); the enemy list below only if it can't be used
        void* gt[64]; int ne = 0;
        int gn = GameTargetChars(gt, 64, side, &ne);
        if (gn >= 0)
        {
            for (int i = 0; i < gn && n < maxOut; ++i)
            {   // still an enemy of the battle (the list can hold other things for other sides) and not down
                bool inBattle = false; for (int k = 0; k < arr.num && arr.data; ++k) if (arr.data[k] == gt[i]) inBattle = true;
                int hp = 0, mx = 0;
                if (!inBattle || (EnemyHP(gt[i], &hp, &mx) && mx > 0 && hp <= 0)) continue;
                out[n++] = gt[i];
            }
            static int lastN = -1, lastE = -1; static LogRate logs;
            if ((n != lastN || arr.num != lastE) && g_DetailedLog && LogOk(logs, 30))
            {
                lastN = n; lastE = arr.num;
                CoopLog("TGTFILTER: the game's target list - %d entr(ies), %d enemy(ies) targetable now (of %d in the battle)", ne, n, arr.num);
            }
            if (arr.data) ((FMemoryFree_t)(g_Base + RVA_FMEMORY_FREE))(arr.data);
            return n;
        }
    }
    void* all[64];
    for (int i = 0; i < arr.num && nAll < 64; ++i)
    {
        void* c = arr.data ? arr.data[i] : nullptr;
        if (!c || !Readable((unsigned char*)c + OFF_BATTLE_UNIT, 8)) continue;
        if (!*(void**)((unsigned char*)c + OFF_BATTLE_UNIT)) continue;
        if (!isT(c)) continue;
        all[nAll++] = c;
        if (!LockableByPlayers(c, side))
        {   // first time per enemy (weak slot) only
            static int skIdx[32], skSer[32], skN = 0, logs = 0;
            int wi, ws; bool seen = false;
            if (MakeWeak(c, &wi, &ws)) { for (int k = 0; k < (skN < 32 ? skN : 32); ++k) if (skIdx[k] == wi && skSer[k] == ws) seen = true;
                if (!seen) { skIdx[skN % 32] = wi; skSer[skN % 32] = ws; ++skN; } }
            if (!seen && logs < 40) { ++logs; char nm[96]; NameOf(c, nm, sizeof(nm)); CoopLog("TGTFILTER: %s left out - player 1's lock-on can't pick it either (side %d)", nm, side); }
            continue;
        }
        if (n < maxOut) out[n++] = c;
    }
    if (n == 0 && nAll > 0)
    {   // safety: the filter left nothing although the game says there are targets - use them all (as before f1.110)
        static ULONGLONG lastLog = 0; ULONGLONG t = GetTickCount64();
        if (t - lastLog > 10000) { lastLog = t; CoopLog("TGTFILTER: no enemy passed the lock-on check (of %d) - using all of them", nAll); }
        for (int i = 0; i < nAll && n < maxOut; ++i) out[n++] = all[i];
    }
    if (arr.data) ((FMemoryFree_t)(g_Base + RVA_FMEMORY_FREE))(arr.data);
    return n;
}

static void LogTarget(const char* why, void* t, float dist)
{
    char nm[128] = "none";
    if (t) NameOf(t, nm, sizeof(nm));
    CoopLog("P%d target %s -> %s (dist %.0f, %s)", g_Player->num, why, nm, dist, g_Player->tgtLocked ? "LOCKED" : "soft");
}

static float P2ControlYaw(unsigned char* pc);
static bool EnemyHP(void* chara, int* hp, int* mx);   // (marker/19)
static void UpdateP2Targeting(unsigned char* pc, void* p2)
{
    g_Player->drawValid = false;
    unsigned char* unit = p2 ? *(unsigned char**)((unsigned char*)p2 + OFF_BATTLE_UNIT) : nullptr;
    // Refresh the enemy list at most every 100ms (kept as weak refs in between). v6.0: per player.
    // (review D7 / F7): ONE game list for everyone - refreshed when it is 100 ms old, each player copies the newest one
    // (before, players 2 and 3 each asked the game every 100 ms for the same list). tgtListScan = the copy this player has.
    int* wIdx = g_Player->tgtListIdx; int* wSer = g_Player->tgtListSer; int& wN = g_Player->tgtListN;
    static int shIdx[64], shSer[64], shN = 0; static ULONGLONG shAt = 0, shGen = 0;
    ULONGLONG nowScan = GetTickCount64();
    if (!unit || !pc) { wN = 0; g_EnemyN = 0; g_Player->tgtListScan = 0; }   // a player without a battle unit empties the camera list (as before); copies again when back
    else
    {
        if (!shGen || nowScan - shAt >= 100)
        {
            shAt = nowScan; ++shGen;
            void* fresh[64];
            int fn = CollectEnemies(fresh, 64);
            shN = 0;
            for (int i = 0; i < fn; ++i) if (MakeWeak(fresh[i], &shIdx[shN], &shSer[shN])) shN++;
        }
        if (g_Player->tgtListScan != shGen)
        {
            g_Player->tgtListScan = shGen;
            wN = shN; memcpy(wIdx, shIdx, sizeof(int) * shN); memcpy(wSer, shSer, sizeof(int) * shN);
            g_EnemyN = wN; memcpy(g_EnemyIdx, wIdx, sizeof(int) * wN); memcpy(g_EnemySer, wSer, sizeof(int) * wN); // for the camera
        }
    }
    void* enemies[64];
    int n = 0;
    for (int i = 0; i < wN; ++i) { void* e = ResolveWeak(wIdx[i], wSer[i]); if (e) enemies[n++] = e; }
    if (n != g_Player->tgtLastEnemyCount)
    {
        g_Player->tgtLastEnemyCount = n;
        if (n > 0 && g_DetailedLog)   // the list is only built when the detailed log writes it
        {
            char line[900]; int pos = 0;
            float me[3] = {0, 0, 0}; ActorLoc(p2, me);
            for (int i = 0; i < n && pos < 820; ++i)
            {
                char nm[96]; NameOf(enemies[i], nm, sizeof(nm));
                float e[3] = {0, 0, 0}; ActorLoc(enemies[i], e);
                float d = sqrtf((e[0]-me[0])*(e[0]-me[0]) + (e[1]-me[1])*(e[1]-me[1]));
                pos += snprintf(line + pos, sizeof(line) - pos, "%s(%.0f) ", nm, d);
            }
            CoopLog("P%d targeting: %d enemies: %s", g_Player->num, n, line);
        }
        else if (n == 0) CoopLog("P%d targeting: no enemies (not in battle?)", g_Player->num);
    }
    if (n == 0)
    {
        if (g_Player->tgtWasActive) { g_Player->tgtLocked = false; g_Player->tgtIdx = -1; g_Player->tgtAppliedIdx = -1; }
        g_Player->tgtWasActive = false;
        g_Player->tgtR3Was = (g_Player->padNow & g_Player->cfg->bLock) != 0;
        return;
    }
    g_Player->tgtWasActive = true;

    float me[3]; if (!ActorLoc(p2, me)) return;
    float camYaw = P2ControlYaw(pc);   // the PiP view while it shows

    // Which way is player 2 "looking"? Left stick (camera-relative) if pushed, else Barret's facing.
    float aimYaw;
    if (g_Player->stickX * g_Player->stickX + g_Player->stickY * g_Player->stickY > 0.09f)
        aimYaw = camYaw + atan2f(g_Player->stickX, g_Player->stickY) * RAD_TO_DEG;
    else
        aimYaw = ActorYaw(p2);

    // Current target still valid?
    void* cur = (g_Player->tgtIdx >= 0) ? ResolveWeak(g_Player->tgtIdx, g_Player->tgtSerial) : nullptr;
    int curI = -1;
    for (int i = 0; cur && i < n; ++i) if (enemies[i] == cur) { curI = i; break; }
    if (curI < 0)
    {
        if (cur || g_Player->tgtLocked) CoopLog("P%d target lost (gone or defeated)%s", g_Player->num, g_Player->tgtLocked ? " - lock released" : "");
        cur = nullptr; g_Player->tgtLocked = false; g_Player->tgtIdx = -1;
    }

    float dist[64], ang[64];
    for (int i = 0; i < n; ++i)
    {
        float e[3] = {me[0], me[1], me[2]}; ActorLoc(enemies[i], e);
        float dx = e[0] - me[0], dy = e[1] - me[1];
        dist[i] = sqrtf(dx * dx + dy * dy);
        ang[i] = (dist[i] > 1.f) ? atan2f(dy, dx) * RAD_TO_DEG : aimYaw;
    }

    // R3: toggle lock on whatever we're targeting now.
    bool r3 = (g_Player->padNow & g_Player->cfg->bLock) != 0;
    if (r3 && !g_Player->tgtR3Was)
    {
        if (g_Player->tgtLocked) { g_Player->tgtLocked = false; CoopLog("P%d R3: lock OFF", g_Player->num); }
        else if (cur)    { g_Player->tgtLocked = true;  LogTarget("R3 lock ON", cur, dist[curI]); }
    }
    g_Player->tgtR3Was = r3;

    // Right stick flick: switch to the nearest enemy to the left/right of the current one
    // (as seen from the camera), then lock it.
    int flick = 0;
    if (g_Player->camStick && (g_Player->num == 2 || g_Player->num == 3)) { /* v6.7: right stick is turning the PiP camera (not locked) */ }
    else if (g_Player->tgtFlickDir == 0 && (g_Player->rStickX > 0.6f || g_Player->rStickX < -0.6f)) { g_Player->tgtFlickDir = g_Player->rStickX > 0 ? 1 : -1; flick = g_Player->tgtFlickDir; }
    else if (g_Player->tgtFlickDir != 0 && g_Player->rStickX < 0.3f && g_Player->rStickX > -0.3f) g_Player->tgtFlickDir = 0;
    if (flick && n > 1)
    {
        // Screen-ish angle: positive = to the right of the camera. (UE yaw grows clockwise from above.)
        float refA = cur ? AngleDiff(ang[curI], camYaw) : 0.f;
        int best = -1; float bestD = 1e9f;
        for (int i = 0; i < n; ++i)
        {
            if (enemies[i] == cur) continue;
            float d = AngleDiff(AngleDiff(ang[i], camYaw), refA) * (float)flick; // >0 = in the flick direction
            if (d <= 0.f) d += 360.f;  // wrap around
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best >= 0)
        {
            cur = enemies[best]; curI = best; g_Player->tgtLocked = true;
            MakeWeak(cur, &g_Player->tgtIdx, &g_Player->tgtSerial);
            LogTarget(flick > 0 ? "switch RIGHT" : "switch LEFT", cur, dist[best]);
        }
    }

    // Soft targeting.
    if (!g_Player->tgtLocked)
    {
        int best = -1; float bestS = 1e12f;
        for (int i = 0; i < n; ++i)
        {
            float a = AngleDiff(ang[i], aimYaw); if (a < 0) a = -a;
            float s = dist[i] * (1.f + 2.f * a / 180.f); // up to 3x "farther" when directly behind
            if (s < bestS) { bestS = s; best = i; }
        }
        if (best >= 0 && enemies[best] != cur)
        {
            float curS = 1e12f;
            if (curI >= 0) { float a = AngleDiff(ang[curI], aimYaw); if (a < 0) a = -a; curS = dist[curI] * (1.f + 2.f * a / 180.f); }
            if (bestS < curS * 0.75f) // hysteresis
            {
                cur = enemies[best]; curI = best;
                MakeWeak(cur, &g_Player->tgtIdx, &g_Player->tgtSerial);
                LogTarget("soft", cur, dist[best]);
            }
        }
    }

    // Push it into Barret's battle unit: on change, and every 250ms in case something else
    // overwrote it. The game's setter ignores us while an ability has the target locked.
    g_Player->drawValid = (cur != nullptr);
    if (!cur) return;
    ULONGLONG now = GetTickCount64();
    bool changed = (g_Player->tgtIdx != g_Player->tgtAppliedIdx || g_Player->tgtSerial != g_Player->tgtAppliedSerial);
    if (changed || now - g_Player->tgtLastApply >= 250)
    {
        if (changed && Readable(unit + OFF_UNIT_TARGETLOCK, 4) && *(int*)(unit + OFF_UNIT_TARGETLOCK) != 0)
            return; // mid-ability: try again next frame
        ((UnitSetTarget_t)(g_Base + RVA_UNIT_SET_TARGET))(unit, cur);
        g_Player->tgtAppliedIdx = g_Player->tgtIdx; g_Player->tgtAppliedSerial = g_Player->tgtSerial;
        g_Player->tgtLastApply = now;
        if (changed)
        {
            static LogRate nLogged;
            if (LogOk(nLogged, 20)) { CoopLog("P%d target applied to their character's battle unit (unit=%p, target slot now %p)", g_Player->num, unit, *(void**)(unit + OFF_UNIT_TARGET)); }
        }
    }
}

