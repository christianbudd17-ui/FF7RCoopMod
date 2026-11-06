// =================================================================================
// SECOND COMMAND SYSTEM (player 2) - the game builds a second, complete battle command manager
// (0x5D0 bytes from GMalloc, ctor 0x1412310; its first update runs the game's own setup 0x14130F0, which
// creates its own Command_C and binds it). Player 2's command pass runs on it instead of on player 1's, so
// player 2's shortcut palette (and later menus) live in their own game-made Command_C in their window.
// Battles only. Always on since f1.17 ([General] OwnCommandSystem removed; f1.15-f1.16 default 1).
// =================================================================================
// ownBcm / ownBcmOn / ownBcmDirty are defined earlier (passes/07_action_pass_state.inc)
static float g_SlowMain = 1.f, g_SlowB2 = 1.f; static bool g_SlowB2Valid = false;
typedef void* (*GMallocFn)(void*, size_t, uint32_t);
typedef void (*BcmCtor_t)(void*);
// (review B18): player 1's battle command system (the game's own) and a command system's Command_C widget (its weak
// reference at +0x300) - each was written out at every use (4x / 7x)
static unsigned char* MainBcm() { return *(unsigned char**)(g_Base + 0x59CAA08); }
static void* BcmCmdC(unsigned char* b) { return b ? ResolveWeak(*(int*)(b + BCM_CMDC_INDEX), *(int*)(b + BCM_CMDC_SERIAL)) : nullptr; }
static float* SlowChannel()
{
    unsigned char* g = *(unsigned char**)(g_Base + 0x57E9AB0);
    if (!g || !Readable(g + 0x3D0, 8)) return nullptr;
    unsigned char* ts = *(unsigned char**)(g + 0x3D0);
    if (!ts || !Readable(ts + 0x500, 0x40)) return nullptr;
    return (float*)(ts + 0x508);
}
// the game sends real key events (player 1's pad / keyboard - and the controller swap during players 2/3's
// main-menu turns) to the newest Command_C. On players 2/3's own Command_C those events worked their palettes
// (player 1's slot press used players 2 and 3's shortcuts; player 1's palette never saw the release and stuck open).
// The Command_C class overrides key preview / down / up (vtable 0x4BCFA68 / A70 / A78 = 0x1218E40 / 0x1219320 /
// 0x1219A90); all Command_C share that table. Events for player 2/3's Command_C are handed to player 1's.
typedef void* (*CmdKeyEv_t)(void* self, void* ret, void* geo, void* ev);
static CmdKeyEv_t Orig_CmdKey[4] = {nullptr, nullptr, nullptr, nullptr};
static int g_CmdKeyFwdN = 0;
static void* CmdKeyTarget(void* self)
{
    void* w2 = BcmCmdC(g_Player->ownBcm);
    void* w3 = BcmCmdC(g_OtherPlayer->ownBcm);
    if (!self || (self != w2 && self != w3)) return self;
    { int ownr = self == w2 ? (g_Player->num == 3 ? 3 : 2) : (g_Player->num == 3 ? 2 : 3); if (g_FCtxOn && g_FCtxLive == ownr) return self; }   // v17.9; inside their own focus context the event is really theirs
    void* mw = BcmCmdC(MainBcm());   // player 1's command system's Command_C
    if (!mw || mw == self) return self;
    if (g_CmdKeyFwdN++ < 5) CoopLog("CMDKEY: a key event for player %d's Command_C goes to player 1's", self == w2 ? (g_Player->num == 3 ? 3 : 2) : (g_Player->num == 3 ? 2 : 3));
    return mw;
}
// while player 2/3 is in a command menu, log the key events Command_C gets (which widget, which key, handled?)
static void* Hook_CmdKey0(void* self, void* ret, void* geo, void* ev) { void* t = CmdKeyTarget(self); void* r = Orig_CmdKey[0](t, ret, geo, ev); return r; }
static void* Hook_CmdKey1(void* self, void* ret, void* geo, void* ev) { void* t = CmdKeyTarget(self); void* r = Orig_CmdKey[1](t, ret, geo, ev); return r; }
static void* Hook_CmdKey2(void* self, void* ret, void* geo, void* ev) { void* t = CmdKeyTarget(self); void* r = Orig_CmdKey[2](t, ret, geo, ev); return r; }
// 0x1218750(this, FKey* key?, int, bool) - the game pushes key states into a Command_C's maps (+0x780 / +0x730)
// outside Slate; players 2/3's confirm presses in player 1's menu are suspected to land on an extra Command_C here.
static void* Hook_CmdKey3(void* self, void* a, void* b, void* c)
{
    void* t = CmdKeyTarget(self);
    static int logs = 0;
    if (logs < 80 && t != self)
    {
        ++logs;   // 'a' is the caller's return slot, not a key - never look up a name from it (crashed in 0x1CE89A0)
        CoopLog("MENUDIAG: Command_C key push %p (%d, %d) on an extra Command_C - passed to player 1's", a, (int)(intptr_t)b, (int)((uintptr_t)c & 0xFF));
    }
    return Orig_CmdKey[3](t, a, b, c);
}
static void InstallCmdKeyFwd(void* anyCmdC)
{
    static bool done = false; if (done) return; done = true;
    // Command_C's vtable slots for its four key-event functions (slotR, RVAs of the slots in the game's vtable) and the
    // game function each slot must hold before it is replaced (fnR) - a slot holding anything else (another game version,
    // another mod) is left alone and logged. Hook_CmdKey0 - 3 forward an extra Command_C's key events to player 1's.
    static const uintptr_t slotR[4] = {0x4BCFA68, 0x4BCFA70, 0x4BCFA78, 0x4BCFB60}, fnR[4] = {0x1218E40, 0x1219320, 0x1219A90, 0x1218750};
    void* hk[4] = {(void*)Hook_CmdKey0, (void*)Hook_CmdKey1, (void*)Hook_CmdKey2, (void*)Hook_CmdKey3};
    uintptr_t vt = anyCmdC ? *(uintptr_t*)anyCmdC : 0;
    bool same = vt && vt <= g_Base + slotR[0] && g_Base + slotR[3] < vt + 0x1000;
    CoopLog("CMDKEY: Command_C vtable %p (%s the patched table)", (void*)(vt ? vt - g_Base : 0), same ? "is" : "is NOT");
    for (int i = 0; i < 4; ++i)
    {
        uintptr_t* slot = (uintptr_t*)(g_Base + slotR[i]);
        if (*slot != g_Base + fnR[i]) { CoopLog("CMDKEY: slot %d holds %p, expected %p - not hooked", i, (void*)(*slot - g_Base), (void*)fnR[i]); continue; }
        Orig_CmdKey[i] = (CmdKeyEv_t)*slot;
        DWORD old; VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old);
        *slot = (uintptr_t)hk[i];
        VirtualProtect(slot, sizeof(uintptr_t), old, &old);
    }
    CoopLog("CMDKEY: Command_C key preview/down/up/push forwarded (players 2/3's -> player 1's)");
}
static unsigned char* Bcm2Make()
{
    void* gm = *(void**)(g_Base + 0x5825680);
    static int fails = 0;   // logged a few times only (it was tried every update); without it this player has no shortcut palette
    if (!gm) { if (++fails <= 3) CoopLog("CMD2: player %d's command system can't be built yet (no game allocator) - no shortcut palette for them until it is", g_Player->num); return nullptr; }
    void* mem = ((GMallocFn)((*(void***)gm)[2]))(gm, 0x5D0, 0);
    if (!mem) { if (++fails <= 3) CoopLog("CMD2: player %d's command system - allocation failed - no shortcut palette for them this battle", g_Player->num); return nullptr; }
    ((BcmCtor_t)(g_Base + 0x1412310))(mem);
    ((unsigned char*)mem)[0x541] = 1;   // run the game's setup on the first update
    CoopLog("CMD2: player %d's command system built (%p)", g_Player->num, mem);
    return (unsigned char*)mem;
}
// (code review C1): this player's (num's) own command menu is open
static bool OwnCmdMenuOpen()
{
    return g_Player->ownBcmOn && g_Player->ownBcm && *(int*)(g_Player->ownBcm + BCM_UI_STATE) >= BCM_UI_MENU_OPEN;
}
static void* Bcm2Widget() { return BcmCmdC(g_Player->ownBcm); }
static void* Bcm2Side()   { return g_Player->ownBcm ? ResolveWeak(*(int*)(g_Player->ownBcm + 0x2F8), *(int*)(g_Player->ownBcm + 0x2FC)) : nullptr; }
// (the author, f1.208 long play 19:31:22 screenshot: "after airbuster fight, the P2 and P3 menus stayed up on the screen"):
// the game hides its own command HUD (player 1's) during a cutscene, but players 2/3's Command_C widgets are our extra
// ones - they stayed at their window spots through the whole 38 s end-of-fight cutscene (19:30:49.9 - 19:31:28.3, their
// command systems still on until the battle ended at 19:31:28.55). Now, once a frame for each extra player (pip_hud/37
// PipDrawUnderHud): while a cutscene / scripted shot is on (the same signal that hides the split-screen windows) their
// Command_C is Hidden (visibility 2); when it is over it gets its old visibility back after CMD_HUD_SHOW_DELAY_MS, if
// their command system is still on (a battle that ends right after the cutscene - this one, 0.25 s later - then never
// flashes it; the end of the battle hides it as before, menus/10 Bcm2Tick visibility 1).
static constexpr ULONGLONG CMD_HUD_SHOW_DELAY_MS = 500;
// f1.215: players 2/3's own menus don't open while their command HUD is hidden for a cutscene / scripted shot (and its
// 500 ms afterwards) - nothing of it would show, and it couldn't be closed there
static constexpr int CUT_CLOSE_MAX_TRIES = 8;              // made Cancels for an own menu open as a cutscene begins
static constexpr ULONGLONG CUT_CLOSE_EVERY_MS = 150;
static bool OwnMenuBlockedByCutscene() { return g_InCutscene || g_ScriptedShot || g_QuietLeadHide || g_Player->cmdHudCutHidden; }
static void OwnCmdHudCutscene()
{
    void* w = g_Player->ownBcm ? Bcm2Widget() : nullptr;
    bool cut = g_InCutscene || g_ScriptedShot || g_QuietLeadHide;   // (f1.248: + a quiet lead hand-off)
    unsigned char* wb = (unsigned char*)w;
    int pi = g_Player->num & 3;
    static int closeTries[4] = {}; static ULONGLONG closeT[4] = {}; static bool gaveUp[4] = {};   // (f1.216 cutscene close)
    if (!cut) { closeTries[pi] = 0; gaveUp[pi] = false; closeT[pi] = 0; }
    if (w && !Readable(wb + 0xB7, 1)) w = nullptr;
    if (cut && g_Player->ownBcmOn && w)
    {
        g_Player->cmdHudCutEnd = 0;
        bool menuOpen = *(int*)(g_Player->ownBcm + BCM_UI_STATE) >= BCM_UI_MENU_OPEN;
        if (menuOpen)
        {   // f1.216 (the author: "They should probably be forced closed like P1's is."): their menu open when a cutscene
            // begins (f1.215 stops it opening in one) is closed - a made Cancel every 150 ms (a sub-menu needs more than
            // one), shown meanwhile so it still takes their keys. (f1.214 soft lock: hidden, it could not be closed.)
            ULONGLONG nowC = GetTickCount64();
            if (closeTries[pi] < CUT_CLOSE_MAX_TRIES)
            {
                if (g_Player->cmdHudCutHidden && wb[0xB7] == 2) WidgetSetVis(w, g_Player->cmdHudCutVis);
                if (!g_CtxAutoClose[pi] && nowC - closeT[pi] >= CUT_CLOSE_EVERY_MS)
                {
                    g_CtxAutoClose[pi] = 1; closeT[pi] = nowC;
                    if (closeTries[pi]++ == 0) CoopLog("CMD2 MENU: player %d's own menu is open as a cutscene / scripted shot begins - closing it (made Cancel)", g_Player->num);
                }
            }
            else if (!gaveUp[pi])
            {   // didn't close: leave it SHOWN (B still closes it; no slowdown in a cutscene - f1.215) rather than hidden
                gaveUp[pi] = true;
                CoopLog("CMD2 MENU: player %d's own menu did NOT close after %d Cancels in the cutscene - left showing (no slowdown; B closes it)", g_Player->num, closeTries[pi]);
            }
            return;
        }
        if (closeTries[pi])
        {
            CoopLog("CMD2 MENU: player %d's own menu closed for the cutscene (%d Cancel(s))", g_Player->num, closeTries[pi]);
            closeTries[pi] = 0; gaveUp[pi] = false; closeT[pi] = 0;
        }
        if (wb[0xB7] == 2) return;   // hidden already
        if (!g_Player->cmdHudCutHidden)
        {
            g_Player->cmdHudCutHidden = true;
            g_Player->cmdHudCutVis = wb[0xB7] == 1 ? 4 : wb[0xB7];
            CoopLog("CMD2: player %d's command HUD hidden - cutscene / scripted shot (was visibility %d)", g_Player->num, (int)wb[0xB7]);
        }
        if (wb[0xB7] != 1) WidgetSetVis(w, 2);   // (1 = we collapsed it at the battle's end - leave that)
        return;
    }
    if (!g_Player->cmdHudCutHidden) return;
    ULONGLONG now = GetTickCount64();
    if (!g_Player->ownBcmOn || !w) { g_Player->cmdHudCutHidden = false; g_Player->cmdHudCutEnd = 0; return; }   // battle over: Bcm2Tick hides it
    if (!g_Player->cmdHudCutEnd) { g_Player->cmdHudCutEnd = now; return; }
    if (now - g_Player->cmdHudCutEnd < CMD_HUD_SHOW_DELAY_MS) return;
    g_Player->cmdHudCutHidden = false; g_Player->cmdHudCutEnd = 0;
    if (wb[0xB7] == 2) WidgetSetVis(w, g_Player->cmdHudCutVis);
    CoopLog("CMD2: player %d's command HUD shown again - the cutscene / scripted shot is over (visibility %d)", g_Player->num, (int)wb[0xB7]);
}
// every Command_C registers itself as a key listener on player 1's controller when it's built
// (NativeConstruct 0x1217C00 -> any-key bindings 0x28D8710 + RegisterInputComponent 0x28DA850); the newest one
// is on top and takes the keys, so player 2's took player 1's. Player 2's keys come from the mod, so it doesn't
// need to listen: UnregisterInputComponent (re-done every half second in case the game registers it again).
// (unregFn / unregW / unregTick: in each player's record, core/02 PlayerSlot)
// building a Command_C also re-registers a shared HUD widget's key listener (0x1217CA4 -> 0x13ABAF0) just
// before its own, so after player 2's was built that shared listener sat ABOVE player 1's Command_C and took all of
// player 1's keys (pad and keyboard; v16.2 log: player 1's command system never saw a key). Fix, once per new
// widget: unregister player 2's, then RegisterInputComponent on player 1's Command_C (PushInputComponent moves it
// back to the top = the game's own order with one Command_C).
// Command_C's construction also registers an inner widget's key listener (0x1217CA4 -> 0x13ABAF0, the menu
// list found in its widget tree) - on top of player 1's input. Every user widget inside player 2/3's Command_C stops
// listening; player 1's inner widgets and Command_C register again (= on top, the game's order with one Command_C).
static void Bcm2NoListen(void* w, unsigned char* mainBcm)
{
    if (!w || !Orig_ProcessEvent) return;
    if (w != g_Player->unregW)
    {
        g_Player->unregW = w; g_Player->unregFn = FindFunc(w, "UnregisterInputComponent"); g_Player->unregTick = 0;
        CoopLog("CMD2: player %d's Command_C %p - UnregisterInputComponent %s", g_Player->num, w, g_Player->unregFn ? "found" : "NOT found");
        if (g_Player->unregFn) { alignas(16) unsigned char b0[32] = {}; Orig_ProcessEvent(w, g_Player->unregFn, b0); }
        void* m = BcmCmdC(mainBcm);
        void* reg = m ? FindFunc(m, "RegisterInputComponent") : nullptr;
        if (reg) { alignas(16) unsigned char b1[32] = {}; Orig_ProcessEvent(m, reg, b1); }
        CoopLog("CMD2: player 1's Command_C %p put back on top of player 1's key listeners (%s)", m, reg ? "done" : "RegisterInputComponent NOT found");
        return;
    }
    if (!g_Player->unregFn || (g_Player->unregTick++ % 30) != 0) return;
    alignas(16) unsigned char b[32] = {};
    Orig_ProcessEvent(w, g_Player->unregFn, b);
    static int logs = 0; if (logs < 2) { ++logs; CoopLog("CMD2: player %d's Command_C no longer listens to player 1's controller", g_Player->num); }
}
// the extra player's battle is ending - their own command system still runs for ~1 s ("keep" window below)
// but gets no buttons, so an attack / guard held at the end is RELEASED in it before it stops being updated (Barret
// kept firing after battles: the hold stayed in that system's state when it went off). Per player: menuEnding (core/02).
// decided at the start of each command update (player 2 context)
static void Bcm2Decide(unsigned char* bcm, bool p2coop)
{
    int ui = *(int*)(bcm + BCM_UI_STATE);
    bool want = p2coop && (ui == 8 || ui == 9) && *(int*)(bcm + BCM_CMDC_SERIAL) != 0;
    // player 1 closing their menu made the battle state flicker for a frame -> both extra systems went off and
    // on again, and their Command_C stayed hidden (palettes "wouldn't reopen"). Off only after ~1 s of not wanted.
    static int offN[4] = {0, 0, 0, 0}; int& off = offN[g_Player->num & 3];
    static constexpr int OWN_SYSTEM_KEEP_UPDATES = 60;   // ~1 s of command updates before an unwanted own system goes off
    bool keep = !want && g_Player->ownBcmOn && p2coop && ++off < OWN_SYSTEM_KEEP_UPDATES;
    if (want) off = 0;
    {
        static int flick = 0;
        if (keep && off == 1 && flick < 10) { ++flick; CoopLog("CMD2: player %d - battle state flickers (main ui %d, +304 %d) - staying on", g_Player->num, ui, *(int*)(bcm + BCM_CMDC_SERIAL)); }
    }
    if (want && !g_Player->ownBcm) g_Player->ownBcm = Bcm2Make();
    bool on = (want || keep) && g_Player->ownBcm;
    bool wasOn = g_Player->ownBcmOn;
    {   // from the 3rd update without a battle (a one-frame flicker when player 1 closes a menu doesn't count)
        bool ending = on && !want && off >= 3;
        bool& e = g_Player->menuEnding;
        if (ending && !e) CoopLog("CMD2: player %d's battle is ending - their own command system gets no buttons now (held 0x%05lX released there before it goes off)", g_Player->num, g_Player->cmdButtons);
        e = ending;
    }
    if (on != g_Player->ownBcmOn)
    {
        CoopLog("CMD2: player %d's own command system %s", g_Player->num, on ? "ON (battle)" : "off"); g_Player->ownBcmOn = on;
        if (!on && g_FCtxOn) CtxReleaseAll();
        // (code review C5): a new battle starts with fresh action / request copies (old ones were reused before)
        if (on) { g_Player->actValid = false; g_Player->reqValid = false; }
    }
    void* w = Bcm2Widget();
    if (on && !wasOn && w && ((unsigned char*)w)[0xB7] == 1) { WidgetSetVis(w, 4); CoopLog("CMD2: player %d's Command_C shown again", g_Player->num); }   // we hid it when it went off
    if (w) InstallCmdKeyFwd(w);
    Bcm2NoListen(w, bcm);
    if (!on && w && ((unsigned char*)w)[0xB7] != 1) WidgetSetVis(w, 1);   // outside battles: hidden
    // battle mode - the game calls 0x1415420(command system, on) at battle start / (.., off) at the end, only
    // on player 1's system; without it player 2's stays in its pre-battle state (ui 1) and never opens a palette
    if (g_Player->ownBcm && Bcm2Side())
    {
        unsigned char want348 = on ? (bcm[0x348] ? bcm[0x348] : g_Player->ownBcm[0x348]) : 0;   // keep it through a flicker
        if (g_Player->ownBcm[0x348] != want348)
        {
            typedef void (*SetBattle_t)(void*, unsigned char);
            { int fc = EnterCtx(g_Player->num); ((SetBattle_t)(g_Base + 0x1415420))(g_Player->ownBcm, want348); LeaveCtx(fc); }   // in their own focus context
            CoopLog("CMD2: player %d's command system battle mode %s (now %d, ui %d)", g_Player->num, want348 ? "ON" : "off", (int)g_Player->ownBcm[0x348], *(int*)(g_Player->ownBcm + BCM_UI_STATE));
        }
    }
    else if (g_Player->ownBcm && !on && g_Player->ownBcm[0x348] != 0)
    {   // (code review C6): the battle ended while their command system's side widget was already gone (a save loaded /
        // retry mid-battle) - the game's SetBattle(.., off) does nothing without it, so the system would stay in battle mode
        // and the next battle had no palette / own menu for this player. Replace it: a new one is built when the next battle
        // starts (as the v17.9 stuck-target-list path does).
        if (w) WidgetSetVis(w, 1);
        CoopLog("CMD2: player %d's command system is still in battle mode with its side widget gone (save loaded mid-battle?) - replaced by a new one next battle", g_Player->num);
        if (g_FCtxOn) CtxReleaseAll();
        g_Player->ownBcm = nullptr; g_Player->ownBcmOn = false; g_Player->ownBcmDirty = false;
        return;
    }
    if (on && g_Player->ownBcm && *(int*)(g_Player->ownBcm + BCM_CMDC_SERIAL) != 0 && !w && !g_Player->ownBcm[0x541]) { g_Player->ownBcm[0x541] = 1; CoopLog("CMD2: its Command_C is gone - the game's setup runs again"); }
}
// players 2/3's command menu runs in player 1's command system (main-menu turn, stage 3, their pad fed as
// game slot 0). With the extra command systems around, their confirm press never reached that menu (Command_C saw
// only the release; nothing happened) - with OwnCommandSystem=0 the same Potion worked. Keyboard focus is taken to
// be on an extra Command_C (the newest). When such a turn starts: log who has focus, SetKeyboardFocus on player 1's
// Command_C, log again 3 updates later.
