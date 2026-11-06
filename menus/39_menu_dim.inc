// (the author: "when the menus open it naturally darkens the screen. I'd like to be able to turn that option off if
// possible"). RE 2026-10-22 (UE4SS dump + exe): the game shows full-screen "UI filters" (EEndMenuUIFilterType: 0 WaitMode,
// 1 Dying, 2 MainMenu, 3 MainMenuParticle, 4 MainMenuFade, 5 MainMenuMono). Show = 0x14C0220(filter manager, type) - a
// count per type at +0x24 + type*4; the first one calls 0x14BF920(manager, type, instant) which makes the filter. The
// command menus' darkening is most likely WaitMode (type 0; Command_C also holds a "WaitMode" widget). 0x14BF920 is called
// directly (inlined counting) from two battle-menu functions (0xE5A3D6, 0x1419F15) with type 0.
// [General] MenuDarken=0: type 0 is never made (types 1 - 5 are left alone). The hide side finds no filter and does nothing.
// f1.208: MenuDarken is for ONE shared screen only; while the split screen shows, type 0 is never made and each view
// darkens on its own (MenuDarkenSplitPlayer1 / 2 / 3 - below).
// MENUDIM log lines: which filter types are made (the first 40), so the test shows whether WaitMode is the darkening.
//
// (the author: "Can the near death filter be applied to each screen only? That way a player knows their character is
// in trouble, and not just adding a red filter anytime someone is in the red?"): while the split screen shows, the game's
// Dying filter (type 1, whole screen) isn't made; instead each view gets its own red edge (pulsing) while THAT player's
// character is alive and at or below [General] DangerHealth % of their max HP (default 25) in a battle - player 2's / 3's
// windows and player 1's part of the screen. [General] DangerPerScreen=0 = the game's own filter as before. Without the
// split screen (one shared view) the game's filter is left alone.
static volatile bool g_SplitShowingNow = false;   // player 2's window was drawn this frame (PipDrawUnderHud)
typedef void (*FilterMake_t)(void* mgr, uint8_t type, uint8_t instant);
static FilterMake_t Orig_FilterMake = nullptr;
static void Hook_FilterMake(void* mgr, uint8_t type, uint8_t instant)
{
    static const char* const kNames[6] = {"WaitMode (command menu)", "Dying", "MainMenu", "MainMenuParticle", "MainMenuFade", "MainMenuMono"};
    // f1.208: while the split screen shows, the command menus' darkening (type 0) is never the game's whole-screen filter -
    // each view darkens on its own (MenuDimView / MenuDimP1, MenuDarkenSplitPlayerN). One shared screen: MenuDarken.
    bool block = (type == 0 && (g_SplitShowingNow || !g_Cfg.menuDarken)) || (type == 1 && g_Cfg.dangerPerScreen && g_SplitShowingNow);
    static int logs = 0;
    if (logs < 40) { ++logs; CoopLog("MENUDIM: the game's %s screen filter %s", type < 6 ? kNames[type] : "?",
        !block ? "shown" : type == 0 ? (g_SplitShowingNow ? "NOT shown - split screen: each view darkens on its own (MenuDarkenSplitPlayerN)" : "NOT shown (MenuDarken=0)")
                                     : "NOT shown - each split-screen view shows its own player's (DangerPerScreen)"); }
    if (block) return;
    Orig_FilterMake(mgr, type, instant);
}
// true while this character is alive and at / below DangerHealth of their max HP (in a battle)
static bool CharInDanger(void* ch)
{
    if (!ch || !InBattleNow() || g_GamePaused || g_InCutscene || g_ScriptedShot || g_MainMenuUp) return false;   // not over the pause / main menu
    int hp = 0, mx = 0;
    if (!EnemyHP(ch, &hp, &mx)) return false;   // (the game's GetCharaHP / GetCharaHPMax - any character)
    return hp > 0 && (float)hp <= (float)mx * g_Cfg.dangerHealth;
}
// a soft red vignette inside the rectangle - f1.107 (the author, f1.106 screenshot: "That's crazy ... it would need to be a
// subtle fading/tint effect like the original. This is tacky."): f1.106 drew it with canvas lines, which ignore the color's
// alpha (solid red bands). Now translucent tiles of the engine's white texture (K2_DrawTexture, BLEND_Translucent): 24 thin
// bands over 16 % of the view, at most 22 % strength at the very edge fading to nothing, a slow pulse (1.6 s, 75 - 100 %).
static void DangerEdge(void* canvas, float x, float y, float w, float h)
{
    float m = w < h ? w : h; if (m < 50.f || !g_CF.white || !SmResolveTex(canvas)) return;
    const int N = 24; float s = m * 0.16f / N;
    float t = (float)(GetTickCount64() % 1600) / 1600.f;
    float pulse = 0.875f + 0.125f * sinf(t * 6.2831853f);
    for (int i = 0; i < N; ++i)
    {
        float f = 1.f - (float)i / N, d = s * i;
        float col[4] = {0.75f, 0.02f, 0.02f, 0.22f * pulse * f * f * f};
        SmTile(canvas, g_CF.white, x + d, y + d, w - 2.f * d, s, col, 2, 0.f);                   // top
        SmTile(canvas, g_CF.white, x + d, y + h - d - s, w - 2.f * d, s, col, 2, 0.f);           // bottom
        SmTile(canvas, g_CF.white, x + d, y + d + s, s, h - 2.f * d - 2.f * s, col, 2, 0.f);     // left
        SmTile(canvas, g_CF.white, x + w - d - s, y + d + s, s, h - 2.f * d - 2.f * s, col, 2, 0.f);   // right
    }
}
static void DangerLog(int p, bool on)
{
    static bool was[4] = {false, false, false, false}; static int logs = 0;
    if (on == was[p & 3]) return;
    was[p & 3] = on;
    if (logs < 60) { ++logs; CoopLog("DANGER: player %d's character %s", p, on ? "is low on health - red edge in their view" : "is out of danger (or down) - red edge off"); }
}
// player 1's part of the screen while the split screen shows (false = the canvas is too small to place it)
static bool P1PartRect(void* canvas, float* px, float* py, float* pw, float* ph)
{
    float cx = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), cy = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (cx < 200.f || cy < 100.f) return false;
    float x, y, w, h; PipRect(cx, cy, &x, &y, &w, &h);   // player 2's window
    *px = 0.f; *py = 0.f; *pw = cx; *ph = cy;
    if (g_MonLayoutOn && g_MonRegF[0][2] > 0.01f && g_MonRegF[0][3] > 0.01f)
    {   // f1.321 (the author: "The darkening was before limited to each players window - why is that different now?"): over
        // all monitors / one wide screen player 1's part comes from the monitors layout - the halves rule below gave half
        // the window (P1 + half of P2's column in thirds)
        *px = g_MonRegF[0][0] * cx; *py = g_MonRegF[0][1] * cy; *pw = g_MonRegF[0][2] * cx; *ph = g_MonRegF[0][3] * cy;
        return true;
    }
    if (SplitThirds()) { *pw = cx / 3.f; }
    else if (SplitSide() >= 2) { *ph = cy * 0.5f; *py = y < cy * 0.25f ? cy * 0.5f : 0.f; }   // windows on the top / bottom half
    else { *pw = cx * 0.5f; *px = x < cx * 0.25f ? cx * 0.5f : 0.f; }                       // windows on the left / right half
    return true;
}
// player 1's part of the screen while player 2's window shows (called from PipDrawUnderHud, player 2 loaded)
static void DangerP1(void* canvas)
{
    g_SplitShowingNow = AnyWindowShowing();   // player 3's window alone counts too (pip_hud/30)
    if (!g_Cfg.dangerPerScreen || !g_SplitShowingNow) { DangerLog(1, false); return; }
    void* p1 = LeaderChar();
    bool on = CharInDanger(p1);
    DangerLog(1, on);
    if (!on) return;
    float px, py, pw, ph;
    if (P1PartRect(canvas, &px, &py, &pw, &ph)) DangerEdge(canvas, px, py, pw, ph);
}

