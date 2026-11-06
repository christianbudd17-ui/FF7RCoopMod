// the game functions hooked here (finding #45 - were bare numbers)
static constexpr uintptr_t RVA_UNIT_CHANGE_STATE = 0x987270;   // a battle unit changes state (guard diagnostics)
static constexpr uintptr_t RVA_PS4_HID_READ = 0x3F878C0;       // the game's own PlayStation 4 controller reader
static constexpr uintptr_t RVA_REQUEST_GUARD_EXEC = 0x17B6190; // RequestGuard's exec (AI guard copy)
static constexpr uintptr_t RVA_KEY_E00 = 0x1394E00, RVA_KEY_F30 = 0x1394F30;   // two key-state readers (field attack)
static constexpr uintptr_t RVA_LEADER_SLOT = 0x1420650;        // "the leader's party slot" (players 2 / 3's closed command bar)
static constexpr uintptr_t RVA_SWITCH_TO = 0x9BAA80;           // SwitchToCharacter (the game's character switch)
static constexpr uintptr_t RVA_CAN_SWITCH_TO = 0x9CC4C0;       // its "can be switched to" check (only caller: RVA_SWITCH_TO)
// every battle / leader / switch hook (finding #45: was InstallActionPass2Hooks - it installs much more than the action pass):
// unit state, the PS4 reader, guard, field-attack keys, WidgetKey, GetLeader, the leader's slot, SetPartyLeader,
// SwitchToCharacter + its check, the command HUD refresh, and the battle command update
static void InstallBattleAndSwitchHooks()
{
    {
        static const unsigned char exUS[15] = {0x48,0x89,0x5c,0x24,0x10,0x44,0x88,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54};
        void* t = MakeInlineHook(g_Base + RVA_UNIT_CHANGE_STATE, (void*)Hook_UnitState, 15, exUS, "UnitChangeState (guard diagnostics)");
        if (t) Orig_UnitState = (UnitState_t)t;
    }
    {   // the game's PlayStation 4 controller reader
        static const unsigned char exHid[15] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
        // f1.337: the HID reader runs on its own thread - its original is set before the hook goes live
        MakeInlineHook(g_Base + RVA_PS4_HID_READ, (void*)Hook_HidRead, 15, exHid, "PS4 controller reader (HID)", (void**)&Orig_HidRead);
    }
    {
        static const unsigned char exRG[16] = {0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x42,0x20};
        void* t = MakeInlineHook(g_Base + RVA_REQUEST_GUARD_EXEC, (void*)Hook_RequestGuardExec, 16, exRG, "RequestGuard exec (AI guard copy)");
        if (t) Orig_RequestGuardExec = (ExecFn_t)t;
        static const unsigned char exK[15] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40};
        void* t1 = MakeInlineHook(g_Base + RVA_KEY_E00, (void*)Hook_KeyE00, 15, exK, "KeyE00 (field attack)");
        if (t1) Orig_KeyE00 = (KeyFn_t)t1;
        void* t2 = MakeInlineHook(g_Base + RVA_KEY_F30, (void*)Hook_KeyF30, 15, exK, "KeyF30 (field attack)");
        if (t2) Orig_KeyF30 = (KeyFn_t)t2;
    }
    static const unsigned char exBcm[17] = {0x40,0x55,0x53,0x41,0x54,0x48,0x8d,0x6c,0x24,0xb9,0x48,0x81,0xec,0xf0,0x00,0x00,0x00};
    static const unsigned char exLeader[14] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x0f,0xb6,0xca,0x84,0xd2};
    static const unsigned char exWKey[15] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20};
    void* tW = MakeInlineHook(g_Base + RVA_WIDGET_KEY, (void*)Hook_WidgetKey, 15, exWKey, "WidgetKeyState");
    if (tW) Orig_WidgetKey = (WidgetKey_t)tW;
    void* tL = MakeInlineHook(g_Base + RVA_GET_LEADER, (void*)Hook_GetLeader, 14, exLeader, "GetLeader");
    if (tL) Orig_GetLeader = (GetLeader_t)tL;
    {   // "the leader's party slot" (passes/07 Hook_LeaderIdx) - players 2/3's closed command bar
        static const unsigned char exIdx[15] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
        void* t = MakeInlineHook(g_Base + RVA_LEADER_SLOT, (void*)Hook_LeaderIdx, 15, exIdx, "leader's party slot (own command bars)");
        if (t) Orig_LeaderIdx = (LeaderIdx_t)t;
    }
    // player 1 switching block
    static const unsigned char exSetLeader[14] = {0x40,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30};
    void* tS = MakeInlineHook(g_Base + RVA_SET_LEADER, (void*)Hook_SetLeader, 14, exSetLeader, "SetPartyLeader (switch block)");
    if (tS) Orig_SetLeader = (SetLeader_t)tS;
    {
        static const unsigned char exSw[16] = {0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
        void* t = MakeInlineHook(g_Base + RVA_SWITCH_TO, (void*)Hook_SwitchTo, 16, exSw, "SwitchToCharacter (marks the game's switch)");
        if (t) Orig_SwitchTo = (SwitchTo_t)t;
    }
    if (Orig_SwitchTo)
    {   // the switch routine's own "can be switched to" check (only caller: RVA_SWITCH_TO) - people-played characters fail it
        static const unsigned char exCan[17] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x82,0xb0,0x0c,0x00,0x00,0x0f,0xb6,0x58,0x58};
        void* t = MakeInlineHook(g_Base + RVA_CAN_SWITCH_TO, (void*)Hook_CanSwitchTo, 17, exCan, "switch check CanSwitchTo (people-played characters skipped)");
        if (t) Orig_CanSwitch = (CanSwitch_t)t;
    }
    {   // the command HUD's shortcut-data refresh (events refresh player 1's; the others are marked to refresh)
        static const unsigned char exRef[17] = {0x48,0x89,0x4c,0x24,0x08,0x53,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x68};
        void* t = MakeInlineHook(g_Base + RVA_PRES_REFRESH, (void*)Hook_PresRefresh, 17, exRef, "command HUD shortcut refresh (own command systems)");
        if (t) Orig_PresRefresh = (PresFn_t)t;
    }
    if (tW && tL)
    {
        void* tB = MakeInlineHook(g_Base + RVA_BCM_UPDATE, (void*)Hook_BcmUpdate, 17, exBcm, "BattleCommandUpdate");
        if (tB) Orig_BcmUpdate = (BcmUpdate_t)tB;
    }
    else CoopLog("Action pass 2 NOT installed (helper hook failed)");
}


