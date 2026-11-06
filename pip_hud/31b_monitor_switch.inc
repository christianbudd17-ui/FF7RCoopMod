// =================================================================================
// f1.290 - MULTIPLE MONITORS (EXPERIMENTAL, [PictureInPicture] MultiMonitor=1, off by default; the app's Split screen >
// Experimental box). The author 2026-11-05: "This cannot be post release"; "two monitor support should be an option under
// the experimental settings for split screen"; "three monitor support should also be a thing" (untested - he has two);
// and on the f1.289 menus ("very zoomed in and hard to navigate"): "we will need a way for it to switch resolutions when in
// the main menu, co-op camera, and for cutscenes, and then switch back when set for split screen".
//  - ONE monitor (player 1's, a borderless 16:9 window at that monitor's size) = the normal game: title / main menu,
//    field with the co-op camera, cutscenes, talking scenes. The other monitors get a black cover window.
//  - ALL monitors (one borderless window over them) while the split-screen windows show (battle by default) and no
//    menu / cutscene / pause: player 1 = their monitor (MonitorPlayer1: Main / Left / Middle / Right), players 2 / 3 the
//    others left to right; two monitors + players 2 and 3 = they share the second (MonitorShare Columns / Rows).
//  - A switch = r.SetRes (the game's render size, pip_hud/30c SpanSetRes), then 0.2 s later the window moved / restyled.
//    A wanted change waits 0.4 s (no flicker for a moment's menu), at least 1 s between switches.
// Research log (HUDGEO lines) while spanning: the game's root widgets' places / sizes, to move player 1's HUD next.
// =================================================================================
static MonRect g_MonM[8]; static int g_MonN = 0;
static int g_EcpProj = -2, g_EcpFov = -2;   // f1.291 EndCanvasPanel ProjectionMode / FieldOfView offsets (below)
static int g_MonP1 = -1;                 // index of player 1's monitor in g_MonM
static RECT g_MonSpanR{}, g_MonP1R{};     // all monitors / player 1's monitor (desktop pixels)
static int g_MonMode = 0;                // 0 not started, 1 one monitor, 2 all monitors
static int g_MonWant = 0, g_MonPend = 0; // wanted mode / switch waiting for the window step
static ULONGLONG g_MonWantSince = 0, g_MonLastSwitch = 0, g_MonApplyAt = 0, g_MonNextCheck = 0, g_MonRefresh = 0, g_MonSeen = 0;
static int g_MonFixes = 0;
static HWND g_MonCover[4] = {}; static int g_MonCoverN = 0; static bool g_MonCoverShown = false;