// ---- f1.208: each player's own view darkens while THEIR command menu is open (split screen only) ----
// (the author: "Could we have a setting for each player that lets us turn that on or off? ... Only their own screen should
// darken if that box is checked." / "a separate filter for the co-op camera"). The game's darkening is a post-process
// particle effect over the whole picture (FX_R_WaitFilter_01, object dump) - it can't be limited to one view, so the
// views get a translucent black tile instead (drawn under the HUD: menus / HUD stay bright, like the game's), fading in /
// out over MENU_DIM_FADE_S. MENU_DIM_ALPHA = how dark (a guess at the game's look - tune after the author's test).
static constexpr float MENU_DIM_ALPHA = 0.40f;
static constexpr float MENU_DIM_FADE_S = 0.15f;
// true while player p's command menu is open (p 1 = player 1's = the game's own command system, 2 / 3 = their own systems)
static bool MenuOpenFor(int p)
{
    unsigned char* b = BcmOf(p);
    return b && Readable(b + BCM_UI_STATE, 4) && *(int*)(b + BCM_UI_STATE) >= BCM_UI_MENU_OPEN;
}
// player p's darkening, 0 .. 1, moving toward on / off by the time since the last call (at most 0.1 s a step; after a
// pause of over 0.25 s it starts again from 0)
static float MenuDimLevel(int p, bool on)
{
    static float level[4] = {0.f, 0.f, 0.f, 0.f};
    static ULONGLONG last[4] = {0, 0, 0, 0};
    p &= 3;
    ULONGLONG now = GetTickCount64();
    ULONGLONG ms = last[p] ? now - last[p] : 0;
    if (ms > 250) level[p] = 0.f;   // not drawn for a while (the window was hidden): start from normal, no left-over dark frame
    if (ms > 100) ms = 100;
    last[p] = now;
    float step = (float)ms / 1000.f / MENU_DIM_FADE_S;
    level[p] += on ? step : -step;
    if (level[p] < 0.f) level[p] = 0.f;
    if (level[p] > 1.f) level[p] = 1.f;
    return level[p];
}
static void MenuDimLog(int p, bool on)
{
    static bool was[4] = {false, false, false, false};
    static LogRate rate;   // f1.211: 40 a minute (was 200 a session - used up 9 minutes into the Airbuster fight)
    if (on == was[p & 3]) return;
    was[p & 3] = on;
    if (LogOk(rate, 40)) { CoopLog("MENUDIM: player %d's view %s", p, on ? "darkened - their command menu is open (MenuDarkenSplitPlayerN)" : "back to normal"); }
}
// darken player p's view (x, y, w, h) while their menu is open and their box is ticked
static void MenuDimView(void* canvas, int p, float x, float y, float w, float h)
{
    bool on = p >= 1 && p <= 3 && g_Cfg.menuDarkSplit[p] && MenuOpenFor(p);
    MenuDimLog(p, on);
    float a = MenuDimLevel(p, on) * MENU_DIM_ALPHA;
    if (a < 0.005f || w < 1.f || h < 1.f || !g_CF.white || !SmResolveTex(canvas)) return;
    float col[4] = {0.f, 0.f, 0.f, a};
    SmTile(canvas, g_CF.white, x, y, w, h, col, 2, 0.f);   // BLEND_Translucent
}
// player 1's part of the screen (called from PipDrawUnderHud before DangerP1, so the red edge stays on top)
static void MenuDimP1(void* canvas)
{
    float px, py, pw, ph;
    if (!AnyWindowShowing() || !P1PartRect(canvas, &px, &py, &pw, &ph)) { MenuDimLog(1, false); MenuDimLevel(1, false); return; }
    MenuDimView(canvas, 1, px, py, pw, ph);
}
static void InstallMenuDimHook()
{
    static const unsigned char ex[16] = {0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57,0x41,0x57,0x49,0x8D,0xAB,0x08,0xFF,0xFF,0xFF};
    void* t = MakeInlineHook(g_Base + 0x14BF920, (void*)Hook_FilterMake, 16, ex, "screen filters (MenuDarken)");
    if (t) Orig_FilterMake = (FilterMake_t)t;
}
