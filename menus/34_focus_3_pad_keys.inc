// =================================================================================
// PLAYERS 2 / 3'S PAD AS UI KEYS (restructure 3d, from menus/34): their D-pad / A / B made into the game's UI key events
// inside their own focus context (CtxKeys / CtxSendKey), player 1's real keys kept in his
// =================================================================================
// ---- f1.20 step B: players 2 / 3's pad made into the game's UI keys, inside their own focus context ----
// The game's UI input processor KeyDown 0x1436720 / KeyUp 0x1438280 (vtable 0x4BF24B0 +0x10 / +0x18) take
// (processor, FSlateApplication&, const FKeyEvent&). The real ones (player 1's) are hooked only to remember the
// FSlateApplication and to make sure they always run in player 1's context.
typedef bool (*ProcKey_t)(void* mgr, void* slateApp, void* ev);
static ProcKey_t Orig_ProcKeyDown = nullptr, Orig_ProcKeyUp = nullptr;
static void* g_SlateApp = nullptr;
static bool g_InCtxKey = false;
static bool ProcKeyReal(ProcKey_t orig, void* mgr, void* app, void* ev)
{
    if (app) g_SlateApp = app;
    if (!g_FCtxOn || g_InCtxKey || g_FCtxLive == 1) return orig(mgr, app, ev);
    static int logs = 0; if (logs < 10) { ++logs; CoopLog("CTX: a real key event arrived while player %d's focus context was in - sent to player 1's", g_FCtxLive); }
    int prev = EnterCtx(1); bool r = orig(mgr, app, ev); LeaveCtx(prev); return r;
}
static bool Hook_ProcKeyDown(void* mgr, void* app, void* ev) { return ProcKeyReal(Orig_ProcKeyDown, mgr, app, ev); }
typedef void (*ProcTick_t)(void* mgr, float dt, void* slateApp, void* cursor);   // vtable +0x08 0x1434550 (cursor = TSharedRef by value, consumed by it)
static ProcTick_t Orig_ProcTick = nullptr;
static void Hook_ProcTick(void* mgr, float dt, void* app, void* cursor)
{
    if (app) g_SlateApp = app;   // known from the first frame (not only after player 1's first key)
    if (!g_FCtxOn || g_FCtxLive == 1) { Orig_ProcTick(mgr, dt, app, cursor); return; }
    int prev = EnterCtx(1); Orig_ProcTick(mgr, dt, app, cursor); LeaveCtx(prev);
}
static bool Hook_ProcKeyUp(void* mgr, void* app, void* ev) { return ProcKeyReal(Orig_ProcKeyUp, mgr, app, ev); }
// player p's (stored) focus path leads into their own Command_C (an SEndObjectWidget, vtable 0x4BE4E68, whose +0x4E8
// user widget is their Command_C) and its leaf is alive. Call while player p's context is NOT the live one.
static bool CtxGate(int p, int* outN)
{
    *outN = 0;
    if (p < 2 || p > 3 || g_FCtxLive == p) return false;
    const FocusCtx& c = g_FCtx[p];
    unsigned char* d = *(unsigned char* const*)(c.path); int n = *(const int*)(c.path + 8);
    *outN = n;
    if (n <= 0 || n > 256 || !d || !Readable(d, (size_t)n * 16)) return false;
    unsigned char* lc = *(unsigned char**)(d + (size_t)(n - 1) * 16 + 8);
    if (!lc || !Readable(lc, 0x10) || *(int*)(lc + 8) <= 0) return false;
    void* cc = CmdCOf(p);
    if (!cc) return false;
    for (int i = 0; i < n; ++i)
    {
        unsigned char* o = *(unsigned char**)(d + (size_t)i * 16); unsigned char* k = *(unsigned char**)(d + (size_t)i * 16 + 8);
        if (!o || !k || !Readable(k, 0x10) || *(int*)(k + 8) <= 0 || !Readable(o, 0x4F0)) continue;
        if (*(uintptr_t*)o == g_Base + 0x4BE4E68 && *(void**)(o + 0x4E8) == cc) return true;
    }
    return false;
}
// one made key event for player p (down / repeat / up) in their own context; returns "handled"
static bool CtxSendKey(int p, const char* key, bool down, bool rep)
{
    if (p >= 2 && down && !rep && key && strstr(key, "FaceButton_Bottom")) g_CtxConfirmT[p & 3] = GetTickCount64();                // (not for p = 1, a turn in player 1's menu)
    unsigned char* mgr = FocusMgr();
    uint64_t fn = GpKeyF(key);
    if (!mgr || !Orig_ProcKeyDown || !Orig_ProcKeyUp || !fn || !g_SlateApp) return false;
    alignas(16) unsigned char ev[0x40] = {};
    *(void**)ev = (void*)(g_Base + 0x4BE0370);   // FKeyEvent vtable; modifiers 0, user 0, no event path, no key details (looked up by the game)
    ev[0x10] = rep ? 1 : 0;
    *(uint64_t*)(ev + 0x20) = fn;
    unsigned char snap[0x1A]; memcpy(snap, mgr + 0x52, sizeof(snap));   // "last input user / device" flags stay player 1's
    void* p1d = *(void**)(mgr + 0x08); int p1n = PathNum(mgr);
    void* p1leaf = (p1n > 0 && p1d) ? *(void**)((unsigned char*)p1d + (size_t)(p1n - 1) * 16) : nullptr;
    int prev = EnterCtx(p);
    if (g_FCtxLive != p) return false;
    int n0 = PathNum(mgr);
    g_InCtxKey = true;
    bool h = down ? Orig_ProcKeyDown(mgr, g_SlateApp, ev) : Orig_ProcKeyUp(mgr, g_SlateApp, ev);
    g_InCtxKey = false;
    int n1 = PathNum(mgr); int leaf = PathLeafOwner(mgr);
    LeaveCtx(prev);
    memcpy(mgr + 0x52, snap, sizeof(snap));
    void* q1d = *(void**)(mgr + 0x08); int q1n = PathNum(mgr);
    void* q1leaf = (q1n > 0 && q1d) ? *(void**)((unsigned char*)q1d + (size_t)(q1n - 1) * 16) : nullptr;
    static int logs = 0;
    bool p1moved = q1n != p1n || q1leaf != p1leaf;
    if ((logs < 300 && !rep) || p1moved)
    {
        if (logs < 400) { ++logs; CoopLog("CTX: player %d key %s %s | their path %d -> %d, leaf %s | player 1's path %s (%d entr(ies))",
            p, key, down ? (rep ? "repeat" : "down") : "up", n0, n1, OwnTag(leaf), p1moved ? "CHANGED" : "unchanged", q1n); }
    }
    return h;
}
static DWORD g_CtxSent[4] = {0, 0, 0, 0}, g_CtxLatch[4] = {0, 0, 0, 0};   // (g_CtxGateWas: menus/08)
static const struct CtxKeyDef { DWORD b; const char* k; bool rep; } g_CtxKm[6] = {
    {0x0001, "Gamepad_DPad_Up", true}, {0x0002, "Gamepad_DPad_Down", true}, {0x0004, "Gamepad_DPad_Left", true}, {0x0008, "Gamepad_DPad_Right", true},
    {0x1000, "Gamepad_FaceButton_Bottom", false}, {0x2000, "Gamepad_FaceButton_Right", false} };   // (mask 0x300F - see Bcm2MenuKey)
