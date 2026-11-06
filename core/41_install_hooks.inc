// (finding #35, the author 2026-10-26: "a window is fine"): when co-op switches itself off completely (the game's code
// isn't what the mod was made for - usually a game update) the mod's on-screen notices can't show (its HUD hooks aren't
// in), so a plain Windows message box says it - on its own thread, so the game keeps starting / running underneath it.
static DWORD WINAPI CoopOffBoxThread(void* text)
{
    MessageBoxW(nullptr, (const wchar_t*)text, L"FF7R Co-op Mod", MB_OK | MB_ICONWARNING | MB_TOPMOST);
    return 0;
}
static void CoopOffPopup(const char* why)
{
    static wchar_t text[600];
    static LONG shown = 0;
    if (InterlockedExchange(&shown, 1)) return;   // once per session
    swprintf(text, 600, L"Co-op is switched off for this session: %hs.\n\n"
                        L"This usually means the game was updated and the mod doesn't match it yet, or it isn't the Steam version (the Epic "
                        L"version isn't supported yet). The game itself runs "
                        L"normally, just without co-op.\n\n"
                        L"Details: FF7RCoopMod_Coop.log in the game's End\\Binaries\\Win64 folder.", why);
    HANDLE t = CreateThread(nullptr, 0, CoopOffBoxThread, text, 0, nullptr);
    if (t) CloseHandle(t);
    CoopLog("Co-op off - told the player in a message box (%s)", why);
}
static void InstallCoopHooks()
{
    if (g_HooksInstalled) return;
    g_HooksInstalled = true; // only ever try once

    g_Base = (uintptr_t)GetModuleHandleW(nullptr);
    CoopLog("==== FF7RCoopMod v" COOP_MOD_VERSION " - installing co-op hooks (exe base %p) ====", (void*)g_Base);
    InstallDInputHooks();

    // Sanity check: the controller's vtable slot must point at the Tick we analyzed.
    uintptr_t* slot = (uintptr_t*)(g_Base + RVA_PC_VTABLE + PC_TICK_SLOT * sizeof(uintptr_t));
    // f1.334 (the author: "If someone put this .dll on their epic games version, would it fail?"): the slot is read only when
    // it's readable - another game build (Epic, a future patch) has other code at these addresses
    if (!Readable(slot, sizeof(uintptr_t)) || *slot != g_Base + RVA_PC_TICK)
    {
        CoopLog("HOOK FAILED: controller Tick slot holds %p, expected %p - game version differs, co-op disabled", Readable(slot, sizeof(uintptr_t)) ? (void*)*slot : nullptr, (void*)(g_Base + RVA_PC_TICK));
        g_CoopOff = true;   // DirectInput controllers read normally again (finding #39)
        CoopOffPopup("the game's version differs from the one the mod was made for");
        return;
    }

    static const unsigned char exAxis[14]    = {0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
    static const unsigned char exHeld[15]    = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40};
    static const unsigned char exPressed[16] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x01,0x48,0x8b,0xd9};

    // f1.337 (pre-release review): each original is set as its hook goes live - if a later one fails (co-op off below), the
    // hooks already written still call a real function (before: GetAxisPair hooked with a null original = a crash next frame)
    void* tAxis    = MakeInlineHook(g_Base + RVA_GET_AXIS_PAIR, (void*)Hook_GetAxisPair, 14, exAxis, "GetAxisPair", (void**)&Orig_GetAxisPair);
    void* tHeld    = MakeInlineHook(g_Base + RVA_KEY_HELD,      (void*)Hook_KeyHeld,     15, exHeld, "KeyHeld", (void**)&Orig_KeyHeld);
    void* tPressed = MakeInlineHook(g_Base + RVA_KEY_PRESSED,   (void*)Hook_KeyPressed,  16, exPressed, "KeyPressed", (void**)&Orig_KeyPressed);
    if (!tAxis || !tHeld || !tPressed)
    {
        CoopLog("Co-op disabled: one or more helper hooks failed (see above).");
        g_CoopOff = true;   // DirectInput controllers read normally again (finding #39)
        CoopOffPopup("a part of the game the mod needs has changed");
        return;
    }
    Orig_GetAxisPair = (GetAxisPair_t)tAxis;
    // (finding #40: KeyQ3 / KeyQ4 start with the same 16 bytes as KeyPressed - one array)
    void* tQ3 = MakeInlineHook(g_Base + RVA_KEY_Q3, (void*)Hook_KeyQ3, 16, exPressed, "KeyQ3(2640)");
    void* tQ4 = MakeInlineHook(g_Base + RVA_KEY_Q4, (void*)Hook_KeyQ4, 16, exPressed, "KeyQ4(2780)");
    if (tQ3) Orig_KeyQ3 = (KeyCheck_t)tQ3;
    if (tQ4) Orig_KeyQ4 = (KeyCheck_t)tQ4;
    Orig_KeyHeld = (KeyCheck_t)tHeld;
    Orig_KeyPressed = (KeyCheck_t)tPressed;
    GetPartyMember = (GetPartyMember_t)(g_Base + RVA_GET_PARTY_MEM);

    // removed the old v2 "battle action handler" test hook

    static const unsigned char exPE[19] = {0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xf0,0x00,0x00,0x00};
    void* tPE = MakeInlineHook(g_Base + RVA_PROCESS_EVENT, (void*)Hook_ProcessEvent, 19, exPE, "ProcessEvent (HUD + cutscene events)", (void**)&Orig_ProcessEvent);
    // f1.337 (pre-release review): not hooked (e.g. another tool such as UE4SS hooked it first - its bytes differ) - the mod's own
    // calls go to the function's real entry (through that tool's hook); before, Orig_ProcessEvent stayed null and the first
    // frame's pause check called it
    if (!tPE) { Orig_ProcessEvent = (ProcessEvent_t)(g_Base + RVA_PROCESS_EVENT); CoopLog("ProcessEvent not hooked - the mod calls it directly (HUD / cutscene events not seen)"); }

    // removed the command-widget key diagnostic and the Blueprint script tracer (test-only)
    InstallBattleAndSwitchHooks();
    InstallFocusLogHooks();   // two command menus at once - since f1.20 these hooks route each player's menu focus (not only logs)
    InstallSharedItemHooks();   // queued item commands count as used
    InstallMenuDimHook();   // [General] MenuDarken
    InstallEnemyDamageHook();   // [Balance] EnemyDamage
    InstallTargetSetLog();   // f1.212 TARGETSET (log only): which code writes enemy targets
    InstallBossActLog();   // f1.226 BOSSACT (log only): each boss move
    InstallGrabCamLog();   // f1.241 GRABCAM (log only): PlayBattleCameraSequence
    InstallInputDirHook();   // f1.265: P2 / P3's dodge / ability direction from their own stick (passes/12)
    InstallPopupResearch();   // players 2/3's copies of the game's damage numbers (pip_hud/36e; the name is from its f1.171 research start)
    InstallEnemyStatusResearch();   // players 2/3's game enemy bars (pip_hud/36f; the name is from its f1.174 research start)
    InstallGameReticleHook();   // players 2/3's game reticle (pip_hud/36g)
    InstallEnemyAttackRateHook();   // [Balance] EnemyAttackRate
    InstallOwnMenuPageHook();   // own menus only turn to their own / an AI teammate's page
    // (InstallOwnMenuShowHook - ChangePage 0x14216B0 for QuietPageSetup - removed)
    InstallButtonIconHooks();   // players 2/3's own button icons
    InstallCameraHook();
    InstallHumanRulePatches();

    // removed the old v2 ResolveActor replacement (action-redirect test) - the game's own runs again

    Orig_Tick = (Tick_t)(g_Base + RVA_PC_TICK);
    DWORD old;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old))   // (finding #38: the result is checked now)
    {
        CoopLog("HOOK FAILED controller Tick: VirtualProtect on its vtable slot - co-op disabled");
        g_CoopOff = true;
        CoopOffPopup("Windows refused the mod access to the game's code");
        return;
    }
    *slot = (uintptr_t)&Hook_Tick;
    VirtualProtect(slot, sizeof(uintptr_t), old, &old);
    {   // (finding #38) the configured join keys, not hard-coded "F10 / Ctrl+F10"
        wchar_t k2[40], k3[40];
        KeyCombo(g_P2Cfg.joinVk, g_P2Cfg.joinCtrl, g_P2Cfg.joinShift, g_P2Cfg.joinAlt, k2, 40);
        KeyCombo(g_P3Cfg.joinVk, g_P3Cfg.joinCtrl, g_P3Cfg.joinShift, g_P3Cfg.joinAlt, k3, 40);
        CoopLog("Hooked controller Tick (vtable slot %d). Players 2/3 join / leave with their JoinKey (%ls / %ls) or JoinButton.", PC_TICK_SLOT, k2, k3);
    }
    In2Start();   // the new input modules (always - they read every player's controller)
}

