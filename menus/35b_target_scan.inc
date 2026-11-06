// =================================================================================
// Players 2/3's marker on the target they pick in their own menu + player 1's reticle kept out of their windows
// =================================================================================
// f1.37-f1.45 (2026-10-20/21): run-time scans (TGTSCAN) found nothing; static RE of the exe found the game's own
// answer (below). f1.46 (clean-up, the author 2026-10-21: "Marker and reticle hiding should be permanent"): both always
// on (the OwnMenuTargetCursor / HideP1ReticleInWindows ini keys are gone), the scans / learning / TGTDIAG / TGTPICK DIAG
// code removed.
typedef void* (*GetMenuTarget_t)(void* bcm, void* out);
// region-cached Readable (thousands of reads per scan)
static bool TsReadable(const void* p, size_t n)
{
    static uintptr_t lo = 0, hi = 0;
    uintptr_t a = (uintptr_t)p;
    if (a >= lo && a + n <= hi) return true;
    if (a < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return false;
    lo = (uintptr_t)mbi.BaseAddress; hi = lo + mbi.RegionSize;
    return a + n <= hi;
}
// the target being picked, from the game's own code (static RE 2026-10-21, exe from the author's zip):
//   0x14206E0 GetMenuTarget(bcm, TWeakPtr<Target>* out) - what the game's reticle shows while a command menu picks a
//   target (0x9B92F6 calls it on player 1's command system, menu manager 0x59CA970 +0x98). It reads that command
//   system's own UI states (bcm+0x18 stack, +0x398 state objects; state 9 = choosing a target, its +0x80 virtual
//   0x13D8BE0 returns the highlighted entry of its target list, entries 0x38 bytes, target at +0x28). Called on
//   players 2/3's own system it gives THEIR pick. A target object is a character's own handle: chara+0xCB8 (the
//   value UnitSetTarget copies), so it is matched against every enemy / party member's +0xCB8.
//   The returned TWeakPtr holds one weak reference: released right away (controller +0xC; destroy via vtable +8).
static void* TsPickedChar(unsigned char* bcm)
{
    if (!bcm || !g_Base) return nullptr;
    static int ok = 0;   // the function's first bytes must be this game version's
    if (ok == 0)
    {
        static const unsigned char ex[16] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
        ok = memcmp((void*)(g_Base + 0x14206E0), ex, 16) == 0 ? 1 : -1;
        CoopLog("TGTPICK: the game's menu-target function %s", ok == 1 ? "found - players 2/3's marker follows the target they pick" : "NOT found (other game version) - no marker on the picked target");
    }
    if (ok != 1) return nullptr;
    alignas(16) void* out[2] = {nullptr, nullptr};
    ((GetMenuTarget_t)(g_Base + 0x14206E0))(bcm, out);
    void* obj = out[0]; unsigned char* ctrl = (unsigned char*)out[1];
    void* found = nullptr;
    if (obj && ctrl && *(volatile LONG*)(ctrl + 8) > 0)
    {
        void* en[24]; int ne = CollectEnemies(en, 24);
        for (int i = 0; i < ne && !found; ++i) if (en[i] && TsReadable((unsigned char*)en[i] + 0xCB8, 8) && *(void**)((unsigned char*)en[i] + 0xCB8) == obj) found = en[i];
        unsigned char* pm = PartyMgr();
        for (int i = 0; pm && i < 3 && !found; ++i)
        {
            int t = OrderType(pm, i); void* c = t >= 0 ? CharByType(pm, t) : nullptr;
            if (c && TsReadable((unsigned char*)c + 0xCB8, 8) && *(void**)((unsigned char*)c + 0xCB8) == obj) found = c;
        }
    }
    if (ctrl && InterlockedDecrement((volatile LONG*)(ctrl + 0xC)) == 0)
    {
        void** vt = *(void***)ctrl;
        ((void (*)(void*, int))vt[1])(ctrl, 1);
    }
    return found;
}
// f1.44's diag - player 1's picks come through UI state 8 (index 0..4 as he moves), player 2's state 8 had
// index -1: the list's highlighted index comes from the FOCUS (0x13C9F30 -> 0x1526210 reads the focus manager,
// menu manager 0x59CA970 +0x10 = FocusMgr), and player 2's focus only exists inside their own focus context. So
// their pick is read right after their own command system's update, still inside their context (passes/12 BcmAfter2),
// and kept as a weak reference for the marker.
static int g_TsPickIdx[4] = {-1, -1, -1, -1}, g_TsPickSer[4] = {}; static ULONGLONG g_TsPickT[4] = {};
static void TsCapturePick(int p, unsigned char* bcm)
{
    if (p < 2 || p > 3) return;
    p &= 3;
    void* c = nullptr;
    if (bcm && TsReadable(bcm + BCM_UI_STATE, 4) && *(int*)(bcm + BCM_UI_STATE) >= BCM_UI_MENU_OPEN) c = TsPickedChar(bcm);
    int idx = -1, ser = 0;
    if (c && !MakeWeak(c, &idx, &ser)) c = nullptr;
    if (idx != g_TsPickIdx[p] || ser != g_TsPickSer[p])
    {
        static int logs = 0;
        if (logs < 80)
        {
            ++logs; char nm[96] = "nothing (not picking a target)";
            if (c) { NameOf(c, nm, sizeof(nm)); const wchar_t* en = EnemyNameOf(c); if (en) { char e8[64]; int j = 0; for (; en[j] && j < 63; ++j) e8[j] = en[j] < 128 ? (char)en[j] : '?'; e8[j] = 0; size_t L = strlen(nm); snprintf(nm + L, sizeof(nm) - L, " \"%s\"", e8); } }
            CoopLog("TGTPICK: player %d is picking %s%s", p, nm, c ? " - their marker shows it" : "");
        }
    }
    g_TsPickIdx[p] = idx; g_TsPickSer[p] = ser; g_TsPickT[p] = GetTickCount64();
}
// players 2/3's marker target while their own menu is open and picking a target
static void* TsMenuTarget(int p)
{
    if (p < 2 || p > 3 || !InBattleNow()) return nullptr;
    p &= 3;
    if (g_TsPickIdx[p] < 0 || GetTickCount64() - g_TsPickT[p] > 300) return nullptr;   // stale: their system didn't update
    unsigned char* b = BcmOf(p);
    if (!b || !TsReadable(b + BCM_UI_STATE, 4) || *(int*)(b + BCM_UI_STATE) < BCM_UI_MENU_OPEN) return nullptr;   // their menu is closed
    return ResolveWeak(g_TsPickIdx[p], g_TsPickSer[p]);
}
// the game's one reticle widget (BattleTarget_New_C, not the mod's copy) and whether player 1 is locked on:
// one of its running animations (UUserWidget ActiveSequencePlayers +0x168, UUMGSequencePlayer Animation +0x370) is
// Lock_In (+0x3C0) / Lock_Loop (+0x3A8) - the soft target runs Free_* / Auto_* instead.
static bool TiIsOurs(void* w);   // players 2/3's game reticles (pip_hud/36g) are not player 1's
static void* GameReticleWidget()
{
    for (int i = 0; i < g_RwN; ++i)
    {
        void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]);
        if (!w) continue;
        void* cls = *(void**)((unsigned char*)w + 0x10);
        if (cls && ClassIs(cls, "BattleTarget_New_C") && !TiIsOurs(w)) return w;
    }
    return nullptr;
}
// whether the game's own lock-on is on (the leader's - player 1's, or player 2/3's while they lead). RE: the battle
// manager (*(base+0x57E9720)) +0x388 is the flag the reticle code reads to pick icon state 3 LockedEnabled vs 1 Enabled
// (0x9B98E3). f1.47-f1.51 read the reticle widget's animations / panels instead - the f1.51 test log showed those never
// change (LOCKDIAG), so that guess and its log are gone.
static bool GameLockedOn()
{
    unsigned char* bm = *(unsigned char**)(g_Base + RVA_BATTLE_GLOBAL_A);
    bool on = bm && TsReadable(bm + 0x388, 1) && bm[0x388] != 0;
    static int last = -1, logs = 0;
    if ((int)on != last) { last = on; if (logs < 40) { ++logs; CoopLog("LOCKON: the game's lock-on is %s%s", on ? "ON" : "off", ""); } }
    return on;
}
static bool P1LockedOn() { return GameLockedOn(); }
// a character's current target character (battle unit +0x38 = the target's handle = that character's +0xCB8).
// one cache slot per caller (0 = player 1's camera, 2/3 = the leading player's marker).
static void* TargetCharOf(void* ch, int slot)
{
    if (!ch || slot < 0 || slot > 3 || !TsReadable((unsigned char*)ch + OFF_BATTLE_UNIT, 8)) return nullptr;
    unsigned char* u = *(unsigned char**)((unsigned char*)ch + OFF_BATTLE_UNIT);
    if (!u || !TsReadable(u + OFF_UNIT_TARGET, 8)) return nullptr;
    void* h = *(void**)(u + OFF_UNIT_TARGET);
    if (!h) return nullptr;
    static void* lastH[4] = {}; static int ci[4] = {-1, -1, -1, -1}, cs[4] = {};
    if (h == lastH[slot] && ci[slot] >= 0)
    {
        void* c = ResolveWeak(ci[slot], cs[slot]);
        if (c && TsReadable((unsigned char*)c + 0xCB8, 8) && *(void**)((unsigned char*)c + 0xCB8) == h) return c;
    }
    lastH[slot] = h; ci[slot] = -1;
    void* en[24]; int ne = CollectEnemies(en, 24);
    for (int i = 0; i < ne; ++i)
        if (en[i] && TsReadable((unsigned char*)en[i] + 0xCB8, 8) && *(void**)((unsigned char*)en[i] + 0xCB8) == h) { MakeWeak(en[i], &ci[slot], &cs[slot]); return en[i]; }
    return nullptr;
}
static void* P1TargetChar(void* p1) { return TargetCharOf(p1, 0); }
// player 1's reticle kept out of players 2/3's windows
static void HideP1ReticleInWindows(void* canvas)
{
    // this engine has no SetRenderOpacity ("NOT found") - UUserWidget::SetColorAndOpacity (whole widget,
    //        alpha 0); the color it had before is put back (UUserWidget ColorAndOpacity +0x100)
    static int state = 0;   // (the function itself is shared - passes/12 ColFind / SetWidgetColor)
    static int hIdx = -1, hSer = 0; static bool hidden = false; static int logs = 0;
    static float saved[4] = {1.f, 1.f, 1.f, 1.f};
    auto SetOp = [&](void* w, float a) { if (a >= 1.f) SetWidgetColor(w, saved); else { float c[4] = {saved[0], saved[1], saved[2], a}; SetWidgetColor(w, c); } };
    void* hw = hidden ? ResolveWeak(hIdx, hSer) : nullptr;
    bool want = AnyWindowShowing() && !g_InCutscene && g_Hc.state == 1 && g_RwN > 0;
    void* bt = nullptr;
    if (want) bt = GameReticleWidget();   // same search, shared
    if (bt && state == 0)
    {
        state = ColFind(bt) ? 1 : -1;
        CoopLog("RETICLE: SetColorAndOpacity %s", state == 1 ? "found" : "NOT found - player 1's reticle can show over the windows");
    }
    bool out = false; float cx = 0.f, cy = 0.f;
    if (bt && state == 1)
    {
        float x, y, rw, rh, sc;
        if (HcRect(bt, &x, &y, &rw, &rh, &sc) && rw > 2.f)
        {
            cx = x + rw * 0.5f; cy = y + rh * 0.5f;
            float X = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), Y = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
            // player 1's part: right half (windows on the left, side 0), left half (right, 1), bottom half (top, 2), top half (bottom, 3)
            switch (SplitSide())   // thirds = windows to the right of player 1's left third
            {
                case 0: out = cx < X * 0.5f; break;
                case 1: out = cx > X * SplitP1Frac(); break;
                case 2: out = cy < Y * 0.5f; break;
                default: out = cy > Y * 0.5f; break;
            }
        }
    }
    if (out)
    {
        if (!hidden || hw != bt)
        {
            if (hw && hw != bt) SetOp(hw, 1.f);
            const float* c = (const float*)((unsigned char*)bt + 0x100);   // its color now (normally 1, 1, 1, 1)
            if (c[3] > 0.01f && c[3] <= 1.f) memcpy(saved, c, 16); else { saved[0] = saved[1] = saved[2] = saved[3] = 1.f; }
            MakeWeak(bt, &hIdx, &hSer); hidden = true; if (logs < 20) { ++logs; CoopLog("RETICLE: player 1's reticle is over a player's window (%.0f, %.0f) - hidden there", cx, cy); } }
        SetOp(bt, 0.f);
    }
    else if (hidden)
    {
        if (hw && state == 1) SetOp(hw, 1.f);
        hidden = false;
        if (logs < 20) { ++logs; CoopLog("RETICLE: player 1's reticle shows again"); }
    }
}