static LRESULT CALLBACK MonCoverProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}
// f1.310 (the author: "If there were 3 screens ... Yeah, okay" - a monitor with no player on it goes black, not the
// edge of player 1's wide view): which monitors the covers go over - mode 1 / 3 = every monitor but player 1's; mode 2 =
// the monitors no player's part is on (three monitors, players 2 or 3 not in)
// f1.311 - ONE WIDE SCREEN (the author: "Can we use the two monitor setup I have quickly to test the vertical split setup
// for triple screens? ... If it isn't going to work right we should at least ATTEMPT to make it work right before launch"):
// the same machinery as the monitors layout, but the players' parts come from a split of the WHOLE picture (player 1 | 2,
// or thirds player 1 | 2 | 3) instead of one monitor each. On for one ultrawide monitor (wider than 2.2:1 - the game
// renders at the monitor's size via r.SetRes, the HUD goes into player 1's part) and, for testing it on several monitors,
// with [PictureInPicture] MultiMonitorAsOneScreen=1 (the spanned window = one wide screen, no black covers).
static bool MonOneScreen()
{
    if (!g_Cfg.monSpanTest) return false;   // f1.328 (code review #10): the wide-screen test box alone does nothing
    if (g_Cfg.monOneScreen) return true;
    if (g_MonN == 1 && g_MonM[0].h > 0 && (float)g_MonM[0].w / (float)g_MonM[0].h > 2.2f) return true;
    return false;
}
static bool MonCoverWanted(int i)
{
    if (MonOneScreen() && g_MonMode != 1 && g_MonPend != 1) return false;   // f1.311: one wide screen - nothing to black out (menus on one monitor still do)
    if (i == g_MonP1) return false;
    if (g_MonMode != 2) return true;
    RECT cr{}; POINT tl{0, 0}; if (!g_GameHwnd) return false;
    GetClientRect(g_GameHwnd, &cr); ClientToScreen(g_GameHwnd, &tl);
    const MonRect& m = g_MonM[i];
    for (int p = 1; p < 3; ++p)
    {
        if (g_MonRegF[p][2] <= 0.f) continue;
        float x = tl.x + (g_MonRegF[p][0] + g_MonRegF[p][2] * 0.5f) * cr.right, y = tl.y + (g_MonRegF[p][1] + g_MonRegF[p][3] * 0.5f) * cr.bottom;
        if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) return false;   // a player's part is on it
    }
    return true;
}
static void MonCoversShow(bool on)
{
    if (on)
    {
        static bool reg = false;
        if (!reg)
        {
            WNDCLASSW wc{}; wc.lpfnWndProc = MonCoverProc; wc.hInstance = GetModuleHandleW(nullptr);
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = L"FF7RCoopModMonitorCover";
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            reg = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }
        int k = 0;
        for (int i = 0; i < g_MonN && k < 4; ++i)
        {
            if (!MonCoverWanted(i)) continue;
            const MonRect& m = g_MonM[i];
            if (!g_MonCover[k])
                g_MonCover[k] = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"FF7RCoopModMonitorCover", L"", WS_POPUP,
                    m.x, m.y, m.w, m.h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            // f1.308: TOPMOST (over the taskbar too - the author's 14:11 screenshot: desktop + taskbar on monitor 2 while the
            // game stood still loading a cutscene); only shown while the game is in front (wantCover below)
            if (g_MonCover[k]) SetWindowPos(g_MonCover[k], HWND_TOPMOST, m.x, m.y, m.w, m.h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ++k;
        }
        for (int j = k; j < 4; ++j) if (g_MonCover[j]) ShowWindow(g_MonCover[j], SW_HIDE);
        g_MonCoverN = k;
    }
    else for (int j = 0; j < 4; ++j) if (g_MonCover[j]) ShowWindow(g_MonCover[j], SW_HIDE);
    g_MonCoverShown = on;
}
static void MonRefresh()
{
    g_MonN = MonitorsNow(g_MonM);
    if (g_MonN < 1) return;
    // player 1's monitor: Main (Windows main) / Left / Middle / Right (by x)
    int left = 0, right = 0, mainI = 0;
    for (int i = 0; i < g_MonN; ++i)
    {
        if (g_MonM[i].x < g_MonM[left].x) left = i;
        if (g_MonM[i].x > g_MonM[right].x) right = i;
        if (g_MonM[i].primary) mainI = i;
    }
    int mid = mainI;
    if (g_MonN >= 3) { for (int i = 0; i < g_MonN; ++i) if (i != left && i != right) { mid = i; break; } }
    int p1 = g_Cfg.monP1 == 1 ? left : g_Cfg.monP1 == 3 ? right : g_Cfg.monP1 == 2 ? mid : mainI;
    if (p1 != g_MonP1)
    {
        static int pl = 0;
        if (pl < 10) { ++pl; CoopLog("MONITORS: player 1's monitor = #%d %d,%d %dx%d (%s)", p1 + 1, g_MonM[p1].x, g_MonM[p1].y, g_MonM[p1].w, g_MonM[p1].h,
            g_Cfg.monP1 == 1 ? "Left" : g_Cfg.monP1 == 2 ? "Middle" : g_Cfg.monP1 == 3 ? "Right" : "the Windows main monitor"); }
    }
    g_MonP1 = p1;
    g_MonP1R = {g_MonM[p1].x, g_MonM[p1].y, g_MonM[p1].x + g_MonM[p1].w, g_MonM[p1].y + g_MonM[p1].h};
    g_MonSpanR = g_MonP1R;
    for (int i = 0; i < g_MonN; ++i)
    {
        if (g_MonM[i].x < g_MonSpanR.left) g_MonSpanR.left = g_MonM[i].x;
        if (g_MonM[i].y < g_MonSpanR.top) g_MonSpanR.top = g_MonM[i].y;
        if (g_MonM[i].x + g_MonM[i].w > g_MonSpanR.right) g_MonSpanR.right = g_MonM[i].x + g_MonM[i].w;
        if (g_MonM[i].y + g_MonM[i].h > g_MonSpanR.bottom) g_MonSpanR.bottom = g_MonM[i].y + g_MonM[i].h;
    }
}
// the layout fractions for PipRect / SplitSide / the lens (pip_hud/30) - from MonitorRegions with player 1's monitor marked
static void MonLayoutUpdate(HWND h)
{
    RECT cr{}; POINT tl{0, 0}; GetClientRect(h, &cr); ClientToScreen(h, &tl);
    MonRect mm[8]; for (int i = 0; i < g_MonN; ++i) { mm[i] = g_MonM[i]; mm[i].primary = (i == g_MonP1); }
    float reg[3][4];
    int used = 0;
    if (MonOneScreen())
    {   // f1.311 / f1.314: one window = halves, both windows = thirds; player 1 always on the left (f1.315: no mirroring)
        for (int p = 0; p < 3; ++p) for (int k = 0; k < 4; ++k) reg[p][k] = 0.f;
        int nw = (g_P2WinOn ? 1 : 0) + (g_P3WinOn ? 1 : 0);
        if (nw > 0 && cr.right > 0)
        {
            float part = 1.f / (float)(nw + 1);
            reg[0][0] = 0.f; reg[0][1] = 0.f; reg[0][2] = part; reg[0][3] = 1.f;
            int q = 1;
            if (g_P2WinOn) { reg[1][0] = part * q; reg[1][1] = 0.f; reg[1][2] = part; reg[1][3] = 1.f; ++q; }
            if (g_P3WinOn) { reg[2][0] = part * q; reg[2][1] = 0.f; reg[2][2] = part; reg[2][3] = 1.f; ++q; }
            used = nw + 1;
        }
    }
    else used = MonitorRegions(mm, g_MonN, tl.x, tl.y, cr.right, cr.bottom, 1.f, 1.f, g_Cfg.monShare, g_P2WinOn, g_P3WinOn, reg);
    bool on = used >= 2;
    for (int p = 0; p < 3; ++p) for (int k = 0; k < 4; ++k) g_MonRegF[p][k] = reg[p][k];
    if (on != g_MonLayoutOn || on)
    {
        static float last[3][4] = {}; static int ll = 0;
        if (on && memcmp(last, reg, sizeof(last)) && ll < 30)
        {
            ++ll; memcpy(last, reg, sizeof(last));
            CoopLog("MONITORS: layout over %d %s - player 1 %.3f,%.3f %.3fx%.3f, player 2 %.3f,%.3f %.3fx%.3f, player 3 %.3f,%.3f %.3fx%.3f (of the picture)",
                used, MonOneScreen() ? "parts of one wide screen" : "monitors", reg[0][0], reg[0][1], reg[0][2], reg[0][3], reg[1][0], reg[1][1], reg[1][2], reg[1][3], reg[2][0], reg[2][1], reg[2][2], reg[2][3]);
        }
    }
    g_MonLayoutOn = on;
}
// HUDGEO research (log only): where the game's shown root widgets and their first children are, while spanning
static bool MonGeo(void* w, float pos[2], float size[2])
{
    static void* fGeo = nullptr; static void* sbl = nullptr; static void* fAbsSize = nullptr; static void* fL2A = nullptr; static bool tried = false;
    if (!tried)
    {
        tried = true;
        void* wc = FindObj("Widget", "Class"); fGeo = wc ? FindField(wc, "GetCachedGeometry") : nullptr;
        sbl = FindObj("Default__SlateBlueprintLibrary", "SlateBlueprintLibrary");
        fAbsSize = sbl ? FindFunc(sbl, "GetAbsoluteSize") : nullptr; fL2A = sbl ? FindFunc(sbl, "LocalToAbsolute") : nullptr;
        CoopLog("HUDGEO: GetCachedGeometry %s, GetAbsoluteSize %s, LocalToAbsolute %s", fGeo ? "found" : "NOT found", fAbsSize ? "found" : "NOT found", fL2A ? "found" : "NOT found");
    }
    if (!fGeo || !fAbsSize || !fL2A || !w) return false;
    int gs = ParmsSize(fGeo); if (gs <= 0 || gs > 0x80) return false;
    alignas(16) unsigned char g[0x80] = {};
    Orig_ProcessEvent(w, fGeo, g);   // only ReturnValue - the FGeometry
    int oG = ParamOff(fAbsSize, "Geometry"), oR = ParamOff(fAbsSize, "ReturnValue");
    if (oG < 0 || oR < 0 || ParmsSize(fAbsSize) > 0xC0) return false;
    alignas(16) unsigned char b[0xC0] = {};
    memcpy(b + oG, g, gs); Orig_ProcessEvent(sbl, fAbsSize, b); memcpy(size, b + oR, 8);
    int oG2 = ParamOff(fL2A, "Geometry"), oL = ParamOff(fL2A, "LocalCoordinate"), oR2 = ParamOff(fL2A, "ReturnValue");
    if (oG2 < 0 || oL < 0 || oR2 < 0 || ParmsSize(fL2A) > 0xC0) return false;
    memset(b, 0, sizeof(b)); memcpy(b + oG2, g, gs); Orig_ProcessEvent(sbl, fL2A, b); memcpy(pos, b + oR2, 8);
    return true;
}
static void MonGeoLogOne(void* w, int depth, int* budget)
{
    if (!w || *budget <= 0) return;
    --*budget;
    char nm[96] = "?", cn[64] = "?"; NameOf(w, nm, sizeof(nm)); ObjClassName(w, cn, sizeof(cn));
    float p[2] = {0, 0}, sz[2] = {0, 0}; bool ok = MonGeo(w, p, sz);
    char pj[64] = "";
    if (!strcmp(cn, "EndCanvasPanel") && g_EcpProj > 0 && Readable((unsigned char*)w + g_EcpFov, 4))
        snprintf(pj, sizeof(pj), " [projection %d, FOV %.1f]", ((unsigned char*)w)[g_EcpProj], *(float*)((unsigned char*)w + g_EcpFov));
    CoopLog("HUDGEO: %*s%s (%s) %s %.0f,%.0f %.0fx%.0f%s", depth * 2, "", nm, cn, ok ? "at" : "- no geometry", p[0], p[1], sz[0], sz[1], pj);
    if (depth >= 3) return;
    unsigned char* o = (unsigned char*)w; void* cls = *(void**)(o + 0x10);
    // a user widget -> its WidgetTree's RootWidget; a panel -> its Slots' Content
    static int oTree = -2, oRoot = -2, oSlots = -2, oContent = -2;
    if (oTree == -2) { void* c = FindObj("UserWidget", "Class"); oTree = c ? PropOff(c, "WidgetTree") : -1; void* t = FindObj("WidgetTree", "Class"); oRoot = t ? PropOff(t, "RootWidget") : -1;
        void* pw = FindObj("PanelWidget", "Class"); oSlots = pw ? PropOff(pw, "Slots") : -1; void* ps = FindObj("PanelSlot", "Class"); oContent = ps ? PropOff(ps, "Content") : -1; }
    if (oTree > 0 && FindField(cls, "WidgetTree"))
    {
        void* tree = Readable(o + oTree, 8) ? *(void**)(o + oTree) : nullptr;
        void* root = (tree && oRoot > 0 && Readable((unsigned char*)tree + oRoot, 8)) ? *(void**)((unsigned char*)tree + oRoot) : nullptr;
        if (root) MonGeoLogOne(root, depth + 1, budget);
        return;
    }
    if (oSlots > 0 && oContent > 0 && FindField(cls, "Slots") && Readable(o + oSlots, 16))
    {
        void** data = *(void***)(o + oSlots); int num = *(int*)(o + oSlots + 8);
        for (int i = 0; data && i < num && i < 12 && *budget > 0; ++i)
        {
            unsigned char* sl = (unsigned char*)data[i];
            if (sl && Readable(sl + oContent, 8)) MonGeoLogOne(*(void**)(sl + oContent), depth + 1, budget);
        }
    }
}
// f1.291: the game's HUD panels are EndCanvasPanels drawn through their own PERSPECTIVE projection (ProjectionMode /
// FieldOfView - the tilted command menu look). Across a 32:9 window that projection made player 1's HUD ~2x big (f1.290
// test 10:33: "when he opened his command window it was huge on the left screen"; HUDGEO: Command_C 3456x1944 on a
// 3840x1080 window). While the window spans the monitors each such panel's FOV is widened like the game camera's
// (tan(F/2) x window aspect / 16:9) through its SetFieldOfView; the old value goes back on one monitor.
static void* g_EcpSetFov = nullptr;
struct EcpKeep { int idx, ser; float fov0, set; };
static EcpKeep g_Ecp[160]; static int g_EcpN = 0;
static void EcpResolve(void* anyPanel)
{
    if (g_EcpProj == -2)
    {
        void* c = FindObj("EndCanvasPanel", "Class");
        g_EcpProj = c ? PropOff(c, "ProjectionMode") : -1; g_EcpFov = c ? PropOff(c, "FieldOfView") : -1;
        CoopLog("HUDFOV: EndCanvasPanel ProjectionMode +0x%X, FieldOfView +0x%X", g_EcpProj, g_EcpFov);
    }
    if (!g_EcpSetFov && anyPanel)
    {
        g_EcpSetFov = FindFunc(anyPanel, "SetFieldOfView");
        static bool said = false; if (!said) { said = true; CoopLog("HUDFOV: SetFieldOfView %s", g_EcpSetFov ? "found" : "NOT found - the HUD stays big on all monitors"); }
    }
}
static void EcpSet(void* w, float f)
{
    if (!g_EcpSetFov) return;
    int o = ParamOff(g_EcpSetFov, "InFieldOfView"); if (o < 0 || ParmsSize(g_EcpSetFov) > 16) return;
    alignas(16) unsigned char b[16] = {}; *(float*)(b + o) = f;
    Orig_ProcessEvent(w, g_EcpSetFov, b);
}
struct EcpCls { void* cls; unsigned char f; };   // f: 1 user widget, 2 panel, 4 EndCanvasPanel
static EcpCls g_EcpCls[128]; static int g_EcpClsN = 0;
static unsigned char EcpClassFlags(void* cls)
{
    for (int i = 0; i < g_EcpClsN; ++i) if (g_EcpCls[i].cls == cls) return g_EcpCls[i].f;
    unsigned char f = (FindField(cls, "WidgetTree") ? 1 : 0) | (FindField(cls, "Slots") ? 2 : 0) | (ClassIs(cls, "EndCanvasPanel") ? 4 : 0);
    if (g_EcpClsN < 128) g_EcpCls[g_EcpClsN++] = {cls, f};
    return f;
}
static void EcpVisit(void* w, float k, int* budget, int depth)
{
    if (!w || *budget <= 0 || depth > 8) return;
    --*budget;
    unsigned char* o = (unsigned char*)w; void* cls = *(void**)(o + 0x10);
    static int oTree = -2, oRoot = -2, oSlots = -2, oContent = -2;
    if (oTree == -2) { void* c = FindObj("UserWidget", "Class"); oTree = c ? PropOff(c, "WidgetTree") : -1; void* t = FindObj("WidgetTree", "Class"); oRoot = t ? PropOff(t, "RootWidget") : -1;
        void* pw = FindObj("PanelWidget", "Class"); oSlots = pw ? PropOff(pw, "Slots") : -1; void* ps = FindObj("PanelSlot", "Class"); oContent = ps ? PropOff(ps, "Content") : -1; }
    unsigned char cf = EcpClassFlags(cls);
    if (cf & 4)
    {
        EcpResolve(w);
        if (g_EcpProj > 0 && g_EcpFov > 0 && o[g_EcpProj] == 0 /* Perspective */)
        {
            int ix, se;
            if (MakeWeak(w, &ix, &se))
            {
                int i = 0; while (i < g_EcpN && !(g_Ecp[i].idx == ix && g_Ecp[i].ser == se)) ++i;
                if (i == g_EcpN && g_EcpN < 160) { g_Ecp[g_EcpN] = {ix, se, *(float*)(o + g_EcpFov), -1.f}; ++g_EcpN; }
                if (i < g_EcpN)
                {
                    EcpKeep& e = g_Ecp[i];
                    float want = k > 1.01f ? 2.f * atanf(tanf(e.fov0 * 0.5f * DEG_TO_RAD) * k) * RAD_TO_DEG : e.fov0;
                    if (e.fov0 > 1.f && e.fov0 < 170.f && fabsf(want - e.set) > 0.05f)
                    {
                        EcpSet(w, want); e.set = want;
                        static int logs = 0; if (logs < 12) { ++logs; char nm[96] = "?"; NameOf(w, nm, sizeof(nm));
                            CoopLog("HUDFOV: %s perspective FOV %.1f -> %.1f (%s)", nm, e.fov0, want, k > 1.01f ? "all monitors" : "one monitor - back"); }
                    }
                }
            }
        }
    }
    if (oTree > 0 && (cf & 1))
    {
        void* tree = Readable(o + oTree, 8) ? *(void**)(o + oTree) : nullptr;
        void* root = (tree && oRoot > 0 && Readable((unsigned char*)tree + oRoot, 8)) ? *(void**)((unsigned char*)tree + oRoot) : nullptr;
        if (root) EcpVisit(root, k, budget, depth + 1);
        return;
    }
    if (oSlots > 0 && oContent > 0 && (cf & 2) && Readable(o + oSlots, 16))
    {
        void** data = *(void***)(o + oSlots); int num = *(int*)(o + oSlots + 8);
        for (int i = 0; data && i < num && i < 40 && *budget > 0; ++i)
        {
            unsigned char* sl = (unsigned char*)data[i];
            if (sl && Readable(sl + oContent, 8)) EcpVisit(*(void**)(sl + oContent), k, budget, depth + 1);
        }
    }
}
static void MonHudFovTick(bool span)
{   // 4x a second: every shown root widget's perspective panels (new ones included); one monitor: put the old FOV back
    static ULONGLONG at = 0; ULONGLONG now = GetTickCount64();
    if (now - at < 250) return;
    at = now;
    float k = span && g_GameClientAspect > 2.2f ? g_GameClientAspect / (16.f / 9.f) : 1.f;
    if (k <= 1.01f)
    {   // back to the game's own values (only panels the mod changed)
        for (int i = 0; i < g_EcpN; ++i) if (g_Ecp[i].set > 0.f && fabsf(g_Ecp[i].set - g_Ecp[i].fov0) > 0.05f)
        { void* w = ResolveWeak(g_Ecp[i].idx, g_Ecp[i].ser); if (w) EcpSet(w, g_Ecp[i].fov0); g_Ecp[i].set = g_Ecp[i].fov0; }
        return;
    }
    int budget = 1500;
    for (int i = 0; i < g_RwN && budget > 0; ++i) EcpVisit(ResolveWeak(g_RwIdx[i], g_RwSer[i]), k, &budget, 0);
}
static void MonHudGeoLog(bool span)
{
    static int runs = 0, base = 0; static ULONGLONG at = 0; ULONGLONG now = GetTickCount64();
    if (!span) { if (base || !AnyWindowShowing() || g_RwN <= 0) return; base = 1; }   // one baseline: battle HUD on one monitor
    else { if (runs >= 3 || (at && now - at < 15000) || now - g_MonLastSwitch < 3000) return; at = now; ++runs; }
    EcpResolve(nullptr);
    CoopLog("HUDGEO: --- the game's shown root widgets %s (canvas %.0fx%.0f) ---", span ? "while the window spans the monitors" : "on ONE monitor (baseline)", g_PipCanvasW, g_PipCanvasH);
    int budget = 160;
    for (int i = 0; i < g_RwN && budget > 0; ++i) MonGeoLogOne(ResolveWeak(g_RwIdx[i], g_RwSer[i]), 0, &budget);
}
// f1.292: the game's HUD / menu roots are fixed 1920x1080 boxes scaled by the viewport's DPI scale, and the game's DPI rule
// (EndMenuDPIScalingRule) follows the WIDTH: f1.291 HUDGEO - one monitor Command_C / NaviMap_C 1920x1080 at 0,0, over two
// monitors 3840x2160 at 0,0 (bottom half off the screen = P1's command menu gone, the rest 2x). While spanning, the
// DPI scale is measured (the widest 16:9 root widget / 1920) and taken back to what one monitor of that height gets
// through UserInterfaceSettings' ApplicationScale - then every root box is 1920x1080 at the top left = player 1's monitor
// when it is the leftmost. Back to the game's value before switching to one monitor.
static float* MonAppScale()
{
    static int off = -2;
    void* cdo = FindObj("Default__UserInterfaceSettings", "UserInterfaceSettings");
    if (off == -2) { void* cls = FindObj("UserInterfaceSettings", "Class"); off = cls ? PropOff(cls, "ApplicationScale") : -1;
        CoopLog("HUDSCALE: UserInterfaceSettings %p, ApplicationScale +0x%X%s", cdo, off, off < 0 ? " NOT found - the HUD stays 2x over all monitors" : ""); }
    if (!cdo || off < 0 || !Readable((unsigned char*)cdo + off, 4)) return nullptr;
    return (float*)((unsigned char*)cdo + off);
}
static float g_UiApp0 = -1.f;            // the game's ApplicationScale (before the mod changed it)
static int g_UiFix = 0;                  // 0 none, 1 measure pending, 2 set
static ULONGLONG g_UiMeasureAt = 0; static int g_UiTries = 0;
static void MonUiRestore(const char* why)
{
    float* app = MonAppScale();
    if (app && g_UiApp0 > 0.f && fabsf(*app - g_UiApp0) > 0.0001f) { CoopLog("HUDSCALE: ApplicationScale %.3f -> %.3f (%s)", *app, g_UiApp0, why); *app = g_UiApp0; }
    g_UiFix = 0;
}
// f1.293 - THE HUD OVER ALL MONITORS (the author: "Let's keep going - try to do the clean fix"). RE (game.exe): the game's
// DPI rule (EndMenuDPIScalingRule 0x13A94A0) is fit-to-height (1.0 at 3840x1080 - fine). The 2x HUD comes from how a HUD
// screen is added: UEndUserWidget::AddToViewportEx (exec 0x1BD88E0 -> 0x13491B0) = the standard UUserWidget add - a
// canvas slot whose offset is read every frame from the widget (UUserWidget +0x1B0 anchors, +0x1C0 position x / y, +0x1C8
// size x / y, +0x1D0 alignment; getters 0x28F4030 / 0x28F3F50 / 0x28F4040; SetDesiredSizeInViewport 0x296E7A0 writes
// +0x1C8 / +0x1CC). With size 0 the slot takes the widget's desired size, and the game's canvas (EndCanvasPanel) wants the
// window's WIDTH x width * 9 / 16 = 3840x2160. While spanning: every shown root whose content is an EndCanvasPanel and whose
// size the game left at 0 gets size = its desired size / k (k = window aspect / 16:9); full-screen ones (at 0,0 and
// window-wide) are also put at player 1's monitor. Positioned ones (enemy names, reticle) keep the game's position and
// shrink around their alignment point. Put back to the game's values (size 0, position 0) on one monitor.
struct RootKeep { int idx, ser; float px, py, sx, sy; bool full; };
static RootKeep g_RootK[96]; static int g_RootKN = 0;
static void* g_RootDesFn = nullptr;
static bool RootDesired(void* w, float d[2])
{
    if (!g_RootDesFn) g_RootDesFn = FindFunc(w, "GetDesiredSize");
    if (!g_RootDesFn) return false;
    int o = ParamOff(g_RootDesFn, "ReturnValue"); if (o < 0 || ParmsSize(g_RootDesFn) > 16) return false;
    alignas(16) unsigned char b[16] = {};
    Orig_ProcessEvent(w, g_RootDesFn, b);
    memcpy(d, b + o, 8);
    return d[0] > 1.f && d[1] > 1.f;
}
static bool RootIsEndCanvas(void* w)
{   // the user widget's WidgetTree root is an EndCanvasPanel (the game's own canvas - its desired size follows the width)
    static int oTree = -2, oRoot = -2;
    if (oTree == -2) { void* c = FindObj("UserWidget", "Class"); oTree = c ? PropOff(c, "WidgetTree") : -1; void* t = FindObj("WidgetTree", "Class"); oRoot = t ? PropOff(t, "RootWidget") : -1; }
    unsigned char* o = (unsigned char*)w;
    if (oTree <= 0 || oRoot <= 0 || !Readable(o + oTree, 8)) return false;
    void* tree = *(void**)(o + oTree); if (!tree || !Readable((unsigned char*)tree + oRoot, 8)) return false;
    void* root = *(void**)((unsigned char*)tree + oRoot); if (!root) return false;
    return ClassIs(*(void**)((unsigned char*)root + 0x10), "EndCanvasPanel");
}
static void MonRootFixTick(bool span)
{
    static ULONGLONG at = 0; ULONGLONG now = GetTickCount64();
    if (now - at < 100) return;
    at = now;
    float W = g_PipCanvasW, H = g_PipCanvasH;
    float k = (span && H > 100.f) ? (W / H) / (16.f / 9.f) : 1.f;
    if (k <= 1.01f)
    {   // one monitor: the game's own values back
        for (int i = 0; i < g_RootKN; ++i)
        {
            unsigned char* w = (unsigned char*)ResolveWeak(g_RootK[i].idx, g_RootK[i].ser);
            if (w && Readable(w + 0x1B0, 0x28)) { float* of = (float*)(w + 0x1C0); of[0] = g_RootK[i].px; of[1] = g_RootK[i].py; of[2] = g_RootK[i].sx; of[3] = g_RootK[i].sy; }
        }
        if (g_RootKN) { CoopLog("HUDROOT: %d HUD screen(s) back to the game's own size / place (one monitor)", g_RootKN); g_RootKN = 0; }
        return;
    }
    float p1x = g_MonLayoutOn ? g_MonRegF[0][0] * W : 0.f, p1y = g_MonLayoutOn ? g_MonRegF[0][1] * H : 0.f;
    for (int r = 0; r < g_RwN; ++r)
    {
        unsigned char* w = (unsigned char*)ResolveWeak(g_RwIdx[r], g_RwSer[r]);
        if (!w || !Readable(w + 0x1B0, 0x28)) continue;
        int ix, se; if (!MakeWeak(w, &ix, &se)) continue;
        int i = 0; while (i < g_RootKN && !(g_RootK[i].idx == ix && g_RootK[i].ser == se)) ++i;
        float* an = (float*)(w + 0x1B0); float* of = (float*)(w + 0x1C0);
        if (i == g_RootKN)
        {   // first look at this one: only auto-sized roots on a point anchor with the game's canvas inside
            if (g_RootKN >= 96 || an[0] != an[2] || an[1] != an[3] || of[2] != 0.f || of[3] != 0.f || !RootIsEndCanvas(w)) continue;
            float d[2]; if (!RootDesired(w, d)) continue;
            bool full = of[0] == 0.f && of[1] == 0.f && an[0] == 0.f && an[1] == 0.f && d[0] >= W * 0.95f && fabsf(d[0] / d[1] - 16.f / 9.f) < 0.03f;
            g_RootK[g_RootKN++] = {ix, se, of[0], of[1], of[2], of[3], full};
            static int logs = 0; if (logs < 40) { ++logs; char nm[96] = "?"; NameOf(w, nm, sizeof(nm));
                CoopLog("HUDROOT: %s - wants %.0fx%.0f at %.0f,%.0f -> %.0fx%.0f%s", nm, d[0], d[1], of[0], of[1], d[0] / k, d[1] / k, full ? " at player 1's monitor" : " (its own place)"); }
        }
        RootKeep& kp = g_RootK[i];
        float d[2]; if (!RootDesired(w, d)) continue;
        of[2] = d[0] / k; of[3] = d[1] / k;
        if (kp.full) { of[0] = p1x; of[1] = p1y; }
    }
}
// f1.294 - UI SCALE PROBE (self-testing, while spanning). f1.293 HUDROOT log: EnemyStatus "wants 500x200" (slate units) but
// covers ~1000x400 pixels at ~2x its slate position - EVERYTHING in the game's UI is scaled x2 by the viewport's DPI
// scale (the root boxes are 1920x1080 slate = 3840x2160 px). ApplicationScale didn't change it (f1.292). So: which
// UserInterfaceSettings rule gives that 2.0? Logged: UIScaleRule, the custom rule's own answer (0x13A94A0) for this
// window; then the rule is switched to ShortestSide (0), then Vertical (3), 1 s each, and the widest 16:9 root measured
// after each. The first that brings it to one monitor's size stays while spanning; the game's rule goes back on one monitor.
static int g_UiRuleOff = -2; static unsigned char g_UiRule0 = 0xFF; static int g_UiProbe = 0; static ULONGLONG g_UiProbeAt = 0; static unsigned char g_UiRuleKeep = 0xFF;
static unsigned char* MonUiRule()
{
    void* cdo = FindObj("Default__UserInterfaceSettings", "UserInterfaceSettings");
    if (g_UiRuleOff == -2) { void* cls = FindObj("UserInterfaceSettings", "Class"); g_UiRuleOff = cls ? PropOff(cls, "UIScaleRule") : -1;
        CoopLog("UIPROBE: UserInterfaceSettings %p, UIScaleRule +0x%X", cdo, g_UiRuleOff); }
    if (!cdo || g_UiRuleOff < 0 || !Readable((unsigned char*)cdo + g_UiRuleOff, 1)) return nullptr;
    return (unsigned char*)cdo + g_UiRuleOff;
}
static float MonWidestRoot()
{
    float best = 0.f;
    for (int i = 0; i < g_RwN; ++i)
    {
        void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]); float p[2], sz[2];
        if (!w || !MonGeo(w, p, sz) || sz[1] < 100.f) continue;
        if (fabsf(sz[0] / sz[1] - 16.f / 9.f) < 0.05f && sz[0] > best) best = sz[0];
    }
    return best;
}
static void MonUiRuleRestore(const char* why)
{
    unsigned char* r = MonUiRule();
    if (r && g_UiRule0 != 0xFF && *r != g_UiRule0) { CoopLog("UIPROBE: UIScaleRule %d -> %d (%s)", *r, g_UiRule0, why); *r = g_UiRule0; }
    g_UiProbe = 0;
}
static void MonUiProbeTick(HWND h, ULONGLONG now)
{
    if (g_UiProbe == 0) { g_UiProbe = 1; g_UiProbeAt = now + 1500; return; }
    if (g_UiProbe >= 9 || now < g_UiProbeAt) return;
    unsigned char* r = MonUiRule(); if (!r) { g_UiProbe = 9; return; }
    RECT cr{}; GetClientRect(h, &cr);
    float wide = MonWidestRoot(), want = cr.bottom * 16.f / 9.f;
    if (g_UiRuleKeep != 0xFF && g_UiProbe == 1)
    {   // a rule that worked before - straight to it
        if (g_UiRule0 == 0xFF) g_UiRule0 = *r;
        *r = g_UiRuleKeep; CoopLog("UIPROBE: UIScaleRule %d -> %d (worked before)", g_UiRule0, g_UiRuleKeep); g_UiProbe = 9; return;
    }
    if (g_UiProbe == 1)
    {
        g_UiRule0 = *r;
        typedef float (*Rule_t)(unsigned long long);   // 0x13A94A0 takes the size as two floats packed in rcx (from 0x13A9480)
        float fw = (float)cr.right, fh = (float)cr.bottom; unsigned int bw, bh; memcpy(&bw, &fw, 4); memcpy(&bh, &fh, 4);
        float custom = ((Rule_t)(g_Base + 0x13A94A0))(((unsigned long long)bh << 32) | bw);
        CoopLog("UIPROBE: window %ldx%ld - UIScaleRule %d, the game's custom rule says %.3f, widest HUD root %.0f px (one monitor's would be %.0f)", cr.right, cr.bottom, g_UiRule0, custom, wide, want);
        if (wide > 0.f && fabsf(wide - want) < 8.f) { CoopLog("UIPROBE: already right - nothing to try"); g_UiProbe = 9; return; }
        *r = 0; CoopLog("UIPROBE: try 1 - UIScaleRule 0 (ShortestSide)"); g_UiProbe = 2; g_UiProbeAt = now + 1000; return;
    }
    if (g_UiProbe == 2 || g_UiProbe == 3)
    {
        CoopLog("UIPROBE: try %d (UIScaleRule %d) - widest HUD root now %.0f px (want %.0f)", g_UiProbe - 1, *r, wide, want);
        if (wide > 0.f && fabsf(wide - want) < 8.f) { g_UiRuleKeep = *r; CoopLog("UIPROBE: WORKS with UIScaleRule %d - kept while the window spans the monitors", *r); g_UiProbe = 9; return; }
        if (g_UiProbe == 2) { *r = 3; CoopLog("UIPROBE: try 2 - UIScaleRule 3 (Vertical)"); g_UiProbe = 3; g_UiProbeAt = now + 1000; return; }
        *r = g_UiRule0; CoopLog("UIPROBE: neither rule changed the HUD - the game's rule %d back", g_UiRule0); g_UiProbe = 9; return;
    }
}
// f1.295 - WHERE THE 2x COMES FROM (log only). f1.294: the UI scale rules change nothing (rule 4 = custom, its answer 1.000).
// Per shown root: the raw FGeometry (UE 4.18: +0 Size, +8 Scale = the accumulated LAYOUT scale from everything above it,
// +0xC AbsolutePosition ...), its viewport slot (+0x1B0 anchors, +0x1C0 position / size, +0x1D0 alignment) and its desired
// size. Scale 2 = something above the HUD scales it; scale 1 with a 3840-wide size = the HUD screen itself asks for that.
static void MonGeoRaw(bool span)
{
    static bool done[2] = {false, false}; if (done[span ? 1 : 0]) return; done[span ? 1 : 0] = true;   // f1.296: once per mode (f1.295 used both on one monitor)
    int runs = span ? 2 : 1;
    void* wc = FindObj("Widget", "Class"); void* fGeo = wc ? FindField(wc, "GetCachedGeometry") : nullptr;
    if (!fGeo) return;
    int gs = ParmsSize(fGeo); if (gs <= 0 || gs > 0x80) return;
    CoopLog("UIWHY: --- shown roots, raw geometry (%d bytes) + viewport slot + desired size (run %d) ---", gs, runs);
    for (int i = 0; i < g_RwN && i < 40; ++i)
    {
        unsigned char* w = (unsigned char*)ResolveWeak(g_RwIdx[i], g_RwSer[i]);
        if (!w || !Readable(w + 0x1B0, 0x28)) continue;
        alignas(16) unsigned char g[0x80] = {}; Orig_ProcessEvent(w, fGeo, g);
        float* gf = (float*)g; float* an = (float*)(w + 0x1B0); float* of = (float*)(w + 0x1C0); float* al = (float*)(w + 0x1D0);
        float d[2] = {0, 0}; RootDesired(w, d);
        char nm[96] = "?"; NameOf(w, nm, sizeof(nm));
        if (gf[0] < 1.f && d[0] < 1.f) continue;
        CoopLog("UIWHY: %s geo [%.1f %.1f | %.3f | %.1f %.1f | %.1f %.1f | %.3f | %.1f %.1f | %.3f %.3f %.3f %.3f %.1f %.1f] anchors %.2f,%.2f-%.2f,%.2f pos %.1f,%.1f size %.1f,%.1f align %.2f,%.2f desired %.1fx%.1f endcanvas %d",
            nm, gf[0], gf[1], gf[2], gf[3], gf[4], gf[5], gf[6], gf[7], gf[8], gf[9], gf[10], gf[11], gf[12], gf[13], gf[14], gf[15],
            an[0], an[1], an[2], an[3], of[0], of[1], of[2], of[3], al[0], al[1], d[0], d[1], (int)RootIsEndCanvas(w));
    }
}
// f1.297 - WHO ASKS FOR THE UI SCALE (log only). f1.296 UIWHY over two monitors: every HUD root is 1920x1080 slate units with
// an accumulated LAYOUT scale of 2.000 (one monitor: 1.000) - something above the HUD scales a fixed 1920x1080 area by 2
// ("fill" = max(3840/1920, 1080/1080)). The game's custom DPI rule (EndMenuDPIScalingRule::GetDPIScaleBasedOnSize, vtable
// slot 0x4D081C0 -> 0x13A9480) is replaced by a logging pass-through: each new (size asked, answer, the 4 callers) once.
typedef float (*DpiRule_t)(void* self, unsigned long long size);
static DpiRule_t g_DpiRuleOrig = nullptr;
static float Hook_DpiRule(void* self, unsigned long long size)
{
    float r = g_DpiRuleOrig(self, size);
    static unsigned long long seen[24]; static int ns = 0, logs = 0;
    unsigned rb; memcpy(&rb, &r, 4);
    unsigned long long key = size ^ ((unsigned long long)rb << 1);
    bool known = false; for (int i = 0; i < ns; ++i) if (seen[i] == key) known = true;
    if (!known && logs < 24)
    {
        if (ns < 24) seen[ns++] = key;
        ++logs;
        void* fr[6] = {}; USHORT n = RtlCaptureStackBackTrace(1, 6, fr, nullptr);
        char cs[200]; int o = 0; for (int i = 0; i < n && o < 180; ++i) o += snprintf(cs + o, sizeof(cs) - o, " %llX", (unsigned long long)((uintptr_t)fr[i] - g_Base));
        CoopLog("UIDPI: the UI scale asked for %dx%d -> %.3f (callers RVA%s)", (int)(size & 0xFFFFFFFF), (int)(size >> 32), r, cs);
    }
    return r;
}
// f1.298: the engine's game-viewport DPI scale (0x3264E30 = SGameLayerManager::GetGameViewportDPIScale: the rule for the
// viewport's size / the viewport's own geometry scale +0x208 - f1.297's caller chain). f1.297: the rule answers 1.000, yet
// the HUD's layout scale is 2.000. Logged: what this function returns (each new value once). If it returns ~2 while the
// window spans the monitors, the answer is divided by k (window aspect / 16:9) - the HUD then gets one monitor's scale.
typedef float (*VpDpi_t)(void* self);
static VpDpi_t g_VpDpiOrig = nullptr;
static float g_VpDpiK = 1.f;   // set by the switch tick: k while spanning, 1 otherwise
static float Hook_VpDpi(void* self)
{
    float r = g_VpDpiOrig(self), out = r;
    if (g_VpDpiK > 1.01f && r > 1.5f) out = r / g_VpDpiK;
    static float seen[16]; static int ns = 0;
    bool known = false; for (int i = 0; i < ns; ++i) if (fabsf(seen[i] - r) < 0.0005f) known = true;
    if (!known && ns < 16)
    {
        seen[ns++] = r;
        void* fr[4] = {}; USHORT n = RtlCaptureStackBackTrace(1, 4, fr, nullptr);
        char cs[120]; int o = 0; for (int i = 0; i < n && o < 100; ++i) o += snprintf(cs + o, sizeof(cs) - o, " %llX", (unsigned long long)((uintptr_t)fr[i] - g_Base));
        CoopLog("UIDPI: the game viewport's UI scale = %.3f%s (callers RVA%s)", r, out != r ? " - the mod makes it one monitor's" : "", cs);
    }
    if (out != r) { static int lg = 0; if (lg < 10) { ++lg; CoopLog("UIDPI: viewport UI scale %.3f -> %.3f (all monitors, k %.2f)", r, out, g_VpDpiK); } }
    return out;
}
static void MonDpiHookInstall()
{
    static bool done = false; if (done) return; done = true;
    void** slot = (void**)(g_Base + 0x4D081C0);
    if (!Readable(slot, 8) || (uintptr_t)*slot != g_Base + 0x13A9480) { CoopLog("UIDPI: the DPI rule slot doesn't hold 0x13A9480 - not hooked"); return; }
    DWORD old; if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) { CoopLog("UIDPI: VirtualProtect failed"); return; }
    g_DpiRuleOrig = (DpiRule_t)*slot; *slot = (void*)&Hook_DpiRule;
    VirtualProtect(slot, 8, old, &old);
    CoopLog("UIDPI: the game's DPI rule (vtable slot +0x238) now logs who asks (log only)");
    static const unsigned char exVp[16] = {0x40,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48,0x81,0xC1,0x70,0x05,0x00,0x00};
    g_VpDpiOrig = (VpDpi_t)MakeInlineHook(g_Base + 0x3264E30, (void*)Hook_VpDpi, 16, exVp, "game viewport UI scale (multiple monitors)");
}
// f1.300 - THE HUD PICTURE (RE 2026-11-05, after f1.299 made the HUD's layout scale 1.000 but the picture stayed 2x):
// FF7R does not draw its HUD straight onto the screen. SEndRootOverlay's paint (0x150A420) hands its widgets to the
// draw call 0x150AAA0 with a geometry (size 1920x1080 design units, scale = rect height / 1080) and the draw goes into
// the renderer's "InGameUIRenderTarget" (FSceneRenderTargets 0x5923AD0: made by 0x2541670, size +0x240 / +0x244 =
// r.InGameUI.FixedWidth/Height or 1920x1080 / 3840x2160), which the scene's post processing lays over EVERY view
// (the InGameUITexture scene texture - why the split-screen pictures carry the HUD too). That overlay maps the whole
// target onto the view by its WIDTH (16:9 - over 3840x1080 the target shows 3840x2160 = the 2x, bottom cut off).
// The fix: while spanning, the draw gets a geometry that puts the 1920x1080 HUD into the part of the target that the
// overlay shows on player 1's monitor (scale + offset). [PictureInPicture] MonitorHudMap (research): 1 = the overlay
// keeps 16:9 by width (default, from the screenshots), 2 = it stretches the target over the whole view, 0 = off.
typedef uintptr_t (*RootDraw_t)(void*, void*, float*, float*, void*, void*, void*, void*);
static RootDraw_t g_RootDrawOrig = nullptr;
static bool g_RootDrawOn = false;   // set by MonitorSwitchTick: spanning with the layout on
// f1.304: where the HUD sits on the window while spanning (window pixels: its top left + window pixels per design unit) -
// for the game's projection fields (below) and the mod's enemy-bar areas (36f EsRegion)
static bool g_HudSpanOk = false; static float g_HudSpanX = 0.f, g_HudSpanY = 0.f, g_HudSpanFit = 1.f, g_HudSpanW = 1920.f, g_HudSpanH = 1080.f;
static bool HudP1Part(float clipX, float clipY, float* x0, float* w)
{
    (void)clipY;
    if (!(g_MonMode >= 2 && !g_MonPend && g_RootDrawOn) || g_MonRegF[0][2] <= 0.f) return false;
    *x0 = g_MonRegF[0][0] * clipX; *w = g_MonRegF[0][2] * clipX;
    return true;
}
static uintptr_t Hook_RootDraw(void* self, void* ctx, float* geo, float* clip, void* a5, void* a6, void* a7, void* a8)
{
    static int lg = 0, lgSkip = 0;
    void* root = Readable((void*)(g_Base + 0x585A100), 8) ? *(void**)(g_Base + 0x585A100) : nullptr;
    int mode = g_Cfg.monHudMap;
    if (!g_RootDrawOrig) return 0;
    if (!g_RootDrawOn || mode <= 0 || self != root || !geo || !Readable(geo, 56) || !g_GameHwnd)
        return g_RootDrawOrig(self, ctx, geo, clip, a5, a6, a7, a8);
    RECT cr{}; GetClientRect(g_GameHwnd, &cr);
    float W = geo[0], H = geo[1], s = geo[2], Wv = (float)cr.right, Hv = (float)cr.bottom;
    float mw = g_MonRegF[0][2] * Wv, mh = g_MonRegF[0][3] * Hv;
    if (!(W > 1.f && H > 1.f && s > 0.01f && Wv > 1.f && Hv > 1.f && mw > 1.f && mh > 1.f))
    {
        if (lgSkip < 5) { ++lgSkip; CoopLog("HUDMAP: skipped - geometry %.1fx%.1f scale %.3f, window %.0fx%.0f, player 1's part %.0fx%.0f", W, H, s, Wv, Hv, mw, mh); }
        return g_RootDrawOrig(self, ctx, geo, clip, a5, a6, a7, a8);
    }
    float Wrt = W * s, Hrt = H * s;                       // the target in pixels (= the game's UI rect)
    float kx = Wrt / Wv, ky = mode == 2 ? Hrt / Hv : Wrt / Wv;   // target pixels per window pixel
    float mx = g_MonRegF[0][0] * Wv, my = g_MonRegF[0][1] * Hv;
    // f1.311: the HUD box takes the SHAPE of player 1's part - full height (window px per design unit = part height / 1080),
    // its width in design units = the part's (at least 1280 units: a narrower part scales the HUD down instead), so the
    // game lays its corners out to that part (a third / an ultrawide screen); a 16:9 monitor = 1920 x 1080 as before
    // f1.314: f1.311's "box shaped to the part" didn't work (the game lays the HUD out before painting - 15:12 test) -
    // the 16:9 HUD is fitted into player 1's part again; a part taller than 16:9 (a third) keeps it at the bottom
    // f1.315 (the author, f1.314 test 16:59 - 17:00: "Menu is TINY" - fitted into a 1280 px third it was 0.67 x): the HUD
    // keeps player 1's part's HEIGHT (normal size on a 1080 p part); a part narrower than 16:9 gets the HUD box at its left
    // edge (the box reaches into the next part - the game's own right-side pieces, e.g. the map, sit there, as in the
    // one-screen Thirds layout; player 1's health panel is moved back into their part - 36b), a wider part (one wide
    // screen without windows) has it in the middle
    float fit = mh / H;
    float boxW = W, boxH = H;   // design units
    float sx = fit * kx, sy = fit * ky;
    float hx0 = mw >= W * fit ? mx + (mw - W * fit) * 0.5f : mx, hy0 = my;   // the HUD's top left (window px)
    float ox = hx0 * kx, oy = hy0 * ky;
    // f1.302 (f1.301 test 13:17: right size, but everything 540 px too high): the overlay CENTRES the target on the view -
    // over 3840x1080 it shows the target's rows 540..1620 - so the HUD goes that much further down (and across, if the
    // target were ever wider than the view at this scale)
    if (mode == 1) { ox += (Wrt - Wv * kx) * 0.5f; oy += (Hrt - Hv * ky) * 0.5f; }
    // f1.304 (the author, f1.302 test 13:25: "The other HUD elements for both players do not work correctly. They don't stay on
    // target ... the enemy health bars etc don't stay over target"): every world->HUD placement (enemy bars, reticles,
    // off-screen arrows ...) goes through 0x1348940 / 0x1348AE0: project to window pixels s, then
    // hud = (s - root+0x390 - root+0x3A0 / 2) / root+0x3A8 + UIsize / (2 * UIdpi) - i.e. relative to the VIEW's centre
    // (OnPaint writes +0x390/+0x394 = the view's top left, +0x3A0/+0x3A4 = its size, +0x3A8 = the geometry scale). With
    // the HUD on player 1's monitor, a point at window pixel s must get hud = (s - HUD's top left) / fit. So right after
    // OnPaint wrote them (this draw is later in the same paint), the origin + scale are set to give exactly that; the next
    // frame's placements use them, and OnPaint writes the game's values again every frame (nothing to put back).
    {
        float hx = hx0, hy = hy0;   // the HUD's top left in WINDOW pixels
        unsigned char* r8 = (unsigned char*)self;
        if (Readable(r8 + 0x390, 0x1C))
        {
            float vw = *(float*)(r8 + 0x3A0), vh = *(float*)(r8 + 0x3A4);
            if (vw > 1.f && vh > 1.f)
            {
                *(float*)(r8 + 0x390) = hx + fit * W * 0.5f - vw * 0.5f;
                *(float*)(r8 + 0x394) = hy + fit * H * 0.5f - vh * 0.5f;
                *(float*)(r8 + 0x3A8) = fit;
                g_HudSpanX = hx; g_HudSpanY = hy; g_HudSpanFit = fit; g_HudSpanW = boxW * fit; g_HudSpanH = boxH * fit; g_HudSpanOk = true;
                static int lp = 0; static float lhx = -1.f;
                if (lp < 10 && fabsf(lhx - hx) > 0.5f) { ++lp; lhx = hx; CoopLog("HUDMAP: placements - view %.0fx%.0f, HUD top left %.0f,%.0f (window px), fit %.3f -> origin %.1f,%.1f scale %.3f",
                    vw, vh, hx, hy, fit, *(float*)(r8 + 0x390), *(float*)(r8 + 0x394), fit); }
            }
        }
    }
    float g2[14]; memcpy(g2, geo, 56);
    g2[0] = boxW; g2[1] = boxH;         // (the game's own 1920 x 1080 - f1.311's reshaping removed)
    g2[2] = sx > sy ? sx : sy;          // layout scale (text is drawn at this size)
    g2[3] = ox; g2[4] = oy;             // absolute position
    g2[7] = sx; g2[8] = 0.f; g2[9] = 0.f; g2[10] = sy; g2[11] = ox; g2[12] = oy;   // render transform
    unsigned char* fl = (unsigned char*)g2 + 0x34;
    if (fabsf(sx - sy) > 0.001f) *fl |= 1; else *fl &= (unsigned char)~1;
    if (lg < 12)
    {
        static float lw = 0, lh = 0, lsx = 0, lox = 0;
        if (fabsf(lw - Wv) > 0.5f || fabsf(lh - Hv) > 0.5f || fabsf(lsx - sx) > 0.001f || fabsf(lox - ox) > 0.5f)
        {
            ++lg; lw = Wv; lh = Hv; lsx = sx; lox = ox;
            int rtW = 0, rtH = 0; void* rt = nullptr;
            unsigned char* sc = (unsigned char*)(g_Base + 0x5923AD0);
            if (Readable(sc + 0x110, 8)) rt = *(void**)(sc + 0x110);
            if (Readable(sc + 0x240, 8)) { rtW = *(int*)(sc + 0x240); rtH = *(int*)(sc + 0x244); }
            CoopLog("HUDMAP: map %d - game geometry %.0fx%.0f scale %.3f (target %.0fx%.0f; renderer's InGameUI target %dx%d %s), window %.0fx%.0f, "
                "player 1's monitor %.0f,%.0f %.0fx%.0f -> HUD scale %.3f x %.3f at %.0f,%.0f in the target%s",
                mode, W, H, s, Wrt, Hrt, rtW, rtH, rt ? "made" : "not made yet", Wv, Hv, mx, my, mw, mh, sx, sy, ox, oy,
                (rtW && (abs(rtW - (int)(Wrt + 0.5f)) > 1 || abs(rtH - (int)(Hrt + 0.5f)) > 1)) ? " - NOTE the renderer's target size differs" : "");
        }
    }
    return g_RootDrawOrig(self, ctx, g2, clip, a5, a6, a7, a8);
}
// f1.301: f1.300's inline hook failed ("bytes at target don't match" - something else on this PC already changed the
// function's first bytes, e.g. an overlay). Now the two CALLS to it (0x150A1DF, 0x150A995 - both in SEndRootOverlay's
// paint) are pointed at a small jump next to the game's code -> our hook; the hook calls the function's own address, so
// whatever is in its first bytes keeps working. Patched on the game thread (the HUD is painted on it too).
static void* MonAllocNear(uintptr_t nearAddr)
{
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t g = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    uintptr_t base = nearAddr & ~(g - 1);
    for (uintptr_t d = g; d < 0x60000000; d += g)
    {
        for (int sgn = 0; sgn < 2; ++sgn)
        {
            uintptr_t a = sgn ? base + d : base - d;
            if (!sgn && base < d) continue;
            void* p = VirtualAlloc((void*)a, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return nullptr;
}
static void MonRootDrawInstall()
{
    static bool done = false; if (done) return; done = true;
    uintptr_t fn = g_Base + 0x150AAA0;
    {
        unsigned char* b = (unsigned char*)fn; char hx[64] = {}; int o = 0;
        if (Readable(b, 16)) for (int i = 0; i < 16; ++i) o += snprintf(hx + o, sizeof(hx) - o, "%02X ", b[i]);
        CoopLog("HUDMAP: the HUD draw function's first bytes: %s(game's own: 4C 8B DC 55 41 54 41 55 48 8D AB 18 FB FF FF)", hx);
    }
    unsigned char* stub = (unsigned char*)MonAllocNear(fn);
    if (!stub) { CoopLog("HUDMAP: no memory next to the game's code - HUD draw hook NOT on"); return; }
    stub[0] = 0xFF; stub[1] = 0x25; *(DWORD*)(stub + 2) = 0; *(uint64_t*)(stub + 6) = (uint64_t)(void*)&Hook_RootDraw;
    FlushInstructionCache(GetCurrentProcess(), stub, 14);
    g_RootDrawOrig = (RootDraw_t)fn;
    static const uintptr_t sites[2] = {0x150A1DF, 0x150A995};
    int ok = 0;
    for (uintptr_t rva : sites)
    {
        unsigned char* c = (unsigned char*)(g_Base + rva);
        if (!Readable(c, 5) || c[0] != 0xE8) { CoopLog("HUDMAP: call at RVA %llX not found (byte %02X) - skipped", (unsigned long long)rva, Readable(c, 1) ? c[0] : 0); continue; }
        int32_t rel; memcpy(&rel, c + 1, 4);
        if ((uintptr_t)(c + 5 + rel) != fn) { CoopLog("HUDMAP: call at RVA %llX goes elsewhere (RVA %llX) - skipped", (unsigned long long)rva, (unsigned long long)((uintptr_t)(c + 5 + rel) - g_Base)); continue; }
        int64_t nr = (int64_t)(uintptr_t)stub - (int64_t)(uintptr_t)(c + 5);
        if (nr > INT32_MAX || nr < INT32_MIN) { CoopLog("HUDMAP: jump too far - skipped"); continue; }
        int32_t n32 = (int32_t)nr; DWORD old;
        if (!VirtualProtect(c + 1, 4, PAGE_EXECUTE_READWRITE, &old)) { CoopLog("HUDMAP: VirtualProtect failed"); continue; }
        memcpy(c + 1, &n32, 4);
        VirtualProtect(c + 1, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), c, 5);
        ++ok;
    }
    CoopLog("HUDMAP: HUD draw hook on %d of 2 calls (MonitorHudMap=%d)", ok, g_Cfg.monHudMap);
    if (!ok) g_RootDrawOrig = nullptr;
}
// f1.306 - PANINI (the author, f1.305 test 13:42: player 2's menu "squished" on the left of their window, their party
// panel "stretched" on the right - measured: x scale 0.79 near the left edge, 1.22 near the right, heights unchanged =
// a horizontal warp centred on the middle of the whole picture, not a size): the engine's Panini projection
// (r.Upscale.Panini.D, applied about the middle of each view in the upscale pass) bends a wide view sideways; the split
// pictures are wide views whose player sees only one side of it, so the bend shows. While spanning the mod sets D to 0
// (and the game's own value back on one monitor). [PictureInPicture] MonitorNoPanini=0 leaves it alone (research key).
// The values live behind the console variables' data pointers (registered 0x4B8D50 / ...: D 0x59235C0, S 0x59235D8,
// ScreenFit 0x59235F0, r.Upscale.Quality 0x59232D8 - float / int, [0] = the game thread's copy).
static float MonCvarF(uintptr_t rva, bool* ok)
{
    void** pp = (void**)(g_Base + rva); *ok = false;
    if (!Readable(pp, 8) || !*pp || !Readable(*pp, 4)) return 0.f;
    *ok = true; return *(float*)*pp;
}
static float g_PaniniGame = -1.f; static bool g_PaniniOff = false;
static void MonPaniniLog(const char* why)
{
    bool o1, o2, o3, o4; float d = MonCvarF(0x59235C0, &o1), sv = MonCvarF(0x59235D8, &o2), fit = MonCvarF(0x59235F0, &o3);
    int q = 0; { void** pp = (void**)(g_Base + 0x59232D8); o4 = Readable(pp, 8) && *pp && Readable(*pp, 4); if (o4) q = *(int*)*pp; }
    CoopLog("PANINI: %s - r.Upscale.Panini.D %s%.3f, S %s%.3f, ScreenFit %s%.3f, r.Upscale.Quality %s%d", why, o1 ? "" : "(not readable) ", d, o2 ? "" : "(not readable) ", sv,
        o3 ? "" : "(not readable) ", fit, o4 ? "" : "(not readable) ", q);
}
static void MonPaniniSet(bool span)
{
    if (!g_Cfg.monNoPanini) return;
    bool ok; float d = MonCvarF(0x59235C0, &ok);
    if (!ok) return;
    if (span && !g_PaniniOff)
    {
        g_PaniniGame = d;
        if (d > 0.0005f) { SpanConsole(L"r.Upscale.Panini.D 0"); g_PaniniOff = true; CoopLog("PANINI: off while spanning (the game's D %.3f)", d); }
        else CoopLog("PANINI: the game's D is already %.3f - nothing to switch off", d);
    }
    else if (!span && g_PaniniOff)
    {
        wchar_t c[64]; swprintf(c, 64, L"r.Upscale.Panini.D %.3f", g_PaniniGame); SpanConsole(c); g_PaniniOff = false;
        CoopLog("PANINI: the game's D %.3f back (one monitor)", g_PaniniGame);
    }
}
static ULONGLONG g_MonTickAt = 0;
static void MonitorSwitchTick();
// f1.306 (the author, f1.305 test 13:45: "Cutscene didn't cause P2 to go black, it just went full screen across both"):
// the main tick (core/27, the player controller's) doesn't run during some cutscenes (13:45:40 - 13:45:48: none of its
// lines) - so the camera hook (runs every frame) calls the switching too when the main tick has missed 50 ms.
static void MonitorSwitchTickMaybe()
{
    if (!g_Cfg.monSpanTest) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_MonTickAt < 50) return;
    static int logs = 0; if (logs < 5) { ++logs; CoopLog("MONITORS: the main tick stopped (cutscene?) - switching checked from the camera"); }
    MonitorSwitchTick();
}
static void MonitorSwitchTick()   // every frame (core/27), game thread
{
    g_MonTickAt = GetTickCount64();
    { static bool said = false; if (!said && g_Cfg.monSpanTest) { said = true; MonPaniniLog("at start"); } }
    HWND h = g_GameHwnd;
    ULONGLONG now = GetTickCount64();
    if (h) { RECT cr{}; GetClientRect(h, &cr); g_GameClientAspect = cr.bottom > 0 ? (float)cr.right / (float)cr.bottom : 0.f; }
    if (false) MonDpiHookInstall();   // f1.297 / f1.298 research hooks (DPI rule log, viewport UI scale) - not installed since f1.299 (the cause was r.InGameUI)
    if (g_Cfg.monSpanTest && g_Cfg.monHudMap > 0) MonRootDrawInstall();   // f1.300
    g_RootDrawOn = false;
    if (!g_Cfg.monSpanTest || !h)
    {
        if (g_MonMode != 0) { MonPaniniSet(false); MonCoversShow(false); MonUiRestore("multiple monitors off"); MonUiRuleRestore("multiple monitors off"); SpanUiFixed(0, 0); g_MonLayoutOn = false; g_MonMode = 0; CoopLog("MONITORS: multiple monitors switched off - the window stays as it is"); }
        g_HudSpanOk = false;   // f1.328 (code review #9): the HUD map's placements would stay on until a restart
        return;
    }
    if (!g_MonSeen) g_MonSeen = now;
    if (now - g_MonSeen < 3000) return;   // let the window settle at start
    if (now >= g_MonRefresh) { g_MonRefresh = now + 2000; MonRefresh(); }
    if ((g_MonN < 2 && !MonOneScreen()) || g_MonP1 < 0)
    {
        static bool said = false; if (!said) { said = true; CoopLog("MONITORS: multiple monitors on, but only %d monitor found (not wider than 2.2:1) - normal single screen", g_MonN); }
        g_MonLayoutOn = false; g_HudSpanOk = false; return;   // f1.328 (code review #9)
    }
    bool fg = GetForegroundWindow() == h;
    // which mode is wanted now
    // f1.309 (the author, f1.308 test 14:19: "That pause should not be happening - I have a feeling it has to do with the
    // resolution shift. Doesn't happen on normal split screen."): mode 3 = the window STAYS over all monitors (no r.SetRes,
    // no window move) with player 1's view moved onto their monitor (off-centre lens, camera/38) and the other monitors
    // black (covers) - for cutscenes, loading screens, talking scenes, story shots and gameplay without the windows.
    // Only the main menu / pause still switch the real resolution to one monitor (mode 1): the game is paused there, and
    // menus need the mouse to hit where they are drawn.
    bool menuUp = g_MainMenuUp || g_GamePaused;
    bool split = AnyWindowShowing() && !g_LoadingUp && !g_InCutscene && !g_TalkScene && !g_ScriptedShot;
    int want = menuUp ? 1 : split ? 2 : (g_MonMode >= 2 ? 3 : 1);
    if (g_MonMode == 0) want = 1;
    // f1.312 (the author, f1.311 test 15:12: "it shows, but the menus are in the wrong places" - the HUD box can't take
    // another shape from the draw: the game lays its HUD out before painting): one wide screen = ALWAYS the whole wide
    // window (mode 2, no switching - menus / cutscenes too) with the game's HUD laid out over all of it by the game itself
    // (r.InGameUI.FixedWidth / FixedHeight = the window's size: its UI rect then has the window's shape, the layout box =
    // that shape at height 1080, and the in-scene HUD overlay maps it 1:1). The split screen is then the ordinary one-screen
    // split (halves / Thirds) - its menu / panel / bar moves were made for exactly that.
    // f1.314 (the author, f1.313 test 15:26: the title / menus "not usable" - stretched 2x top to bottom: the in-scene HUD
    // overlay always maps the target as a 16:9 picture, so a 32:9 UI rect can't work): one wide screen = the monitors
    // machinery with halves / thirds as the parts (the HUD drawn into player 1's part, 16:9, by the draw hook); several
    // monitors (the test) - menus on one monitor as usual; one ultrawide monitor - menus on the whole screen (mode 3: the
    // HUD 16:9 in its middle), never a resolution switch
    if (MonOneScreen() && g_MonN == 1) want = split ? 2 : 3;
    if (want != g_MonWant) { g_MonWant = want; g_MonWantSince = now; }
    if (!g_MonPend && g_MonMode >= 2 && g_MonWant >= 2 && g_MonWant != g_MonMode)
    {   // 2 <-> 3: both over all monitors - nothing to resize, just the covers / the lens / the HUD part
        CoopLog("MONITORS: %s (%s) - the window stays over all monitors", g_MonWant == 2 ? "split screen over all monitors" : "player 1's view on their monitor, the others black",
            g_MonWant == 2 ? "split-screen windows showing" : g_LoadingUp ? "loading screen" : g_InCutscene ? "cutscene" : g_TalkScene ? "talking scene" : g_ScriptedShot ? "story / cinematic shot" : "no split-screen window");
        g_MonMode = g_MonWant; g_MonLastSwitch = now;
        if (g_MonMode == 3 && fg) MonCoversShow(true);
    }
    if (!g_MonPend && g_MonWant != g_MonMode && (g_MonMode == 0 || (now - g_MonWantSince >= 400 && now - g_MonLastSwitch >= 1000)))
    {   // step 1: the game's render size
        const RECT& r = g_MonWant >= 2 ? g_MonSpanR : g_MonP1R;
        CoopLog("MONITORS: switching to %s (%s)", g_MonWant >= 2 ? "ALL monitors" : "player 1's monitor only",
            g_MonWant == 2 ? "split-screen windows showing" : g_MonWant == 3 ? "one wide screen" : g_MonMode == 0 ? "start" : g_MainMenuUp ? "main menu" : g_GamePaused ? "paused" :
            g_LoadingUp ? "loading screen" : g_InCutscene ? "cutscene" : g_TalkScene ? "talking scene" : g_ScriptedShot ? "story / cinematic shot" : "no split-screen window");
        if (g_MonWant < 2) { MonUiRestore("one monitor"); MonUiRuleRestore("one monitor"); }   // f1.292 / f1.294
        SpanSetRes(r.right - r.left, r.bottom - r.top);
        MonPaniniSet(g_MonWant >= 2);   // f1.306
        if (g_MonWant >= 2)
        {   // f1.300: the HUD target at the window's width, 16:9 (3840x2160 over two 1080p monitors = the game's own 4K choice;
            // 5760x3240 over three) - the HUD draw hook then puts the HUD at 1:1 onto player 1's monitor (f1.299 had one monitor's size)
            int tw = r.right - r.left;
            SpanUiFixed(g_Cfg.monHudMap > 0 ? tw : g_MonP1R.right - g_MonP1R.left, g_Cfg.monHudMap > 0 ? (tw * 9 + 8) / 16 : g_MonP1R.bottom - g_MonP1R.top);
        }
        else SpanUiFixed(0, 0);   // the game's own UI size again
        g_MonPend = g_MonWant; g_MonApplyAt = now + 200; g_MonLastSwitch = now;
        if (g_MonWant >= 2) MonCoversShow(false);
        else if (fg) MonCoversShow(true);   // f1.308: black at once (step 1) - a cutscene load can hold the game for seconds before step 2
        return;
    }
    if (g_MonPend && now >= g_MonApplyAt)
    {   // step 2: the window over the monitor(s)
        const RECT& r = g_MonPend >= 2 ? g_MonSpanR : g_MonP1R;
        SpanApplyWindow(h, r, g_MonPend >= 2 ? "all monitors" : "player 1's monitor");
        g_MonMode = g_MonPend; g_MonPend = 0; g_MonFixes = 0; g_MonNextCheck = now + 1000;
        if (g_MonMode == 2) { g_UiFix = 1; g_UiTries = 0; g_UiMeasureAt = now + 500; }   // f1.292: measure the HUD scale
        if (g_MonMode == 1 && fg) MonCoversShow(true);
        return;
    }
    if (g_MonMode == 2) MonLayoutUpdate(h); else g_MonLayoutOn = false;
    if (g_MonMode == 3)
    {   // f1.309: player 1's monitor as a part of the window (fractions), no other parts
        RECT cr{}; POINT tl{0, 0}; GetClientRect(h, &cr); ClientToScreen(h, &tl);
        float W = (float)cr.right, H = (float)cr.bottom;
        if (W > 1.f && H > 1.f)
        {
            g_MonRegF[0][0] = (g_MonP1R.left - tl.x) / W; g_MonRegF[0][1] = (g_MonP1R.top - tl.y) / H;
            g_MonRegF[0][2] = (g_MonP1R.right - g_MonP1R.left) / W; g_MonRegF[0][3] = (g_MonP1R.bottom - g_MonP1R.top) / H;
            if (MonOneScreen()) { g_MonRegF[0][0] = 0.f; g_MonRegF[0][1] = 0.f; g_MonRegF[0][2] = 1.f; g_MonRegF[0][3] = 1.f; }   // f1.311: the whole wide screen
            for (int p = 1; p < 3; ++p) for (int k = 0; k < 4; ++k) g_MonRegF[p][k] = 0.f;
        }
    }
    g_RootDrawOn = ((g_MonMode == 2 && g_MonLayoutOn) || g_MonMode == 3) && !g_MonPend;   // f1.300: the HUD onto player 1's monitor (f1.309: + mode 3)
    if (!g_RootDrawOn) g_HudSpanOk = false;
    {   // f1.298: k for the viewport UI scale hook (only while spanning)
        float nk = (g_MonMode == 2 && !g_MonPend && g_GameClientAspect > 2.2f) ? g_GameClientAspect / (16.f / 9.f) : 1.f;
        g_VpDpiK = nk;
    }
    // covers only while the game is in front (alt-tab: the desktop is usable)
    bool spare = false; if (g_MonMode == 2) for (int i = 0; i < g_MonN; ++i) if (MonCoverWanted(i)) spare = true;   // f1.310
    static bool lastSpare = false; if (spare != lastSpare) { lastSpare = spare; if (g_MonCoverShown) MonCoversShow(true); }   // which monitors changed
    bool wantCover = (g_MonMode == 1 || g_MonMode == 3 || spare) && !g_MonPend && (fg || (GetForegroundWindow() && GetAncestor(GetForegroundWindow(), GA_ROOT) == h));
    if (wantCover != g_MonCoverShown) MonCoversShow(wantCover);
    // the game put its window back somewhere else (alt-tab, its own resolution change) - again, up to 5 times per switch
    if (now >= g_MonNextCheck && !g_MonPend && fg)
    {
        g_MonNextCheck = now + 1000;
        const RECT& r = g_MonMode >= 2 ? g_MonSpanR : g_MonP1R;
        RECT wr{}; GetWindowRect(h, &wr);
        if ((wr.left != r.left || wr.top != r.top || wr.right != r.right || wr.bottom != r.bottom || (GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION)) && g_MonFixes < 5)
        {
            ++g_MonFixes;
            SpanState(h, "the window is not where it should be", nullptr);
            SpanApplyWindow(h, r, "put back");
        }
    }
    MonHudFovTick(false);   // f1.291's HUD-panel FOV widening didn't change the HUD (the scale is the DPI - f1.292); false = only puts back
    MonRootFixTick(false);   // f1.293's HUD screen sizes: off for the f1.294 probe (false = only puts the game's values back)
    if (g_MonMode == 2 && g_MonLayoutOn && !g_MonPend && now - g_MonLastSwitch > 3000) MonGeoRaw(true);   // f1.295 (log only; the f1.294 probe is off)
    else if (g_MonMode == 1 && !g_MonPend && now - g_MonLastSwitch > 3000 && AnyWindowShowing()) MonGeoRaw(false);   // a one-monitor run for comparison
    if (false) MonUiProbeTick(h, now);   // (f1.294 probe kept for reference, not run)
    if (g_MonMode == 2 && g_MonLayoutOn) MonHudGeoLog(true);
    else if (g_MonMode == 1 && !g_MonPend && now - g_MonLastSwitch > 3000) MonHudGeoLog(false);
}
