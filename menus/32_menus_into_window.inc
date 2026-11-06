// (review B8): UWidget's SetRenderTranslation - the same game function for every widget, looked up once and shared by the
// three widget movers (the target cursor and player 1's Command_C here, players 2/3's own Command_C in menus/35). Before,
// each looked it up itself and had its own copy of the setter.
static void* g_SetTrFn = nullptr; static int g_SetTrP = -1;
static bool SetTrFind(void* w)   // w = any live widget (may be null); true once the function is known
{
    if (g_SetTrFn) return true;
    if (!w) return false;
    void* f = FindFunc(w, "SetRenderTranslation"); if (!f) return false;
    int p = ParamOff(f, "Translation"); if (p < 0 || ParmsSize(f) > 32) return false;
    g_SetTrP = p; g_SetTrFn = f;
    return true;
}
static void SetRenderTr(void* w, float tx, float ty)
{
    alignas(16) unsigned char b[32] = {}; float v[2] = {tx, ty};
    memcpy(b + g_SetTrP, v, 8);
    Orig_ProcessEvent(w, g_SetTrFn, b);
}
// (the author, Thirds test: "It does overlap. Part of the issue for this is we don't have any resolution size for the left
// side hud/menu/shortcuts in this configuration"; "set the defaults to ... 90/90/75"): the command HUD / menus / shortcut
// palettes (each player's Command_C - everything of theirs is laid out inside it) get a size per view size, like the party
// panels: [PictureInPicture] CommandMenuScale (a half-screen view) / CommandMenuScaleThird / CommandMenuScaleQuarter, 50 - 100.
// UWidget SetRenderScale + SetRenderTransformPivot (0, 1): the widget is screen-sized, so it shrinks toward the bottom-left
// corner of the view, which stays where the translation puts it. The translation is in the parent's units - the measured
// scale (HcRect) includes ours, so it's divided out again. Full screen (no split) = 100 %.
static int g_WsState = 0; static void* g_WsScaleFn = nullptr; static void* g_WsPivotFn = nullptr; static int g_WsScaleP = -1, g_WsPivotP = -1;
static bool WidgetScale(void* w, float sc, float pivX, float pivY)
{
    if (!w) return false;
    if (g_WsState == 0)
    {
        void* f = FindFunc(w, "SetRenderScale"); void* fp = FindFunc(w, "SetRenderTransformPivot");
        int p = f ? ParamOff(f, "Scale") : -1, pp = fp ? ParamOff(fp, "Pivot") : -1;
        g_WsState = (f && fp && p >= 0 && pp >= 0 && ParmsSize(f) <= 32 && ParmsSize(fp) <= 32) ? 1 : -1;
        if (g_WsState == 1) { g_WsScaleFn = f; g_WsScaleP = p; g_WsPivotFn = fp; g_WsPivotP = pp; }
        CoopLog("CMDMENU: command HUD scaling %s", g_WsState == 1 ? "ready (SetRenderScale + SetRenderTransformPivot)" : "NOT available - CommandMenuScale does nothing");
    }
    if (g_WsState != 1) return false;
    float piv[2] = {sc < 0.999f ? pivX : 0.5f, sc < 0.999f ? pivY : 0.5f}, v[2] = {sc, sc};
    alignas(16) unsigned char b[32] = {}; memcpy(b + g_WsPivotP, piv, 8); Orig_ProcessEvent(w, g_WsPivotFn, b);
    alignas(16) unsigned char c[32] = {}; memcpy(c + g_WsScaleP, v, 8); Orig_ProcessEvent(w, g_WsScaleFn, c);
    return true;
}
// frac = the view's share of the screen: under 0.3 = a quarter, under 0.42 = a third, under 0.75 = a half, else full (100 %)
static float CmdScaleWanted(float frac)
{
    if (frac >= 0.75f) return 1.f;
    int p = frac < 0.3f ? g_Cfg.cmdScale4 : frac < 0.42f ? g_Cfg.cmdScale3 : g_Cfg.cmdScale;
    if (p < 50) p = 50;
    if (p > 100) p = 100;
    return p / 100.f;
}

