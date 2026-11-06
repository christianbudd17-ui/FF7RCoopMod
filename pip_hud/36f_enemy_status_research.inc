// The game's own enemy health / stagger bars (EnemyStatus_C) for players 2/3 (f1.174 research -> f1.175; the
// research dumps and placing logs removed - the tidy-up the author approved).
// The author (2026-10-25, after f1.173's numbers passed): "Can we get the enemy health/stagger/pressure/names to also use
// the right assets?" Exe RE (claude/FF7R_Game_Original_HUD_Plan.md):
//  - The enemy status manager: a 0x118-byte object (vtable 0x4BF1260, an FGCObject - its AddReferencedObjects 0x13DAF40
//    keeps the TArray at +0xF8 / +0x100 alive), made in 0x13B1A70 and kept at the menu system +0xD8 (static 0x59CAA48).
//    Its tick 0x13DEEF0(manager, dt) is called by the menu system tick (0x13AE391).
//  - +0x08 a TMap of entries (0x150 bytes: +0x00 TWeakPtr<the enemy's battle info> (obj, controller), +0x10 weak
//    EnemyStatus_C widget, ... +0x13D / +0x13F flags, +0x140 a timer); 0x13DCCB0 (in the tick) adds an entry + widget for
//    every enemy in the battle list (0x9FF910 / 0xA00350); the tick then shows / updates / places each one.
//  - +0xD8 / +0xE0 TWeakPtr = the current target (0x13DA690 resolves it: obj vfunc +0x68 valid, +0x20 = the character);
//    +0xE8 / +0xF0 a second one. +0x58 / +0x60 must be set for the sync to run.
//  - Placing: 0x13DBBC0(manager, widget, FVector* world) (normal), 0x13DC010(manager, widget, info, FVector*, ...) (one
//    path in the tick, 0x13DF7ED).
typedef void (*EsTick_t)(void* mgr, float dt);
static EsTick_t Orig_EsTick = nullptr;
typedef uint64_t (*EsPos_t)(void* mgr, void* widget, float* world, uint64_t a4);
static EsPos_t Orig_EsPos = nullptr;
typedef uint64_t (*EsPosT_t)(void* mgr, void* widget, void* info, float* world, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8);
static EsPosT_t Orig_EsPosT = nullptr;
static int g_EsVisOff = -2;   // UWidget::Visibility's offset (reflection)

