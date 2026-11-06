// the per-player parts of a frame (run on player 2's state, then on player 3's)
static void PlayerFrameInput()
{
    ReadPadThisFrame();   // (review #23 / #28): their controller, read once - the three below share it
    PollPad2();
    PollP2Connection();   // (Auto slot, join button, disconnect handling)
    P2SwitchTick();       // D-pad switch character
    PollCoopToggleKey();
    ApplyP2AI(g_Player->joined);
    g_Player->drawValid = false;  // set again by UpdateP2Targeting when there is a target
}
// Movement pass for the loaded player. Returns their character if a person is playing it this frame.
static void* PlayerMovePass(void* pcv, float dt)
{
    if (!g_Player->joined || !GetPartyMember) return nullptr;
    unsigned char* pc = (unsigned char*)pcv;
    void** leaderSlot = (void**)(pc + OFF_LEADER_CHAR);
    void* p1 = *leaderSlot;
    if (!p1) return nullptr;
    void* p2 = P2Char();
    if (!p2 || p2 == p1) return nullptr;

    if (p2 != g_Player->lastChar)
    {
        g_Player->lastChar = p2;
        g_Player->moveStateValid = false;
        CoopLog("Pass %d target changed: p1=%p char=%p", g_Player->num, p1, p2);
    }

    SaveState(pc, g_P1State);
    if (g_Player->moveStateValid) LoadState(pc, g_Player->moveState);

    // the game turns the stick into a direction using player 1's camera yaw. While the
    // picture-in-picture shows, turn the stick by the difference so it follows the window's view.
    float rawX = g_Player->stickX, rawY = g_Player->stickY;
    {
        float ctrl = *(float*)(pc + OFF_CONTROL_ROTATION + 4);
        float d = AngleDiff(P2ControlYaw(pc), ctrl) * DEG_TO_RAD;
        if (d != 0.f && (rawX != 0.f || rawY != 0.f))
        {
            float a = atan2f(rawX, rawY), m = sqrtf(rawX * rawX + rawY * rawY);
            g_Player->stickX = m * sinf(a + d); g_Player->stickY = m * cosf(a + d);
        }
    }
    *leaderSlot = p2;
    g_InPass2 = true;
    Orig_Tick(pcv, dt);
    g_InPass2 = false;
    *leaderSlot = p1;
    g_Player->stickX = rawX; g_Player->stickY = rawY;

    SaveState(pc, g_Player->moveState);
    g_Player->moveStateValid = true;
    LoadState(pc, g_P1State);

    UpdateP2Targeting(pc, p2);

    static ULONGLONG lastLog[2] = {0, 0};
    ULONGLONG now = GetTickCount64();
    int li = g_Player->num == 3 ? 1 : 0;
    if (now - lastLog[li] > 2000 && (g_Player->stickX != 0.f || g_Player->stickY != 0.f))
    {
        lastLog[li] = now;
        CoopLog("Pass %d running: char=%p stick=(%.2f, %.2f)", g_Player->num, p2, g_Player->stickX, g_Player->stickY);
    }
    return p2;
}
// AI teammates copy player 1's guard.
// RE: a party member's battle unit (chara+0xCB0) has a "wants to guard" byte at +0x668. The AI
// controller's tick (0x8651A0) starts the guard (UnitStartGuard 0x976230, only from unit state 1 =
// free, so not mid-attack or mid-dodge) while it's set, and ends it (0x976360) when it's cleared.
// With player 2/3 in, the game's own copy of the leader's guard stopped reaching the AI members (the
// mechanism wasn't found: it isn't RequestGuard - v8.5 saw no calls at all). So the mod does it: while
// player 1's character is guarding (unit state 4/5), each AI-run party member gets +0x668 = 1; when
// player 1 lets go, it's cleared again (only where the mod set it).
static bool g_GuardCopySet[16] = {};
static void AIGuardCopyTick(const int* humanTy, int nHuman)
{
    if (!GetPartyMember || !g_Base) return;
    bool battle = InBattleNow();
    if (!battle)
    {   // (review A8 / F6): outside battle there is nothing to do unless a copied guard is still to be let go
        bool any = false; for (int t = 0; t < 16; ++t) any |= g_GuardCopySet[t];
        if (!any) return;
    }
    unsigned char* mgr = PartyMgr();
    int n = PartyCount();
    int lead = LeaderType(mgr);
    bool anyHuman = false; for (int h = 0; h < nHuman; ++h) if (humanTy[h] >= 0) anyHuman = true;
    // the party is walked once (was twice per frame with the same checks) - members with a readable battle unit
    unsigned char* mc[8]; unsigned char* mu[8]; int m = 0;
    for (int i = 0; i < n && m < 8; ++i)
    {
        unsigned char* c = (unsigned char*)GetPartyMember(i);
        if (!c || !Readable(c + OFF_BATTLE_UNIT, 8)) continue;
        unsigned char* u = *(unsigned char**)(c + OFF_BATTLE_UNIT);
        if (!u || !Readable(u + 0x58, 1) || !Readable(u + 0x611, 0x60)) continue;
        mc[m] = c; mu[m] = u; ++m;
    }
    // (the author): the AI copies ANY human player's guard - player 1's (the leader) or player 2/3's.
    bool p1Guards = false; int guardTy = -1;
    if (battle)
        for (int i = 0; i < m; ++i)
        {
            unsigned char* u = mu[i];
            int t = u[0x58]; bool hum = (t == lead); for (int h = 0; h < nHuman; ++h) if (humanTy[h] == t) hum = true;
            if (hum && (u[0x611] == 4 || u[0x611] == 5)) { p1Guards = true; guardTy = t; }
        }
    bool copyOn = g_Cfg.aiGuardCopy && g_Player->joined && anyHuman && !g_GamePaused;
    ULONGLONG now = GetTickCount64();
    static ULONGLONG lastCopyLog = 0;
    for (int i = 0; i < m; ++i)
    {
        unsigned char* c = mc[i];
        unsigned char* u = mu[i];
        int ty = u[0x58]; if (ty < 0 || ty >= 16) continue;
        bool human = false; for (int h = 0; h < nHuman; ++h) if (humanTy[h] == ty) human = true;
        bool isLead = (ty == lead);
        if (!isLead && !human && copyOn)
        {
            if (p1Guards && u[0x668] == 0 && !g_GuardCopySet[ty] && CharHP(c) != 0)
            {
                u[0x668] = 1; g_GuardCopySet[ty] = true;
                if (now - lastCopyLog > 1500) { lastCopyLog = now; CoopLog("GUARD: AI teammate type %d copies the guard of human-played type %d", ty, guardTy); }
            }
            else if (!p1Guards && g_GuardCopySet[ty])
            {
                u[0x668] = 0; g_GuardCopySet[ty] = false;
            }
        }
        else if (g_GuardCopySet[ty])   // became human-played / leader / copy switched off: let go
        {
            if (u[0x668]) u[0x668] = 0;
            g_GuardCopySet[ty] = false;
        }
    }
    if (!battle) for (int t = 0; t < 16; ++t) g_GuardCopySet[t] = false;
}
// rumble for players 2/3. The game only rumbles player 1's controller (the one it reads), so the mod
// rumbles player 2's/3's own controller when their character loses HP: stronger and longer for bigger hits.
static int g_RumHP[2] = {-1, -1}, g_RumMax[2] = {0, 0}; static void* g_RumChar[2] = {nullptr, nullptr};
static ULONGLONG g_RumUntil[2] = {0, 0}; static LONG g_RumSlot[2] = {-1, -1};
static void RumbleTick()
{
    int k = g_Player->num == 3 ? 1 : 0;
    ULONGLONG now = GetTickCount64();
    bool pre = g_Player->cfg->rumble > 0 && g_Player->joined && !g_GamePaused && !g_InCutscene && Real_XInputSetState;   // (review A23): the cheap checks first
    DWORD slot = 0xFF;
    if (pre) { int xs = In2SeatXSlot(g_Player->num == 3 ? 2 : 1); if (xs >= 0) slot = (DWORD)xs; }   // their seat's controller, if it is an XInput one (DirectInput: no rumble)
    bool can = pre && slot < 4;
    void* c = can ? P2Char() : nullptr;
    int hp = c ? CharHP(c) : -1;
    if (c != g_RumChar[k]) { g_RumChar[k] = c; g_RumHP[k] = -1; g_RumMax[k] = 0; }
    if (hp >= 0)
    {
        if (hp > g_RumMax[k]) g_RumMax[k] = hp;
        if (g_RumHP[k] > hp && g_RumMax[k] > 0)
        {
            float f = (float)(g_RumHP[k] - hp) / (float)g_RumMax[k];
            float st = (0.35f + f * 4.f) * g_Player->cfg->rumble / 100.f; if (st > 1.f) st = 1.f;
            if (g_RumSlot[k] >= 0 && g_RumSlot[k] != (LONG)slot) { XINPUT_VIBRATION z{0, 0}; Real_XInputSetState((DWORD)g_RumSlot[k], &z); }
            XINPUT_VIBRATION v{ (WORD)(st * 65535.f), (WORD)(st * 0.6f * 65535.f) };
            Real_XInputSetState(slot, &v);
            g_RumSlot[k] = (LONG)slot;
            ULONGLONG len = 120 + (ULONGLONG)(f * 1200.f); if (len > 500) len = 500;
            g_RumUntil[k] = now + len;
            static ULONGLONG lastLog = 0; if (now - lastLog > 5000) { lastLog = now; CoopLog("RUMBLE: player %d hit for %d HP - controller %lu rumbles (%.0f%%, %llu ms)", g_Player->num, g_RumHP[k] - hp, slot + 1, st * 100.f, (unsigned long long)len); }
        }
    }
    g_RumHP[k] = hp;
    if (g_RumSlot[k] >= 0 && (now >= g_RumUntil[k] || !can))
    {
        XINPUT_VIBRATION z{0, 0}; Real_XInputSetState((DWORD)g_RumSlot[k], &z); g_RumSlot[k] = -1;
    }
}