// player 2/3's command menu / shortcut palette (Command_* widgets: Command_C, its cells) sits
// at the bottom left of the whole screen = over player 1's. While that player owns it, shift it to the
// bottom left of their window. Widgets nested in another Command_* widget move with their parent.
struct CmMovedW { int idx, ser; float tx, ty, ox, oy; int setAt; ULONGLONG t; float px, py; };   // px/py = the pixel offset it is being moved by   // v14.0: ox/oy = on-screen position before any shift (oy = its BOTTOM - the bottom-left corner doesn't move when it's scaled)
static float g_CmScaleNow = 1.f;   // the scale on player 1's Command_C(s) now (CmScaleApply)
static CmMovedW g_CmMoved[24]; static int g_CmN = 0; static ULONGLONG g_CmLast = 0; static int g_CmFrame = 0;
static int g_CmState = 0;
static void CmSet(void* w, float tx, float ty) { if (g_CmState == 1) SetRenderTr(w, tx, ty); }
static void CmRestoreAll()
{
    for (int i = 0; i < g_CmN; ++i) { void* w = ResolveWeak(g_CmMoved[i].idx, g_CmMoved[i].ser); if (w) CmSet(w, 0.f, 0.f); }
    if (g_CmN) CoopLog("CMDMENU: command menu / palette back in its normal place");
    g_CmN = 0;
}
static void CmdMenuTick()   // every frame (game thread): put the widgets back once nobody's window draws them
{
    if (g_CmN && GetTickCount64() - g_CmLast > 150) CmRestoreAll();
}
static bool CmSetup(void* canvas)
{
    if (g_Hc.state == 0 && !HcResolve(canvas)) return false;
    if (g_Hc.state != 1) return false;
    if (g_CmState == 0)
    {
        g_CmState = -1;
        for (int i = 0; i < g_RwN && !g_SetTrFn; ++i) SetTrFind(ResolveWeak(g_RwIdx[i], g_RwSer[i]));
        if (SetTrFind(nullptr)) g_CmState = 1;
        CoopLog("CMDMENU: SetRenderTranslation %s", g_CmState == 1 ? "found" : "NOT found - player 2/3's menu stays over player 1's");
    }
    if (g_CmState != 1) return false;
    return true;
}
// player 1's Command_C(s) (not players 2/3's own) at this scale - every frame while not 100 %; the ones we scaled are
// remembered so they go back to 100 % even when they're no longer in the shown-widget list
static int g_CmScIdx[8], g_CmScSer[8], g_CmScN = 0;
static void CmScaleApply(float sc, const char* why)
{
    if (sc >= 0.999f && g_CmScaleNow >= 0.999f && g_CmScN == 0) return;
    for (int i = 0; i < g_RwN; ++i)
    {
        void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]);
        if (!w || !ClassIs(*(void**)((unsigned char*)w + 0x10), "Command_C")) continue;
        if (w == Bcm2Widget() || (g_OtherPlayer->ownBcm && w == BcmCmdC(g_OtherPlayer->ownBcm))) continue;   // players 2 / 3's own (menus/35)
        if (!WidgetScale(w, sc, 0.f, 1.f)) return;
        if (sc < 0.999f)
        {
            int ix, se; bool known = false;
            if (MakeWeak(w, &ix, &se)) { for (int k = 0; k < g_CmScN; ++k) if (g_CmScIdx[k] == ix && g_CmScSer[k] == se) known = true;
                if (!known && g_CmScN < 8) { g_CmScIdx[g_CmScN] = ix; g_CmScSer[g_CmScN] = se; ++g_CmScN; } }
        }
    }
    if (sc >= 0.999f)
    {   // back to 100 %: also the ones not shown right now
        for (int k = 0; k < g_CmScN; ++k) { void* w = ResolveWeak(g_CmScIdx[k], g_CmScSer[k]); if (w) WidgetScale(w, 1.f, 0.5f, 0.5f); }
        g_CmScN = 0;
    }
    if (fabsf(sc - g_CmScaleNow) > 0.001f)
    {
        static int logs = 0; if (logs < 30) { ++logs; CoopLog("CMDMENU: player 1's command HUD / menus at %.0f%% size (%s - CommandMenuScale)", sc * 100.f, why); }
        g_CmScaleNow = sc;
    }
}
// shift player 1's Command_C container(s) by (pxx, pxy) pixels from where the game lays them out
static void CmMoveTo(float pxx, float pxy, const char* who, const char* where, bool diag, float wx, float wy, float ww, float wh)
{
    g_CmLast = GetTickCount64(); ++g_CmFrame;
    for (int i = 0; i < g_RwN; ++i)
    {
        void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]);
        if (!w || !ClassIs(*(void**)((unsigned char*)w + 0x10), "Command_C")) continue;   // the container only - everything else is laid out inside it
        if (w == Bcm2Widget() || (g_OtherPlayer->ownBcm && w == BcmCmdC(g_OtherPlayer->ownBcm))) continue;   // players 2 / 3's own Command_C
        // some Command_ widgets (the shortcut slots) are laid out inside the palette without being its
        // children by Outer, so the parent's shift carries them too. Every widget is measured where it really
        // is on screen and its own shift corrected until it sits exactly window-offset from where it started.
        float x, y, rw, rh, sc;
        if (!HcRect(w, &x, &y, &rw, &rh, &sc) || sc <= 0.f) continue;
        sc /= g_CmScaleNow > 0.f ? g_CmScaleNow : 1.f;   // the parent's units (our own scale divided out)
        y += rh;                                          // track the bottom-left corner (doesn't move with our scale)
        int slot = -1; for (int k = 0; k < g_CmN; ++k) if (g_CmMoved[k].idx == g_RwIdx[i] && g_CmMoved[k].ser == g_RwSer[i]) slot = k;
        if (slot < 0)
        {
            if (g_CmN >= 24) continue;
            slot = g_CmN++; CmMovedW& m = g_CmMoved[slot];
            m.idx = g_RwIdx[i]; m.ser = g_RwSer[i]; m.ox = x; m.oy = y; m.tx = pxx / sc; m.ty = pxy / sc; m.setAt = g_CmFrame; m.px = pxx; m.py = pxy;
            CmSet(w, m.tx, m.ty);
            static int logs = 0; if (logs < 12) { ++logs; char nm[96]; NameOf(*(void**)((unsigned char*)w + 0x10), nm, sizeof(nm)); CoopLog("CMDMENU: %s %s moved %s (%.0f, %.0f px; was at %.0f, %.0f)", who, nm, where, pxx, pxy, x, y); }
        }
        else
        {
            CmMovedW& m = g_CmMoved[slot];
            if (fabsf(pxx - m.px) > 0.5f || fabsf(pxy - m.py) > 0.5f)
            {   // a new place (player 1's own part <-> a player 2/3 turn in their window) - jump straight there
                m.tx += (pxx - m.px) / sc; m.ty += (pxy - m.py) / sc; m.px = pxx; m.py = pxy; m.setAt = g_CmFrame;
                static int logs = 0; if (logs < 20) { ++logs; char nm[96]; NameOf(*(void**)((unsigned char*)w + 0x10), nm, sizeof(nm)); CoopLog("CMDMENU: %s %s moves %s (%.0f, %.0f px)", who, nm, where, pxx, pxy); }
            }
            else if (g_CmFrame - m.setAt >= 3)   // the layout lags a frame or two behind a change
            {
                float ex = (m.ox + pxx) - x, ey = (m.oy + pxy) - y;
                if (fabsf(ex) > 1.5f || fabsf(ey) > 1.5f)
                {
                    m.tx += ex / sc; m.ty += ey / sc; m.setAt = g_CmFrame;
                    static int logs = 0; if (logs < 12) { ++logs; char nm[96]; NameOf(*(void**)((unsigned char*)w + 0x10), nm, sizeof(nm)); CoopLog("CMDMENU: %s was %.0f, %.0f px off - corrected (shift now %.0f, %.0f)", nm, -ex, -ey, m.tx, m.ty); }
                }
            }
            CmSet(w, m.tx, m.ty);
        }
        g_CmMoved[slot].t = g_CmLast;
        {   // DIAG (test of f1.25: player 3's shared-menu turn while leading showed on player 1's half): where the
            // container really is on screen vs where it should be, once a second during a turn (max 60 lines)
            static ULONGLONG lastD = 0; static int dlogs = 0; ULONGLONG nd = GetTickCount64();
            if (diag && nd - lastD > 1000 && dlogs < 60)
            {
                lastD = nd; ++dlogs; CmMovedW& m = g_CmMoved[slot];
                CoopLog("CMDMENU DIAG: player %d's turn - Command_C %p on screen at %.0f, %.0f (size %.0f x %.0f, scale %.2f), should be %.0f, %.0f; shift %.0f, %.0f; window %.0f, %.0f %.0f x %.0f; P3 window %d, leader player %d",
                    g_Player->num, w, x, y, rw, rh, sc, m.ox + pxx, m.oy + pxy, m.tx, m.ty, wx, wy, ww, wh, (int)g_P3WinOn, 1);
            }
        }
    }
}
// (the author: with players 2/3 on the left / at the bottom, player 3's menu covered player 1's): while the split-screen
// windows show, player 1's own command menu goes to the bottom left of player 1's part of the screen (right half with
// players 2/3 on the left, top half with them at the bottom - the other layouts already have it there). (always -
// Player1MenuInOwnView is no longer a setting)
static void CmdMenuP1Place(void* canvas)
{
    if (!AnyWindowShowing() || g_InCutscene || g_RwN <= 0) return;   // (was player 2's window only)
    int side = SplitSide();   // thirds = player 1 on the left, where the game puts the menu anyway
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    if (g_HudSpanOk)
    {   // f1.311: the HUD box is already player 1's part (monitors / one wide screen - 31b) - the menu stays where the game puts
        // it. f1.315 (the author, f1.314 test: "P1 command menu is on the right"): no CmMoveTo here - its "where it started"
        // was measured under another HUD scale and pulled the menu across; just any earlier move undone
        if (g_CmN) CmRestoreAll();
        return;
    }
    if (SplitThirds())
    {   // (the author: "Can the menus be shifted left slightly (left side menus) ... for the 1/3rd screen config"):
        // [PictureInPicture] ThirdsCommandNudge px (at 1080p) to the left
        float n = g_Cfg.thirdsCmdNudge * clipY / 1080.f;
        if (n < 0.5f || !CmSetup(canvas)) return;
        g_Crumb = "cmd menu: P1 thirds nudge";
        CmMoveTo(-n, 0.f, "player 1's", "a little to the left (Thirds - ThirdsCommandNudge)", false, 0.f, 0.f, clipX / 3.f, clipY);
        g_Crumb = "-";
        return;
    }
    if (side != 0 && side != 3) return;
    if (!CmSetup(canvas)) return;
    g_Crumb = "cmd menu: P1 place";
    float pxx = side == 0 ? clipX * 0.5f : 0.f, pxy = side == 3 ? -clipY * 0.5f : 0.f;
    float wx = side == 0 ? clipX * 0.5f : 0.f, wy = 0.f, ww = side == 0 ? clipX * 0.5f : clipX, wh = side == 3 ? clipY * 0.5f : clipY;
    CmMoveTo(pxx, pxy, "player 1's", "into player 1's part of the screen", false, wx, wy, ww, wh);
    g_Crumb = "-";
}
// player 1's command HUD / menus size - player 1's part of the screen while a split-screen window shows (any
// layout), else 100 %
static void CmdMenuP1Scale()
{
    if (g_RwN <= 0) return;
    bool split = AnyWindowShowing() && !g_InCutscene;
    CmScaleApply(split ? CmdScaleWanted(SplitP1Frac()) : 1.f, split ? (SplitThirds() ? "player 1's third of the screen" : "player 1's half of the screen") : "no split screen");
}