static void EsNameOf(void* o, char* out, size_t n) { if (!o) { snprintf(out, n, "-"); return; } NameOf(o, out, n); }
// ---- f1.175: players 2/3 get the GAME'S OWN enemy bar ([General] GameEnemyBars=1, default) ----
// f1.174's log (23:05 - 23:07): the manager makes one EnemyStatus_C per enemy of the battle (EnemyStatus_C_10 ... _15) and
// shows exactly one - its +0xD8 target's (Visibility 0 = Visible, the others 1 = Collapsed); the target's bar is placed by
// 0x13DC010, the hidden ones by 0x13DBBC0; +0x58 = the EnemyStatus_C class, +0x60 = on (1 in a battle, 0 after it, the
// entries gone); +0xF8 = a pool of spare widgets (it grew as enemies died). The tick compares each entry's key (its
// TWeakPtr<battle info>) with +0xD8's object directly - the info object = character +0xCB8 (controller +0xCC0), the same
// thing a battle unit's target field points at (menus/35b TargetCharOf).
// So players 2 and 3 each get a SECOND manager of the same kind (made like the game makes its own - 0x13B25CB: the game's
// allocator, the FGCObject constructor so the garbage collector keeps its widgets, the empty containers, its vtable).
// Every frame, after the game's own tick: the switches (+0x58 .. +0x87) are copied from the game's manager, +0xD8 is set
// to that player's target, and the game's tick runs on it - the game builds, fills (HP, stagger, pressure, name, status
// icons) and animates the bars itself. Its placing calls are redirected into the player's window: the world point ->
// PipProject in their window -> DeprojectScreenToWorld on player 1's camera (at the same distance as from their window
// camera) -> the game places the bar there. Not in their window = hidden. Player 1's own bar over a window = hidden.
// GameEnemyBars=0 = the mod's drawn bar (marker/19) as before; the second managers' bars are hidden.
static unsigned char* g_EsClone[4] = {};
static int g_EsCur = 0;               // 1 = the game's manager is ticking, 2 / 3 = that player's
static void* g_EsHide[64]; static int g_EsHideN = 0;   // widgets to hide after this tick
static bool g_EsPlaced = false;       // a bar of the ticking player's was placed in their window
static int g_EsPosTN = 0, g_EsMapFail = 0;   // target-bar placing calls this tick / of them not in the window
static void* g_EsTW[4] = {};                  // the widget the game placed as that player's target bar (last tick)
static bool g_EsShared = false;       // the ticking player's bars go on the shared screen (split screen off)
static float g_EsLastRect[4][4] = {};  // the screen area last given to that player's target bar (slate units)
static float g_EsWinBot[4] = {};       // that player's window bottom for their target bar (slate units), 0 = none
// the target bar's position (+0x1C0 / +0x1C4, written by 0x13DC010 - also kept in its entry at +0xC4) vs what shows:
// the f1.189 SEE lines + the author's screenshot put the bar's name + gauges at about y + 10 .. y + 75 (slate units)
static const float kEsBarBelow = 85.f;   // so y <= window bottom - 85 keeps the bar 10 above the window's bottom
static void EsClampBottom(unsigned char* mgr, void* widget, int pn)
{
    if (pn < 2 || pn > 3 || !mgr || !widget || g_EsWinBot[pn] <= 0.f || !Readable((unsigned char*)widget + 0x1C0, 8)) return;
    float* pos = (float*)((unsigned char*)widget + 0x1C0);
    float yMax = g_EsWinBot[pn] - kEsBarBelow;
    if (!(pos[1] > yMax)) return;
    float was = pos[1];
    pos[1] = yMax;
    // the entry's copy (0x13DC2C2: entry + 0x10 + 0xB4) - the same value, kept in step
    unsigned char* data = *(unsigned char**)(mgr + 0x08); int num = *(int*)(mgr + 0x10);
    unsigned* bits = *(unsigned**)(mgr + 0x28); if (!bits) bits = (unsigned*)(mgr + 0x18);
    for (int i = 0; data && i < num && i < 64; ++i)
    {
        if (!Readable(bits + (i >> 5), 4) || !(bits[i >> 5] >> (i & 31) & 1)) continue;
        unsigned char* e = data + (size_t)i * 0x150; if (!Readable(e, 0xCC)) continue;
        if (ResolveWeak(*(int*)(e + 0x10), *(int*)(e + 0x14)) != widget) continue;
        float* ep = (float*)(e + 0xC4); if (ep[1] > yMax) ep[1] = yMax;
        break;
    }
    static LogRate logs;
    if (LogOk(logs, 6)) CoopLog("GAMEBARS: player %d's enemy bar kept above their window's bottom (y %.0f -> %.0f)", pn, was, yMax);
}
static unsigned char* EsMakeClone()
{
    typedef void* (*Malloc_t)(size_t, uint32_t);
    typedef void (*BitInit_t)(void*, uint64_t, uint64_t);
    unsigned char* c = (unsigned char*)((Malloc_t)(g_Base + 0x1C32C90))(0x118, 0);
    if (!c) return nullptr;
    memset(c, 0, 0x118);
    ((void (*)(void*))(g_Base + 0xD82980))(c);            // FGCObject base: registers it with the garbage collector
    *(void**)c = (void*)(g_Base + 0x4BF1260);             // the enemy status manager's vtable (as 0x13B25F9)
    ((BitInit_t)(g_Base + 0x63F170))(c + 0x18, 0, 0);     // the entries' allocation bits (as 0x13B260C)
    *(int*)(c + 0x38) = -1; *(int*)(c + 0x3C) = 0;
    *(int*)(c + 0x78) = -1; *(int*)(c + 0x80) = -1;
    ((BitInit_t)(g_Base + 0x63F170))(c + 0x98, 0, 0);     // the second set's bits (as 0x13B266E)
    *(int*)(c + 0xB8) = -1; *(int*)(c + 0xBC) = 0;
    return c;
}
static void* EsPlayerTarget(int pn)
{
    if (pn == 3) { WithP3 b; return TargetCharOf(P2Char(), 3); }
    WithP2 b; return TargetCharOf(P2Char(), 2);
}
static void EsSetTarget(unsigned char* c, void* tg)
{
    unsigned char* t = (unsigned char*)tg;
    void* obj = (t && Readable(t + 0xCB8, 16)) ? *(void**)(t + 0xCB8) : nullptr;
    unsigned char* ctl = obj ? *(unsigned char**)(t + 0xCC0) : nullptr;
    if (!ctl || !Readable(ctl, 16)) { obj = nullptr; ctl = nullptr; }
    if (*(void**)(c + 0xD8) == obj && *(void**)(c + 0xE0) == ctl) return;
    if (ctl) InterlockedIncrement((volatile LONG*)(ctl + 0xC));   // our weak reference (never given back: a tiny leak per change)
    *(void**)(c + 0xD8) = obj; *(void**)(c + 0xE0) = ctl;
}
static void EsSetVis(void* w, unsigned char v)
{
    if (!w || !Readable(w, 8)) return;
    void** vt = *(void***)w; if (!vt || !Readable(vt + 0x248 / 8, 8)) return;
    ((void (*)(void*, unsigned char))vt[0x248 / 8])(w, v);
}
// hide a bar the way the game does (Collapsed), only if it is showing now (UWidget Visibility: 0 Visible, 1 Collapsed,
// 2 Hidden, 3 / 4 hit-test-invisible = shown)
static void EsCollapse(void* w)
{
    if (!w || !Readable(w, 0x20)) return;
    if (g_EsVisOff == -2) { g_EsVisOff = PropOff(*(void**)((unsigned char*)w + 0x10), "Visibility"); CoopLog("GAMEBARS: UWidget Visibility at +0x%X", g_EsVisOff); }
    if (g_EsVisOff > 0 && Readable((unsigned char*)w + g_EsVisOff, 1))
    {
        unsigned char v = ((unsigned char*)w)[g_EsVisOff];
        if (v == 1 || v == 2) return;
    }
    EsSetVis(w, 1);
}
static void EsHideAll(unsigned char* c)
{
    unsigned char* data = *(unsigned char**)(c + 0x08); int num = *(int*)(c + 0x10);
    unsigned* bits = *(unsigned**)(c + 0x28); if (!bits) bits = (unsigned*)(c + 0x18);
    for (int i = 0; data && i < num && i < 64; ++i)
    {
        if (!Readable(bits + (i >> 5), 4) || !(bits[i >> 5] >> (i & 31) & 1)) continue;
        unsigned char* e = data + (size_t)i * 0x150; if (!Readable(e, 0x18)) continue;
        EsCollapse(ResolveWeak(*(int*)(e + 0x10), *(int*)(e + 0x14)));
    }
}
static void EsHidePending() { for (int i = 0; i < g_EsHideN; ++i) EsCollapse(g_EsHide[i]); g_EsHideN = 0; }
static void EsHideLater(void* w) { if (w && g_EsHideN < 64) g_EsHide[g_EsHideN++] = w; }
// that player's window shows the world point: the point on player 1's camera ray that lands on the same screen spot
static bool EsMapWorld(int pn, const float* world, float out[3])
{
    float S[2], o[3], d[3];
    if (!PopWinPoint(pn, false, world, S)) return false;
    if (!g_P1Pc || !PopDeproject(g_P1Pc, S, o, d)) return false;
    float cam[3];
    {
        if (pn == 3) { WithP3 b; memcpy(cam, g_Player->pipCamPos, 12); }
        else
        { WithP2 b; memcpy(cam, g_Player->pipCamPos, 12); }
    }
    float dx = world[0] - cam[0], dy = world[1] - cam[1], dz = world[2] - cam[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz); if (dist < 50.f) dist = 50.f;   // as far from player 1's camera as the enemy is from theirs
    out[0] = o[0] + d[0] * dist; out[1] = o[1] + d[1] * dist; out[2] = o[2] + d[2] * dist;
    return true;
}
// player 1's bar: where the game shows it (its own projection); over a window = hidden
static void EsCheckP1(void* widget, const float* world)
{
    if (!g_P1Pc || !widget) return;
    float sc[2] = {0, 0};
    bool ok = ((bool (*)(void*, const float*, float*))(g_Base + 0x1348940))(g_P1Pc, world, sc);
    if (!ok) return;
    float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f;
    bool inP1;
    switch (SplitSide())
    {
        case 0: inP1 = sc[0] >= W * 0.5f; break;
        case 1: inP1 = sc[0] <= W * SplitP1Frac(); break;
        case 2: inP1 = sc[1] >= H * 0.5f; break;
        default: inP1 = sc[1] <= H * 0.5f; break;
    }
    if (!inP1)
    {
        EsHideLater(widget);
        static LogRate logs; if (LogOk(logs, 4)) { CoopLog("GAMEBARS: player 1's enemy bar over a window (%.0f, %.0f) - hidden there", sc[0], sc[1]); }
    }
}
// (the author's screenshot: "P3's stuff encroaching onto P2's"): the game keeps the target bar on screen itself -
// 0x13DC010 asks 0x13488A0 for the screen's 16:9 area (slate units: [left, top, right, bottom]) and clamps the bar's
// position to it (0x13DC1D1 .. 0x13DC294: x + 0.25 * 500 >= left, x + 0.75 * 500 <= right, y - 0.5 * 200 >= top, and
// if y + 2 * 200 > bottom then y = bottom - 0.5 * 200 (a jump - f1.190); 500 / 200 = the globals 0x53AB1D0 / 0x53AB1D4). For players 2/3's bars (and player 1's while
// windows show) that ONE call answers with the player's own part of the screen, so the game's clamp keeps the bar inside
// it (except the bottom for players 2/3 - see Hook_EsRect). Other callers of 0x13488A0 (14 in the exe) get the
// game's answer.
typedef float* (*EsRect_t)(float* out);
static EsRect_t Orig_EsRect = nullptr;
static bool EsRegion(int pn, float W, float H, float r[4])   // canvas pixels: left, top, right, bottom
{
    if (pn >= 2)
    {
        void* ch = nullptr; if (!PopWinShowing(pn, &ch)) return false;
        float x, y, w, h;
        if (pn == 3) { WithP3 b; PipRect(W, H, &x, &y, &w, &h); }
        else
        { WithP2 b; PipRect(W, H, &x, &y, &w, &h); }
        r[0] = x; r[1] = y; r[2] = x + w; r[3] = y + h; return w > 50.f && h > 50.f;
    }
    if (!AnyWindowShowing()) return false;
    if (g_MonLayoutOn)
    {   // f1.311: the monitors / one-wide-screen layout - player 1's part straight from the regions (player 1 can be in the middle)
        r[0] = g_MonRegF[0][0] * W; r[1] = g_MonRegF[0][1] * H; r[2] = r[0] + g_MonRegF[0][2] * W; r[3] = r[1] + g_MonRegF[0][3] * H;
        return r[2] - r[0] > 50.f && r[3] - r[1] > 50.f;
    }
    r[0] = 0; r[1] = 0; r[2] = W; r[3] = H;   // player 1's part (as EsCheckP1)
    if (SplitThirds()) { r[2] = W / 3.f; return true; }
    switch (SplitSide())
    {
        case 0: r[0] = W * 0.5f; break;
        case 1: r[2] = W * SplitP1Frac(); break;
        case 2: r[1] = H * 0.5f; break;
        default: r[3] = H * 0.5f; break;
    }
    return true;
}
static float* Hook_EsRect(float* out)
{
    void* ra = __builtin_return_address(0);
    float* res = Orig_EsRect(out);
    if (g_HudSpanOk && out && g_HudSpanFit > 0.01f && GetCurrentThreadId() == g_GameThreadId)
    {   // f1.311: the HUD box is player 1's part, maybe wider / narrower than 1920 units (one wide screen, thirds) - every
        // caller's "screen area" (margins kept) is that box (14 callers: enemy bars, off-screen target arrows, popups ...)
        float bw = g_HudSpanW / g_HudSpanFit, bh = g_HudSpanH / g_HudSpanFit;
        if (bw > 100.f && bh > 100.f && out[0] >= 0.f && out[1] >= 0.f && out[0] * 2.f < bw && out[1] * 2.f < bh) { out[2] = bw - out[0]; out[3] = bh - out[1]; }
    }
    if (ra != (void*)(g_Base + 0x13DC1D6) || g_EsCur < 1 || !g_Player->joined || !out || GetCurrentThreadId() != g_GameThreadId) return res;
    float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f;
    float pr[4]; if (!EsRegion(g_EsCur, W, H, pr)) return res;
    float Ws = out[0] + out[2], Hs = out[1] + out[3], refH = out[3] - out[1];   // game: [m, n, Ws - m, Hs - n]
    if (Ws < 100.f || Hs < 100.f || refH < 100.f) return res;
    if (g_HudSpanOk && g_GameHwnd && g_HudSpanFit > 0.01f)
    {   // f1.304: over all monitors the HUD's design units map to window pixels as (px - HUD top left) / fit (31b) - the
        // part's edges go through that (player 2's part lies right of the 1920 units = their monitor), no clamp to the
        // game's 16:9 area (that is player 1's monitor only)
        RECT cr{}; GetClientRect(g_GameHwnd, &cr);
        float cx = cr.right > 0 ? (float)cr.right / W : 1.f, cy = cr.bottom > 0 ? (float)cr.bottom / H : 1.f;   // canvas -> window px
        float L = (pr[0] * cx - g_HudSpanX) / g_HudSpanFit, T = (pr[1] * cy - g_HudSpanY) / g_HudSpanFit;
        float R = (pr[2] * cx - g_HudSpanX) / g_HudSpanFit, B = (pr[3] * cy - g_HudSpanY) / g_HudSpanFit;
        float B2 = B;
        if (g_EsCur >= 2) { g_EsWinBot[g_EsCur] = B; B2 = B + 4000.f; }
        if (R - L < 260.f) return res;
        out[0] = L; out[1] = T; out[2] = R; out[3] = B2;
        if (g_EsCur >= 1 && g_EsCur <= 3) { g_EsLastRect[g_EsCur][0] = L; g_EsLastRect[g_EsCur][1] = T; g_EsLastRect[g_EsCur][2] = R; g_EsLastRect[g_EsCur][3] = B2; }
        static LogRate logsM;
        if (LogOk(logsM, 4)) { CoopLog("GAMEBARS: player %d's enemy bar kept inside their part (all monitors: %.0f %.0f %.0f %.0f, slate units)", g_EsCur, L, T, R, B2); }
        return res;
    }
    float sx = Ws / W, sy = Hs / H;
    float L = pr[0] * sx, T = pr[1] * sy, R = pr[2] * sx, B = pr[3] * sy;
    if (L < out[0]) L = out[0];
    if (T < out[1]) T = out[1];
    if (R > out[2]) R = out[2];
    if (B > out[3]) B = out[3];
    // (the author, f1.189 test: P2's bar "shows up, but ONLY if P2 gets far away - so it's there, but it's not in the
    // right spot"; the f1.189 GAMEBARS SEE lines: P2's bar at y 580 = the top of P3's window): the game's bottom rule is
    // NOT "keep y + 400 <= bottom" - when y + 400 > bottom it puts the bar at bottom - 100 (0x13DC28C - 0x13DC294), a JUMP
    // down. f1.179 had made players 2/3's bottom 200 slate units LOWER than their window (bottom room for the menus), so
    // every bar of an enemy in the lower part of P2's window jumped to y 580 - under P2's window, on top of P3's (the
    // "ghost" P3 saw in f1.185). Now: players 2/3 get no bottom from the game (out of reach) and Hook_EsPosT keeps the
    // bar's content above their window's bottom itself (EsClampBottom - smooth, no jump); player 1's part keeps the
    // game's rule with their part's real bottom (the jump stays inside their part).
    float B2 = B;
    if (g_EsCur >= 2) { g_EsWinBot[g_EsCur] = B; B2 = out[3] + 4000.f; }
    if (R - L < 260.f) return res;   // too narrow for the game's rule (x + 125 .. x + 375) - leave it
    out[0] = L; out[1] = T; out[2] = R; out[3] = B2;
    if (g_EsCur >= 1 && g_EsCur <= 3) { g_EsLastRect[g_EsCur][0] = L; g_EsLastRect[g_EsCur][1] = T; g_EsLastRect[g_EsCur][2] = R; g_EsLastRect[g_EsCur][3] = B2; }
    static LogRate logs;
    if (LogOk(logs, 4)) { CoopLog("GAMEBARS: player %d's enemy bar kept inside their part of the screen (%.0f %.0f %.0f %.0f, slate units)", g_EsCur, L, T, R, B2); }
    return res;
}
// (the author, f1.188 test: "almost immediately at start of battle the health/pressure/enemy name were not visible on
// P2's screen" - while the f1.188 log said P2's bar was placed in their window, Visibility 0): what the bar widget looks like
// on screen. Log only (detailed log): its real screen rectangle (cached geometry), its color alpha (UUserWidget
// ColorAndOpacity +0x100), its root's Visibility, attached to the screen or not, and whether player 1's manager owns the
// same widget object.
static int TiAttached(void* icon);   // pip_hud/36g
static bool EsOwnsWidget(unsigned char* c, void* w)
{
    if (!c || !w) return false;
    unsigned char* data = *(unsigned char**)(c + 0x08); int num = *(int*)(c + 0x10);
    unsigned* bits = *(unsigned**)(c + 0x28); if (!bits) bits = (unsigned*)(c + 0x18);
    for (int i = 0; data && i < num && i < 64; ++i)
    {
        if (!Readable(bits + (i >> 5), 4) || !(bits[i >> 5] >> (i & 31) & 1)) continue;
        unsigned char* e = data + (size_t)i * 0x150; if (!Readable(e, 0x18)) continue;
        if (ResolveWeak(*(int*)(e + 0x10), *(int*)(e + 0x14)) == w) return true;
    }
    return false;
}
static int EsRootVis(void* w)   // the bar's root widget's Visibility (-1 = unknown)
{
    static int wtOff = -2, rootOff = -2;
    if (!w || !Readable(w, 0x20) || g_EsVisOff <= 0) return -1;
    if (wtOff == -2) wtOff = PropOff(*(void**)((unsigned char*)w + 0x10), "WidgetTree");
    if (wtOff <= 0 || !Readable((unsigned char*)w + wtOff, 8)) return -1;
    unsigned char* wt = *(unsigned char**)((unsigned char*)w + wtOff); if (!wt || !Readable(wt, 0x20)) return -1;
    if (rootOff == -2) rootOff = PropOff(*(void**)(wt + 0x10), "RootWidget");
    if (rootOff <= 0 || !Readable(wt + rootOff, 8)) return -1;
    unsigned char* r = *(unsigned char**)(wt + rootOff); if (!r || !Readable(r + g_EsVisOff, 1)) return -1;
    return r[g_EsVisOff];
}
static void Hook_EsTick(void* mgr, float dt)
{
    bool gt = GetCurrentThreadId() == g_GameThreadId;
    if (!gt) { Orig_EsTick(mgr, dt); return; }
    unsigned char* real = (unsigned char*)mgr;
    g_EsCur = 1; g_EsHideN = 0;
    Orig_EsTick(mgr, dt);
    g_EsCur = 0;
    if (AnyWindowShowing()) EsHidePending(); else g_EsHideN = 0;
    // players 2/3's managers
    bool realOn = real[0x60] && *(void**)(real + 0x58);
    // (the author: "Yes, add that, hide extras on same target"): split screen off (no window showing) = one shared
    // screen; players 2/3's bars show there too, on their targets, placed by the game as player 1's (no mapping). A target
    // that already has a bar on the shared screen (player 1's, or player 2's for player 3) gets no second one.
    bool shared = !AnyWindowShowing();
    void* shown[2] = {*(void**)(real + 0xD8), nullptr};   // battle-info objects whose bar is on the shared screen
    for (int pn = 2; pn <= 3; ++pn)
    {
        unsigned char*& c = g_EsClone[pn];
        if (!g_Player->joined) { if (c) EsHideAll(c); continue; }
        bool active;
        if (pn == 2) { WithP2 b; active = g_Player->joined; }
        else
        {
            WithP3 b; active = g_P3Enabled && g_Player->joined;
        }
        void* ch = nullptr; bool win = PopWinShowing(pn, &ch);
        bool show = (win || (shared && active && g_Cfg.showMarker)) && realOn;
        bool dup = false;
        if (show && shared)
        {
            unsigned char* t = (unsigned char*)EsPlayerTarget(pn);
            void* obj = (t && Readable(t + 0xCB8, 8)) ? *(void**)(t + 0xCB8) : nullptr;
            dup = obj && (obj == shown[0] || obj == shown[1]);
            if (obj && !dup) shown[1] = obj;
            static bool lastDup[4] = {}; static LogRate logs;
            if (dup != lastDup[pn] && LogOk(logs, 12)) { lastDup[pn] = dup; CoopLog("GAMEBARS: shared screen - player %d's target %s", pn, dup ? "already has a bar (no second one)" : "gets its own bar"); }
        }
        if (!c)
        {
            if (!show) continue;
            c = EsMakeClone();
            CoopLog("GAMEBARS: player %d's enemy status manager %s (%p)", pn, c ? "made - their target's bar is the game's own" : "could NOT be made", c);
            if (!c) continue;
        }
        memcpy(c + 0x58, real + 0x58, 0x30);   // the switches: +0x58 widget class, +0x60 on, +0x64 .. +0x87
        if (!show) c[0x60] = 0;
        void* tg = show && !dup ? EsPlayerTarget(pn) : nullptr;
        EsSetTarget(c, tg);
        g_EsWinBot[pn] = 0.f;   // set again by Hook_EsRect if their target bar is placed this tick
        g_EsCur = pn; g_EsHideN = 0; g_EsPlaced = false; g_EsShared = show && !win; g_EsPosTN = 0; g_EsMapFail = 0;
        Orig_EsTick(c, dt);
        g_EsCur = 0; g_EsShared = false;
        if (!show || dup) EsHideAll(c); else EsHidePending();
        if (g_DetailedLog && tg)
        {   // (the author, f1.181 test: "Health bar was not consistent for P2, it sometimes didn't show up"): one line
            // per player whenever their target bar's state changes (at most every 1.5 s): placed by the game this tick, in
            // their window, its widget's Visibility after our hiding
            // the entry is found by its key (the enemy's target object, character +0xCB8) - f1.183 used EsWeakChar,
            // which never matches these objects (visibility was always -1)
            int vis = -1; void* w = nullptr;
            void* key0 = Readable((unsigned char*)tg + 0xCB8, 8) ? *(void**)((unsigned char*)tg + 0xCB8) : nullptr;
            unsigned char* data = *(unsigned char**)(c + 0x08); int num = *(int*)(c + 0x10);
            unsigned* bits = *(unsigned**)(c + 0x28); if (!bits) bits = (unsigned*)(c + 0x18);
            for (int i = 0; key0 && data && i < num && i < 64; ++i)
            {
                if (!Readable(bits + (i >> 5), 4) || !(bits[i >> 5] >> (i & 31) & 1)) continue;
                unsigned char* e = data + (size_t)i * 0x150; if (!Readable(e, 0x18) || *(void**)e != key0) continue;
                w = ResolveWeak(*(int*)(e + 0x10), *(int*)(e + 0x14));
                if (w && g_EsVisOff > 0 && Readable((unsigned char*)w + g_EsVisOff, 1)) vis = ((unsigned char*)w)[g_EsVisOff];
                break;
            }
            static int opOff = -2; if (opOff == -2 && w) opOff = PropOff(*(void**)((unsigned char*)w + 0x10), "RenderOpacity");
            float op = (w && opOff > 0 && Readable((unsigned char*)w + opOff, 4)) ? *(float*)((unsigned char*)w + opOff) : -1.f;
            float px = (w && Readable((unsigned char*)w + 0x1C0, 8)) ? ((float*)((unsigned char*)w + 0x1C0))[0] : -1.f;
            float py = (w && Readable((unsigned char*)w + 0x1C0, 8)) ? ((float*)((unsigned char*)w + 0x1C0))[1] : -1.f;
            void* other = g_EsTW[pn == 2 ? 3 : 2];
            int key = (g_EsPosTN > 0 ? 1 : 0) | (g_EsMapFail > 0 ? 2 : 0) | (g_EsPlaced ? 4 : 0) | ((vis + 1) << 3) | (win ? 128 : 0)
                    | ((op > 0.5f) ? 256 : 0) | ((w && w == other) ? 512 : 0) | ((w && w != g_EsTW[pn]) ? 1024 : 0);
            static int lastKey[4] = {-1, -1, -1, -1}; static void* lastT2[4] = {}; static ULONGLONG lastAt[4] = {};
            ULONGLONG nowT = GetTickCount64();
            static LogRate seeLogs;
            bool changed = (key != lastKey[pn] || tg != lastT2[pn]) && nowT - lastAt[pn] >= 1500;
            bool every = nowT - lastAt[pn] >= 4000;   // also every 4 s while it stays the same (max 40 per minute)
            if (changed || (every && LogOk(seeLogs, 40)))
            {
                lastKey[pn] = key; lastT2[pn] = tg; lastAt[pn] = nowT; char tn[96]; EsNameOf(tg, tn, sizeof(tn));
                CoopLog("GAMEBARS STATE: player %d on %s - %s, placing call %s%s, bar widget %p visibility %d (0 / 3 / 4 = shown), opacity %.2f, at %.0f, %.0f%s%s", pn, tn,
                        win ? "their window" : "shared screen", g_EsPosTN ? "yes" : "NO (the game didn't place a target bar)",
                        g_EsMapFail ? " - point NOT in their window (hidden)" : "", w, vis, op, px, py,
                        (w && w == other) ? " - SAME widget as the other player's bar" : "",
                        (w && g_EsTW[pn] && w != g_EsTW[pn]) ? " - the game placed a DIFFERENT widget" : "");
                if (w)
                {   // what it looks like on screen
                    float rx = -1, ry = -1, rw = -1, rh = -1, rs = 0;
                    bool rOk = g_Hc.state == 1 && HcRect(w, &rx, &ry, &rw, &rh, &rs);
                    float ca = Readable((unsigned char*)w + 0x100, 16) ? ((float*)((unsigned char*)w + 0x100))[3] : -1.f;
                    int att = TiAttached(w), rv = EsRootVis(w);
                    bool p1 = EsOwnsWidget(real, w);
                    float wx, wy, ww, wh; bool wOk = false;
                    {
                        if (pn == 3) { WithP3 b; float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f; PipRect(W, H, &wx, &wy, &ww, &wh); wOk = true; }
                        else
                        { WithP2 b; float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f; PipRect(W, H, &wx, &wy, &ww, &wh); wOk = true; }
                    }
                    bool inWin = rOk && wOk && win && rx + rw > wx && rx < wx + ww && ry + rh > wy && ry < wy + wh;
                    const float* lr = g_EsLastRect[pn];
                    CoopLog("GAMEBARS SEE: player %d's bar on screen %s (%.0f, %.0f, %.0f x %.0f, scale %.2f)%s, color alpha %.2f, root visibility %d, attached %s%s | window %.0f, %.0f, %.0f x %.0f | area given %.0f %.0f %.0f %.0f",
                            pn, rOk ? "at" : "- no geometry", rx, ry, rw, rh, rs, !win ? "" : inWin ? " - inside their window" : rOk ? " - NOT inside their window" : "",
                            ca, rv, att < 0 ? "?" : att ? "yes" : "NO", p1 ? " - ALSO one of player 1's bars" : "",
                            wx, wy, ww, wh, lr[0], lr[1], lr[2], lr[3]);
                }
            }
        }
        static void* lastTg[4] = {}; static LogRate logs;
        if (tg != lastTg[pn] && LogOk(logs, 12))
        {
            lastTg[pn] = tg; char tn[96]; EsNameOf(tg, tn, sizeof(tn));
            CoopLog("GAMEBARS: player %d's target %s - %d bar(s) in their manager, the game's bar %s", pn, tn,
                    *(int*)(c + 0x10) - *(int*)(c + 0x3C), g_EsPlaced ? "placed in their window" : "not placed (yet / off their window)");
        }
    }
}
static uint64_t Hook_EsPos(void* mgr, void* widget, float* world, uint64_t a4)
{
    if (g_EsCur >= 2 && g_EsShared) return Orig_EsPos(mgr, widget, world, a4);   // shared screen = as player 1's
    if (g_EsCur >= 2 && world && GetCurrentThreadId() == g_GameThreadId)
    {   // one of players 2/3's bars (hidden ones are placed here too)
        float w2[3];
        if (EsMapWorld(g_EsCur, world, w2)) return Orig_EsPos(mgr, widget, w2, a4);
        uint64_t r = Orig_EsPos(mgr, widget, world, a4); EsHideLater(widget); return r;
    }
    return Orig_EsPos(mgr, widget, world, a4);
}
static uint64_t Hook_EsPosT(void* mgr, void* widget, void* info, float* world, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
    bool gt = GetCurrentThreadId() == g_GameThreadId;
    if (g_EsCur >= 2) ++g_EsPosTN;
    if (g_EsCur >= 2 && g_EsShared) { g_EsPlaced = true; g_EsTW[g_EsCur] = widget; return Orig_EsPosT(mgr, widget, info, world, a5, a6, a7, a8); }
    if (g_EsCur >= 2 && world && gt)
    {   // that player's TARGET bar
        float w2[3];
        bool mapped = EsMapWorld(g_EsCur, world, w2);
        if (!mapped) ++g_EsMapFail;
        if (mapped)
        {
            g_EsPlaced = true; g_EsTW[g_EsCur] = widget;
            uint64_t r = Orig_EsPosT(mgr, widget, info, w2, a5, a6, a7, a8);
            EsClampBottom((unsigned char*)mgr, widget, g_EsCur);
            return r;
        }
        uint64_t r = Orig_EsPosT(mgr, widget, info, world, a5, a6, a7, a8); EsHideLater(widget); return r;
    }
    uint64_t r = Orig_EsPosT(mgr, widget, info, world, a5, a6, a7, a8);
    if (g_EsCur == 1 && gt && world && AnyWindowShowing()) EsCheckP1(widget, world);   // player 1's target bar
    return r;
}
static void InstallEnemyStatusResearch()
{
    static const unsigned char exTick[19] = {0x40,0x55,0x41,0x56,0x48,0x8D,0xAC,0x24,0x48,0xFE,0xFF,0xFF,0x48,0x81,0xEC,0xB8,0x02,0x00,0x00};
    void* t = MakeInlineHook(g_Base + 0x13DEEF0, (void*)Hook_EsTick, 19, exTick, "enemy status tick (GameEnemyBars)");
    if (t) Orig_EsTick = (EsTick_t)t;
    static const unsigned char exPos[14] = {0x48,0x8B,0xC4,0x4C,0x89,0x40,0x18,0x55,0x53,0x56,0x41,0x54,0x41,0x56};
    void* t2 = MakeInlineHook(g_Base + 0x13DBBC0, (void*)Hook_EsPos, 14, exPos, "enemy status placing (GameEnemyBars)");
    if (t2) Orig_EsPos = (EsPos_t)t2;
    static const unsigned char exPosT[14] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56};
    void* t3 = MakeInlineHook(g_Base + 0x13DC010, (void*)Hook_EsPosT, 14, exPosT, "enemy status placing 2 (GameEnemyBars)");
    if (t3) Orig_EsPosT = (EsPosT_t)t3;
    static const unsigned char exRect[14] = {0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x0F,0x29,0x7C,0x24,0x20};
    void* t4 = MakeInlineHook(g_Base + 0x13488A0, (void*)Hook_EsRect, 14, exRect, "enemy bar screen area (GameEnemyBars)");
    if (t4) Orig_EsRect = (EsRect_t)t4;
    CoopLog("GAMEBARS: screen-area hook %s (f1.179: players' bars stay inside their own part of the screen)", t4 ? "installed" : "NOT installed");
    CoopLog("GAMEBARS: hooks %s / %s / %s (players 2/3 get the game's own enemy bar)",
            t ? "installed" : "NOT installed", t2 ? "installed" : "NOT installed", t3 ? "installed" : "NOT installed");
}