// player context: every key still held in their UI gets its key-up (menu closed, battle over, their pass skipped)
static void CtxReleaseAll()
{
    int p = g_Player->num & 3;
    if (g_FCtxOn) for (int i = 0; i < 6; ++i) if (g_CtxSent[p] & g_CtxKm[i].b) CtxSendKey(p, g_CtxKm[i].k, false, false);
    g_CtxSent[p] = 0;
    g_CtxGateWas[p] = false;   // their command system answers from the pad again (Bcm2MenuKey)
}
// player context (num), before their own command system's update: pad edges -> key events while their own menu is
// open and their focus path is in it. D-pad (with repeats), A (confirm), B (back) only - step B.
static void CtxKeys()
{
    int p = g_Player->num & 3;
    const CtxKeyDef* km = g_CtxKm;
    static ULONGLONG downT[4][6], repT[4][6];
    DWORD now = g_Player->cmdButtons;
    DWORD& sent = g_CtxSent[p]; DWORD& latch = g_CtxLatch[p];
    latch &= now;   // released buttons count again
    int pn = 0;
    bool menu = g_Player->ownBcm && g_Player->ownBcmOn && *(int*)(g_Player->ownBcm + BCM_UI_STATE) >= BCM_UI_MENU_OPEN;
    bool gate = menu && !g_GamePaused && g_SlateApp && CtxGate(p, &pn);   // f1.270: LB (the shortcut hold) no longer closes the gate - in an open menu it is Magnify
    if (gate != g_CtxGateWas[p])
    {
        static LogRate rate;   // f1.216: was 60 lines a session (it ran out before the f1.214 soft lock) - now 30 a minute
        if (LogOk(rate, 30)) { CoopLog("CTX: player %d's own command menu %s their pad (ui %d, their focus path %d entr(ies)%s)", p, gate ? "now takes" : "no longer takes", g_Player->ownBcm ? *(int*)(g_Player->ownBcm + BCM_UI_STATE) : -1, pn,
            gate ? "" : (!menu ? ", menu closed" : g_GamePaused ? ", paused" : !g_SlateApp ? ", no key event seen yet" : (now & g_Player->cfg->bShortcutHold) ? ", shortcut held" : ", path not in their Command_C")); }
        if (gate) latch = now;   // buttons already down (the one that opened the menu) count only after a release
        g_CtxGateWas[p] = gate;
    }
    static int phase[4] = {};   // f1.216 cutscene Cancel: 1 = its "B down" was sent, the "B up" is due next call
    bool cutCancel = !gate && menu && (g_InCutscene || g_ScriptedShot) && g_FCtxOn && g_SlateApp;
    if (phase[p] == 1 && !cutCancel)
    {   // f1.273 (code review #56): the "B down" closed the menu (or the cutscene ended) - the "B up" still goes out, else B
        // stayed down in their focus context and the next cutscene began with a stray key-up
        if (g_FCtxOn && g_SlateApp) CtxSendKey(p, "Gamepad_FaceButton_Right", false, false);
        phase[p] = 0; g_CtxAutoClose[p] = 0;
    }
    if (cutCancel)
    {   // f1.216 backup: their menu is open in a cutscene but doesn't take their pad - the cutscene close (menus/10
        // OwnCmdHudCutscene) and B are sent into their own focus context anyway (a made Cancel, down then up), logged
        static bool bWas[4] = {};
        bool bNow = (now & 0x2000) != 0;
        if (phase[p] == 1) { CtxSendKey(p, "Gamepad_FaceButton_Right", false, false); phase[p] = 0; g_CtxAutoClose[p] = 0; }
        else if (g_CtxAutoClose[p] || (bNow && !bWas[p]))
        {
            bool h = CtxSendKey(p, "Gamepad_FaceButton_Right", true, false); phase[p] = 1;
            static LogRate rate;
            if (LogOk(rate, 20)) CoopLog("CMD2 MENU: player %d's menu is open in a cutscene but doesn't take their pad (focus path %d entr(ies)) - Cancel sent anyway (%s)", p, pn, h ? "handled" : "not handled");
        }
        bWas[p] = bNow;
        return;
    }
    if (!gate) { CtxReleaseAll(); g_CtxAutoClose[p] = 0; return; }
    ULONGLONG t = GetTickCount64();
    if (g_CtxAutoClose[p])
    {   // (OwnMenuCloseAfterCommand): a made Cancel closes their menu - down this frame, up the next (their own keys wait)
        bool dn = g_CtxAutoClose[p] == 1;
        CtxSendKey(p, "Gamepad_FaceButton_Right", dn, false);
        g_CtxAutoClose[p] = dn ? 2 : 0;
        if (!dn) latch = now;   // whatever they hold now counts only after a release
        return;
    }
    if (g_Player->menuTurning)
    {   // f1.215 (the author, f1.214 test 23:28:26: "I was able to open P1's menu from P2 by pressing fast"): while their own
        // menu is hidden and turning to their page it passes through player 1's page - an A pressed then picked from player
        // 1's commands and stopped the turn. Their keys wait until it is on their page; a button pressed meanwhile counts
        // only after it is let go.
        for (int i = 0; i < 6; ++i) if (sent & km[i].b) CtxSendKey(p, km[i].k, false, false);
        sent = 0; latch |= now;
        static DWORD prevW[4] = {};   // f1.218: logged once per press (f1.217: once a frame while held - 187 lines)
        DWORD newly = (now & ~prevW[p]) & 0x300F; prevW[p] = now;
        static LogRate rate;
        if (newly && LogOk(rate, 20)) CoopLog("CMD2 MENU: player %d's keys wait - their menu is still turning to their page (pressed 0x%04lX)", p, newly);
        return;
    }
    for (int i = 0; i < 6; ++i)
    {
        DWORD b = km[i].b; bool d = (now & b) != 0, s = (sent & b) != 0;
        if (d && !s && !(latch & b)) { sent |= b; downT[p][i] = repT[p][i] = t; CtxSendKey(p, km[i].k, true, false); }
        else if (!d && s) { sent &= ~b; CtxSendKey(p, km[i].k, false, false); }
        else if (d && s && km[i].rep && t - downT[p][i] > 350 && t - repT[p][i] > 110) { repT[p][i] = t; CtxSendKey(p, km[i].k, true, true); }
    }
}
static void InstallFocusLogHooks()
{
    g_FCtxOn = true;   // (always - OwnCommandMenu / the Shared menu style is gone)
    FCtxInitOne(g_FCtx[1]); FCtxInitOne(g_FCtx[2]); FCtxInitOne(g_FCtx[3]);
    static const unsigned char exPush[16]  = {0x48,0x8B,0xC4,0x48,0x89,0x50,0x10,0x53,0x48,0x83,0xEC,0x60,0x48,0x89,0x68,0x18};
    static const unsigned char exPushR[16] = {0x48,0x8B,0xC4,0x44,0x88,0x48,0x20,0x48,0x89,0x50,0x10,0x53,0x48,0x83,0xEC,0x70};
    static const unsigned char exPop[16]   = {0x48,0x8B,0xC4,0x48,0x89,0x50,0x10,0x53,0x48,0x83,0xEC,0x60,0x48,0x89,0x68,0x08};
    static const unsigned char exFocus[14] = {0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
    static const unsigned char exReset[15] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20};
    void* t;
    if ((t = MakeInlineHook(g_Base + 0x143D920, (void*)Hook_FPush, 16, exPush, "focus stack Push (log)"))) Orig_FPush = (FPush_t)t;
    if ((t = MakeInlineHook(g_Base + 0x143D560, (void*)Hook_FPushR, 16, exPushR, "focus stack Push+restore (log)"))) Orig_FPushR = (FPushR_t)t;
    if ((t = MakeInlineHook(g_Base + 0x143DC50, (void*)Hook_FPop, 16, exPop, "focus stack Pop (log)"))) Orig_FPop = (FPop_t)t;
    if ((t = MakeInlineHook(g_Base + 0x143BAF0, (void*)Hook_FSetFocus, 14, exFocus, "SetFocus (log)"))) Orig_FSetFocus = (FSetFocus_t)t;
    if ((t = MakeInlineHook(g_Base + 0x143C8F0, (void*)Hook_FSetChild, 14, exFocus, "SetFocusToChild (log)"))) Orig_FSetChild = (FSetChild_t)t;
    if ((t = MakeInlineHook(g_Base + 0x1433790, (void*)Hook_FReset, 15, exReset, "focus reset (log)"))) Orig_FReset = (FReset_t)t;
    if (g_FCtxOn)
    {
        bool ok = Orig_FPush && Orig_FPushR && Orig_FPop && Orig_FSetFocus && Orig_FSetChild;
        uintptr_t* st = (uintptr_t*)(g_Base + 0x4BF24B8);   // processor Tick / KeyDown / KeyUp slots
        uintptr_t* sd = (uintptr_t*)(g_Base + 0x4BF24C0); uintptr_t* su = (uintptr_t*)(g_Base + 0x4BF24C8);
        if (ok && *st == g_Base + 0x1434550 && *sd == g_Base + 0x1436720 && *su == g_Base + 0x1438280)
        {
            Orig_ProcTick = (ProcTick_t)*st; Orig_ProcKeyDown = (ProcKey_t)*sd; Orig_ProcKeyUp = (ProcKey_t)*su;
            DWORD old; VirtualProtect(st, 3 * sizeof(uintptr_t), PAGE_READWRITE, &old);
            *st = (uintptr_t)Hook_ProcTick; *sd = (uintptr_t)Hook_ProcKeyDown; *su = (uintptr_t)Hook_ProcKeyUp;
            VirtualProtect(st, 3 * sizeof(uintptr_t), old, &old);
            CoopLog("CTX: per-player focus contexts ON: players 2/3's command menus keep their own focus; their pad's D-pad / A / B go into their own menu");
        }
        else
        {
            g_FCtxOn = false;
            CoopLog("CTX: per-player focus contexts OFF - a focus hook failed or the UI processor slots hold %p / %p / %p (expected 1434550 / 1436720 / 1438280) - players 2/3's own menus won't get their keys",
                (void*)(*st - g_Base), (void*)(*sd - g_Base), (void*)(*su - g_Base));
        }
    }
}
