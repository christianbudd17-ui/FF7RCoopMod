// =================================================================================
// THE KEYS EACH COMMAND SYSTEM SEES (restructure 3b, from menus/08): the WidgetKey hook answers every key the game asks
// for - in player 1's pass his own keys (P1KeyFilter), in players 2 / 3's passes their pad's buttons (the key map,
// their palette, their own menu's keys - Bcm2MenuKey)
// =================================================================================
static void* ResolveWeak(int idx, int serial);
static constexpr DWORD PAD_LB = 0x0100;   // XINPUT_GAMEPAD_LEFT_SHOULDER in their pad bits (f1.270 Magnify)
// player 2's own command menu - the keys their command system asks for (other than the action keys above)
// come from player 2's pad: CommandMenu = their menu button; while the menu is open: D-pad, face buttons, shoulders
// (gamepad and keyboard key names). Every key it asks for is logged once with what it was given.
static bool PipShowing();   // pip_hud/30: this player's split-screen window is showing
static bool Bcm2BottomReady();   // menus/35: no window, but the own menu is placed along the bottom of the main screen
static int PickAllyType(int which);   // below
static bool OwnMenuBlockedByCutscene();   // menus/10 (f1.215)
static uint64_t Bcm2MenuKey(uint64_t fname, uint64_t r)
{
    struct E { uint64_t f; int code; }; static E cache[96]; static int n = 0, logs = 0;
    int code = -2;
    for (int i = 0; i < n; ++i) if (cache[i].f == fname) { code = cache[i].code; break; }
    if (code == -2)
    {
        char kn[96] = "?";
        if (fname) { alignas(8) unsigned char fake[0x20] = {}; memcpy(fake + 0x18, &fname, 8); NameOf(fake, kn, sizeof(kn)); }   // (finding #3: NameOf reads the name at +0x18 - a real 0x20-byte buffer instead of pointing 0x18 before a local)
        auto has = [&](const char* t) { return strstr(kn, t) != nullptr; };
        auto is = [&](const char* t) { return !strcmp(kn, t); };
        if (is("CommandMenu")) code = -1;
        else if (has("DPad_Up") || is("Up") || has("LeftStick_Up")) code = 0x0001;
        else if (has("DPad_Down") || is("Down") || has("LeftStick_Down")) code = 0x0002;
        else if (has("DPad_Left") || is("Left") || has("LeftStick_Left")) code = 0x0004;
        else if (has("DPad_Right") || is("Right") || has("LeftStick_Right")) code = 0x0008;
        else if (has("FaceButton_Bottom") || is("Enter") || is("SpaceBar") || has("Decide") || has("Confirm") || has("Accept")) code = 0x1000;
        else if (has("FaceButton_Right") || is("Escape") || is("BackSpace") || has("Cancel") || has("Back")) code = 0x2000;
        else if (has("FaceButton_Left")) code = 0x4000;
        else if (has("FaceButton_Top")) code = 0x8000;
        else if (has("LeftShoulder")) code = 0x0100;
        else if (has("RightShoulder")) code = 0x0200;
        else code = 0;
        if (n < 96) { cache[n].f = fname; cache[n].code = code; ++n; }
        if (logs < 60) { ++logs; CoopLog("CMD2 MENU: player %d's command system asks for key %s -> %s 0x%04X (ui %d, B8 %d)", g_Player->num, kn, code ? "pad button" : "nothing", code == -1 ? (unsigned)g_Player->cfg->bMenu : (unsigned)code, *(int*)(g_Player->ownBcm + BCM_UI_STATE), *(int*)(g_Player->ownBcm + BCM_MODE)); }
    }
    if (code == 0) return r & ~0xFFull;
    if (code == -1 && !(PipShowing() || Bcm2BottomReady())) return r & ~0xFFull;   // or along the bottom
                                                                            // same without their window
    bool menuOpen = *(int*)(g_Player->ownBcm + BCM_UI_STATE) >= BCM_UI_MENU_OPEN;
    if (code != -1 && !menuOpen) return r & ~0xFFull;   // only the menu button while it's closed
    if (code != -1 && g_Player->menuTurning) return r & ~0xFFull;   // f1.215: nothing while it turns to their page (CtxKeys)
    if (code == -1 && !menuOpen && OwnMenuBlockedByCutscene())
    {   // f1.215 (the author, f1.214 test 23:34:33: "I pressed a menu button from P2 or P3 during a battle cutscene and it soft
        // locked the game"): their menu opened during the cutscene, hidden with their command HUD (f1.209), couldn't be
        // closed and its slowdown ran the cutscene at 1% speed. Their menu (and L2 / R2) doesn't open during one now.
        bool down = (g_Player->cmdButtons & (g_Player->cfg->bMenu | g_Player->cfg->bAlly1 | g_Player->cfg->bAlly2)) != 0;
        bool was = (g_Player->cmdButtonsPrev & (g_Player->cfg->bMenu | g_Player->cfg->bAlly1 | g_Player->cfg->bAlly2)) != 0;
        static LogRate rate;
        if (down && !was && LogOk(rate, 20)) CoopLog("CMD2 MENU: player %d pressed their menu button during a cutscene / scripted shot - their menu doesn't open now", g_Player->num);
        return r & ~0xFFull;
    }
    if (!menuOpen && (g_Player->cmdButtons & g_Player->cfg->bShortcutHold)) return r & ~0xFFull;   // shortcut hold: slot buttons only (f1.270: a closed menu only - in an open one LB = Magnify)
    if (code == -1 && !menuOpen)
    {   // (step C): L2 / R2 (AllyCommand1/2) with their menu closed - open it on an AI teammate's page (like the game's own L2 / R2)
        DWORD ab = g_Player->cfg->bAlly1 | g_Player->cfg->bAlly2;
        bool adown = (g_Player->cmdButtons & ab) != 0, awas = (g_Player->cmdButtonsPrev & ab) != 0;
        if (adown && !awas && !(g_Player->cmdButtons & g_Player->cfg->bMenu))
        {
            int which = (g_Player->cmdButtons & g_Player->cfg->bAlly1) ? 1 : 2;
            int t = PickAllyType(which);
            static ULONGLONG lastLogA[4];
            ULONGLONG nowA = GetTickCount64(); bool logIt = nowA - lastLogA[g_Player->num & 3] > 400; if (logIt) lastLogA[g_Player->num & 3] = nowA;
            if (t >= 0)
            {
                g_Player->aiPageType = t; g_Player->aiPageTick = GetTickCount64();
                if (logIt) CoopLog("CMD2 MENU: player %d pressed AllyCommand%d - their own menu opens on an AI teammate's page (type %d)", g_Player->num, which, t);
                return (r & ~0xFFull) | 1;
            }
            if (logIt) CoopLog("CMD2 MENU: player %d pressed AllyCommand%d but no teammate is played by the AI right now", g_Player->num, which);
        }
    }
    DWORD b = code == -1 ? g_Player->cfg->bMenu : (DWORD)code;
    if (g_FCtxOn && menuOpen && g_CtxGateWas[g_Player->num & 3] && (b & 0x300F)) return r;   // D-pad / A / B go into their menu's UI as key events (CtxKeys) - their command system reads them
                                                                                     // from their own Command_C's key table like player 1's does (0x121A310 = the game's own answer r)
    bool down = (g_Player->cmdButtons & b) != 0, was = (g_Player->cmdButtonsPrev & b) != 0;
    if (code == -1 && down && !was) CoopLog("CMD2 MENU: player %d pressed their menu button", g_Player->num);
    return (r & ~0xFFull) | (down ? (was ? 2 : 1) : 0);
}
static uint64_t WidgetKeyAnswer(void* widget, void* key, uint64_t fname, uint64_t r);
static uint64_t Hook_WidgetKey(void* widget, void* key)
{
    uint64_t fname = key ? *(uint64_t*)key : 0;      // read before the original releases the key
    uint64_t r = Orig_WidgetKey(widget, key);
    return WidgetKeyAnswer(widget, key, fname, r);   // (split from the hook; the f1.163 key DIAG is gone again since f1.167)
}
// (the author: "Multiuse out of battle spell did not work while holding the shift key" - "when I press shift, multi-use
// should turn from gray to white. It does not."). f1.165 DIAG (20:52:22): every frame the game asks WidgetKey('Magnify') -
// the multi-use key (Shift) - twice: in player 1's frame (game 2 = held, passed on) and again inside players 2/3's own
// command system passes, where every key that isn't one of their buttons reads "not pressed" (mod answers 0). The multi-use
// switch is evidently shared, so the last answer of the frame (0) won and it never lit. Magnify is player 1's key in every
// pass now (players 2/3 have no multi-use button).
static bool P1OnlyMenuKey(uint64_t fname)
{
    // f1.211: also asked in player 1's frame now (MultiUseLog) - once Magnify's name is known the answer is one compare;
    // before that the last 128 other key names are remembered (was 32)
    static uint64_t magnify = 0;
    if (magnify) return fname == magnify;
    static uint64_t known[128]; static int n = 0;
    for (int i = 0; i < n; ++i) if (known[i] == fname) return false;
    char nm[64] = ""; alignas(8) unsigned char fake[0x20] = {}; memcpy(fake + 0x18, &fname, 8); NameOf(fake, nm, sizeof(nm));
    bool y = !strcmp(nm, "Magnify");
    if (y) magnify = fname; else if (n < 128) known[n++] = fname;
    if (y) CoopLog("MENUKEYS: the multi-use key (Magnify) is player 1's in every command system pass");
    return y;
}
// f1.211 (the author: "Using the cure spell, after the menu closes it always casts the spell one more time"): log only - when
// the multi-use answer changes in a pass (1 = player 1's frame, 2 / 3 = their own command system pass), whose menus are open
static bool MenuOpenFor(int p);   // menus/39
static void MultiUseLog(int pass, uint64_t r)
{
    static int last[4] = {-1, -1, -1, -1}; static LogRate rate;
    int now = (int)(r & 0xFF);
    int& l = last[pass & 3];
    if (l >= 0 && (now != 0) == (l != 0)) return;
    bool first = l < 0;
    l = now;
    if (first && !now) return;   // (the first "not held" isn't news)
    if (LogOk(rate, 30)) CoopLog("MULTIUSE: multi-use key %s (game answer %d) in %s - menus open: P1 %d, P2 %d, P3 %d",
        now ? "held" : "released", now, pass == 1 ? "player 1's frame" : pass == 2 ? "player 2's command system pass" : "player 3's command system pass",
        g_MenuOwner == 1 ? 1 : 0, MenuOpenFor(2) ? 1 : 0, MenuOpenFor(3) ? 1 : 0);
}
static uint64_t WidgetKeyAnswer(void* widget, void* key, uint64_t fname, uint64_t r)
{
    if (g_InPass2 && !g_InActionPass2)   // the extra movement pass
    {
        bool down, was;
        if (FieldAttackKey(fname, &down, &was))   // shared with passes/05 + seats/13
        {
            static ULONGLONG lastLog = 0;
            if (down && !was && GetTickCount64() - lastLog > 2000) { lastLog = GetTickCount64(); CoopLog("FIELD: player %d attack", g_Player->num); }
            return (r & ~0xFFull) | (down ? (was ? 2 : 1) : 0);
        }
        static uint64_t guardF = 0;
        if (!guardF && g_Base) guardF = KeyFName(RVA_KEY_GUARD);
        if (fname && fname == guardF)   // guard is a held key: player 2's own button
        {
            bool down = (g_Player->padNow & g_Player->cfg->bGuard) != 0, was = (g_Player->fieldPrevButtons & g_Player->cfg->bGuard) != 0;
            return (r & ~0xFFull) | (down ? (was ? 2 : 1) : 0);
        }
        return r & ~0xFFull;   // never player 1's keys
    }
    if (!g_InActionPass2)
    {
        uint64_t a = P1KeyFilter(key, fname, r);
        if (fname && P1OnlyMenuKey(fname)) MultiUseLog(1, a);
        return a;
    }
    if (fname && P1OnlyMenuKey(fname))
    {   // Magnify (multi-use) - player 1's answer in players 2/3's passes too (f1.165: the switch is shared, the last answer
        // of the frame wins). f1.270 (the author: "magnify does not seem to work in the player menu ... it should be
        // triggered by the LB button"): while THEIR own command menu is open, their LB held is the Magnify key there (the
        // game's answer otherwise - player 1's). Their palette can't open with the menu open (BcmBefore: ui 8 only), and
        // their menu keeps taking D-pad / A / B while LB is held (CtxKeys / Bcm2MenuKey: the hold blocks only a closed menu).
        bool menuOpen2 = g_Player->ownBcmOn && g_Player->ownBcm && !g_InMainSysPass && *(int*)(g_Player->ownBcm + BCM_UI_STATE) >= BCM_UI_MENU_OPEN;
        bool lbNow = (g_Player->cmdButtons & PAD_LB) != 0, lbWas = (g_Player->cmdButtonsPrev & PAD_LB) != 0;
        if (menuOpen2 && lbNow)
        {
            uint64_t a = (r & ~0xFFull) | (lbWas ? 2 : 1);
            if (!lbWas) { static LogRate rate; if (LogOk(rate, 20)) CoopLog("MAGNIFY: player %d holds LB in their own command menu - Magnify (multi-use) on for their pick", g_Player->num); }
            MultiUseLog(g_Player->num, a);
            return a;
        }
        MultiUseLog(g_Player->num, r); return r;
    }
    bool ownPal = g_Player->shortcutOwner == g_Player->num;   // this player's palette runs in this pass
    if (g_Player->ownMenuTurn && g_Player->ownBcmOn && !g_InMainSysPass && fname && fname == KeyFName(RVA_KEY_NEXTMEMBER)) { g_Player->ownMenuTurn = false; g_Player->internalTurn = true; return (r & ~0xFFull) | 1; }   // (the mod's own opening turn - not filtered by NextPage)
    if (g_Player->ownBcmOn && !g_InMainSysPass && fname && g_Player->pageReq)   // their LT/RT (or the page guard) turns their own menu's page
    {
        int& rq = g_Player->pageReq;
        if (fname == KeyFName(rq < 0 ? RVA_KEY_PREVMEMBER : RVA_KEY_NEXTMEMBER)) { rq = 0; return (r & ~0xFFull) | 1; }
    }
    if (ownPal && g_Player->shortcutTurn && !g_InMainSysPass && fname && fname == KeyFName(g_Player->shortcutDir < 0 ? RVA_KEY_PREVMEMBER : RVA_KEY_NEXTMEMBER)) { g_Player->shortcutTurn = false; return (r & ~0xFFull) | 1; }
    for (auto& k : g_KeyMap)
    {
        if (k.fname == 0) k.fname = KeyFName(k.rvaFKey);
        if (k.fname == fname)
        {
            // the button comes from the settings file (player 2's or 3's - whoever is loaded; no name compares)
            DWORD button = k.slot ? g_Player->cfg->bShortcut[k.slot - 1] : g_Player->cfg->*k.button;
            // while the shortcut hold is down, the slot buttons pick shortcuts and nothing else;
            // the slot keys do nothing unless it's held.
            bool isSlot = k.slot != 0;
            bool isHold = k.button == &PlayerCfg::bShortcutHold;
            bool holdNow = (g_Player->cmdButtons & g_Player->cfg->bShortcutHold) != 0;
            bool holdWas = (g_Player->cmdButtonsPrev & g_Player->cfg->bShortcutHold) != 0;
            bool down = (g_Player->cmdButtons & button) != 0;
            bool was  = (g_Player->cmdButtonsPrev & button) != 0;
            if ((isSlot || isHold) && (!ownPal || !g_Player->ownBcmOn || g_InMainSysPass)) return r & ~0xFFull;   // player 2's own palette only once claimed (turned to their page); only in their own command system
            if (isSlot && ownPal)   // the slot press is latched in BcmBefore and given once the palette shows the right character
            {
                if (g_Player->shortcutPageOk && g_Player->shortcutPressFName && fname == g_Player->shortcutPressFName) { g_Player->shortcutPressFName = 0; CoopLog("P%d SHORTCUT: %s given to the palette", g_Player->num, k.name); return (r & ~0xFFull) | 1; }
                return r & ~0xFFull;
            }
            if (isSlot) { down = down && holdNow; was = was && holdWas; }
            else if (!isHold && holdNow) { down = false; was = false; }
            if (isSlot && down && !was) CoopLog("P2 SHORTCUT: %s pressed", k.name);
            return (r & ~0xFFull) | (down ? (was ? 2 : 1) : 0);
        }
    }
    if (g_Player->ownBcmOn && g_Player->ownBcm) return Bcm2MenuKey(fname, r);   // (own menus always)
    return r & ~0xFFull; // any other key: not pressed for player 2
}