// ---- v3.3: one switch. Player 2 joins/leaves (JoinKey, F10 by default; was Ctrl+F12): movement pass 2 + action pass 2 +
// Barret's AI decisions off (same thing SetAIPCControllerTick(false) does natively: set bit 0x40
// in battleCtrl+0xB90, set +0x22F9=1, then call 0x976360(chara+0xCB0)). Re-applied every frame so
// it also takes effect when a battle starts after joining.
static constexpr uintptr_t RVA_AITICK_NOTIFY = 0x976360;
typedef void (*AINotify_t)(void* obj);
static void ApplyP2AI(bool coopOn)
{
    if (!GetPartyMember) return;
    unsigned char* p2 = (unsigned char*)P2Char();
    unsigned char* leader = (unsigned char*)LeaderChar();   // (L2): was GetPartyMember(0) - player 2 on Cloud after player 1 switched kept Cloud's AI on
    unsigned char* ctrl = (p2 && p2 != leader) ? *(unsigned char**)(p2 + OFF_CHAR_AI_CTRL) : nullptr;
    bool wantOff = coopOn && ctrl;
    // restore the previous controller if it changed or co-op turned off
    if (g_Player->aiOffCtrl && (g_Player->aiOffCtrl != ctrl || !wantOff))
    {
        unsigned char* c = (unsigned char*)g_Player->aiOffCtrl;
        if (g_Player->aiOffWIdx != -2 && ResolveWeak(g_Player->aiOffWIdx, g_Player->aiOffWSer) != g_Player->aiOffCtrl)
        {   // (code review C2): the controller is gone (save loaded, chapter change, character left) - its memory may
            // belong to something else now, so nothing is written there
            CoopLog("Player %d: the AI controller %p is gone (level change / load) - nothing to hand back there", g_Player->num, g_Player->aiOffCtrl);
            c = nullptr;
        }
        if (c && Readable(c + OFF_AI_FLAGS, 4)) { *(DWORD*)(c + OFF_AI_FLAGS) &= ~AIFLAG_DECISIONS_OFF; c[OFF_AI_FLAGS_CHANGED] = 1; }
        unsigned char* oc = (unsigned char*)CharByType(PartyMgr(), g_Player->aiOffType);   // the old character, if still here
        if (oc && *(void**)(oc + OFF_BATTLE_UNIT)) ((AINotify_t)(g_Base + RVA_AITICK_NOTIFY))(*(void**)(oc + OFF_BATTLE_UNIT));
        CoopLog("Player %d AI handed back to the game (ctrl=%p, type %d)", g_Player->num, g_Player->aiOffCtrl, g_Player->aiOffType);
        g_Player->aiOffCtrl = nullptr; g_Player->aiOffType = -1; g_Player->aiOffWIdx = -2; g_Player->aiOffWSer = 0;
    }
    if (wantOff && !(*(DWORD*)(ctrl + OFF_AI_FLAGS) & AIFLAG_DECISIONS_OFF))
    {
        *(DWORD*)(ctrl + OFF_AI_FLAGS) |= AIFLAG_DECISIONS_OFF; ctrl[OFF_AI_FLAGS_CHANGED] = 1;
        if (*(void**)(p2 + OFF_BATTLE_UNIT)) ((AINotify_t)(g_Base + RVA_AITICK_NOTIFY))(*(void**)(p2 + OFF_BATTLE_UNIT));
        g_Player->aiOffCtrl = ctrl; g_Player->aiOffType = g_Player->charType;
        if (!MakeWeak(ctrl, &g_Player->aiOffWIdx, &g_Player->aiOffWSer)) { g_Player->aiOffWIdx = -2; g_Player->aiOffWSer = 0; }   // (code review C2)
        CoopLog("Player %d AI decisions OFF (ctrl=%p)", g_Player->num, ctrl);
    }
}

