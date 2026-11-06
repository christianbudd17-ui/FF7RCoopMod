// =================================================================================
// f1.154 - MULTI-MONITOR, step 1 (the author 2026-10-25: "having the option for multiple screens. This should be put under
// experimental" ... "three monitor support would ultimately be the end goal"). Plan: claude/FF7R_Multi_Monitor_Plan.md.
// The way chosen: the game runs as ONE borderless window spanning the monitors; every player gets a monitor - player 1 the
// Windows main monitor, players 2 / 3 the others left to right (the author: "Windows main monitor"). With two monitors and
// three players, players 2 and 3 share the second monitor side by side (the author: "vertical split sort of how we have set
// up for a wide screen monitor"). No second game window / swap chain (too risky).
// This step = research only (log, no behaviour change) + the layout maths (MonitorRegions, unit-tested in
// tools/tests/unit_monitors.cpp), not wired to the drawing yet:
//  - "MONITORS:" lines at start and whenever the monitors or the game window change (checked every 5 s): every monitor's
//    desktop rectangle + which is the Windows main one, the game window / client rectangle + style, the canvas size.
//  - "MONITORS: game settings ..." once: the game's UGameUserSettings (GetGameUserSettings) - its resolution / window mode
//    getters read (read-only), and whether the setters a spanning window would need exist.
// =================================================================================
struct MonRect { int x, y, w, h; bool primary; };

// The player regions for the monitors layout, in canvas pixels. m = the monitors (desktop pixels), client = the game's
// drawing area on the desktop, cw / ch = the canvas size (the game may render at another size - scaled). share2: with two
// monitors and both players 2 and 3 in - 0 = side by side (player 2 left, player 3 right), 1 = player 2 top, player 3
// bottom. out[p] = {x, y, w, h} for player p + 1 (w = 0: no region - that player isn't in / no monitor for them).
// Returns the number of monitors used (< 2 = no monitors layout possible).
static int MonitorRegions(const MonRect* m, int n, int clX, int clY, int clW, int clH, float cw, float ch, int share2,
                          bool p2On, bool p3On, float out[3][4])
{
    for (int p = 0; p < 3; ++p) for (int k = 0; k < 4; ++k) out[p][k] = 0.f;
    if (clW <= 0 || clH <= 0 || cw <= 0.f || ch <= 0.f) return 0;
    int idx[8], k = 0;
    for (int i = 0; i < n && k < 8; ++i)
    {   // only monitors the game's drawing area covers (at least half of the monitor)
        int x0 = m[i].x > clX ? m[i].x : clX, x1 = (m[i].x + m[i].w) < (clX + clW) ? (m[i].x + m[i].w) : (clX + clW);
        int y0 = m[i].y > clY ? m[i].y : clY, y1 = (m[i].y + m[i].h) < (clY + clH) ? (m[i].y + m[i].h) : (clY + clH);
        if (x1 > x0 && y1 > y0 && (long long)(x1 - x0) * (y1 - y0) * 2 >= (long long)m[i].w * m[i].h) idx[k++] = i;
    }
    if (k < 2) return k;
    int first = 0;   // player 1: the Windows main monitor (else the leftmost)
    for (int i = 0; i < k; ++i) if (m[idx[i]].primary) { first = i; break; }
    int order[8], no = 0; order[no++] = idx[first];
    int rest[8], nr = 0; for (int i = 0; i < k; ++i) if (i != first) rest[nr++] = idx[i];
    for (int a = 0; a < nr; ++a) for (int b = a + 1; b < nr; ++b)   // the others left to right (then top to bottom)
        if (m[rest[b]].x < m[rest[a]].x || (m[rest[b]].x == m[rest[a]].x && m[rest[b]].y < m[rest[a]].y)) { int t = rest[a]; rest[a] = rest[b]; rest[b] = t; }
    for (int i = 0; i < nr; ++i) order[no++] = rest[i];
    float sx = cw / (float)clW, sy = ch / (float)clH;
    auto R = [&](int mi, float r[4]) {   // monitor mi -> canvas pixels, clipped to the drawing area
        int x0 = m[mi].x > clX ? m[mi].x : clX, x1 = (m[mi].x + m[mi].w) < (clX + clW) ? (m[mi].x + m[mi].w) : (clX + clW);
        int y0 = m[mi].y > clY ? m[mi].y : clY, y1 = (m[mi].y + m[mi].h) < (clY + clH) ? (m[mi].y + m[mi].h) : (clY + clH);
        r[0] = (x0 - clX) * sx; r[1] = (y0 - clY) * sy; r[2] = (x1 - x0) * sx; r[3] = (y1 - y0) * sy;
    };
    R(order[0], out[0]);
    if (no >= 3)
    {   // three monitors: one each (a player who isn't in leaves their monitor to player 1's wider view)
        if (p2On) R(order[1], out[1]);
        if (p3On) R(order[2], out[2]);
        return no;
    }
    float r[4]; R(order[1], r);   // two monitors: players 2 and 3 share the second
    if (p2On && p3On)
    {
        for (int q = 1; q < 3; ++q) for (int j = 0; j < 4; ++j) out[q][j] = r[j];
        if (share2 == 1) { out[1][3] = out[2][3] = r[3] * 0.5f; out[2][1] = r[1] + r[3] * 0.5f; }
        else { out[1][2] = out[2][2] = r[2] * 0.5f; out[2][0] = r[0] + r[2] * 0.5f; }
    }
    else if (p2On) for (int j = 0; j < 4; ++j) out[1][j] = r[j];
    else if (p3On) for (int j = 0; j < 4; ++j) out[2][j] = r[j];
    return no;
}

#ifndef COOP_UNIT_TEST
struct MonEnumBuf { MonRect* out; int n; };
static BOOL CALLBACK MonEnumCb(HMONITOR hm, HDC, LPRECT, LPARAM lp)
{
    MonEnumBuf* v = (MonEnumBuf*)lp;
    if (v->n >= 8) return FALSE;
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hm, &mi)) return TRUE;
    MonRect& r = v->out[v->n++];
    r.x = mi.rcMonitor.left; r.y = mi.rcMonitor.top; r.w = mi.rcMonitor.right - mi.rcMonitor.left; r.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    r.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    return TRUE;
}
static int MonitorsNow(MonRect* out)
{
    MonEnumBuf v{out, 0};
    EnumDisplayMonitors(nullptr, nullptr, MonEnumCb, (LPARAM)&v);
    return v.n;
}
// the game's UGameUserSettings: read its resolution + window mode once; note which setters exist (log only)
static void* MonGus(bool quiet)   // the game's UGameUserSettings object (nullptr = not found; logged unless quiet)
{
    void* cdo = FindObj("Default__GameUserSettings", "GameUserSettings");
    void* fGet = cdo ? FindFunc(cdo, "GetGameUserSettings") : nullptr;
    int oRet = fGet ? ParamOff(fGet, "ReturnValue") : -1;
    if (!fGet || oRet < 0 || ParmsSize(fGet) > 32) { if (!quiet) CoopLog("MONITORS: game settings object NOT found (GetGameUserSettings) - a spanning window would have to be set up another way"); return nullptr; }
    alignas(16) unsigned char b[32] = {};
    Orig_ProcessEvent(cdo, fGet, b);
    void* gus = *(void**)(b + oRet);
    if (!gus || !Readable(gus, 0x40)) { if (!quiet) CoopLog("MONITORS: game settings object NOT available (GetGameUserSettings returned none)"); return nullptr; }
    return gus;
}
static void MonitorGameSettingsLog()
{
    void* gus = MonGus(false);
    if (!gus) return;
    char cn[96] = "?"; ObjClassName(gus, cn, sizeof(cn));
    int resX = -1, resY = -1, mode = -1;
    if (void* f = FindFunc(gus, "GetScreenResolution")) { int o = ParamOff(f, "ReturnValue"); if (o >= 0 && ParmsSize(f) <= 32) { alignas(16) unsigned char c[32] = {}; Orig_ProcessEvent(gus, f, c); resX = *(int*)(c + o); resY = *(int*)(c + o + 4); } }
    if (void* f = FindFunc(gus, "GetFullscreenMode")) { int o = ParamOff(f, "ReturnValue"); if (o >= 0 && ParmsSize(f) <= 32) { alignas(16) unsigned char c[32] = {}; Orig_ProcessEvent(gus, f, c); mode = c[o]; } }
    static const char* const kMode[3] = {"fullscreen", "borderless window", "window"};
    CoopLog("MONITORS: game settings %s - resolution %d x %d, window mode %d (%s); setters: SetScreenResolution %s, SetFullscreenMode %s, ApplyResolutionSettings %s, ApplySettings %s",
        cn, resX, resY, mode, mode >= 0 && mode < 3 ? kMode[mode] : "?",
        FindFunc(gus, "SetScreenResolution") ? "yes" : "no", FindFunc(gus, "SetFullscreenMode") ? "yes" : "no",
        FindFunc(gus, "ApplyResolutionSettings") ? "yes" : "no", FindFunc(gus, "ApplySettings") ? "yes" : "no");
}
// (the author: "Let's do it for posterities sake"): which graphics APIs / overlays are loaded in the game - only
// asked if intercepting the picture ever becomes a topic (multi-monitor fallback). File name + "Windows" / "game folder" /
// "elsewhere" only (no paths in the log). d3d11 / dxgi can be loaded by a DX12 game too (helpers) - d3d12 is the telling one.
static void MonitorGraphicsLog()
{
    static const wchar_t* const kMods[] = {L"d3d12.dll", L"d3d11.dll", L"dxgi.dll", L"d3d9.dll", L"vulkan-1.dll", L"opengl32.dll",
        L"D3D12Core.dll", L"nvapi64.dll", L"amdxc64.dll", L"GameOverlayRenderer64.dll", L"DiscordHook64.dll", L"RTSSHooks64.dll",
        L"ReShade64.dll", L"dinput8.dll"};
    wchar_t gameDir[MAX_PATH] = L""; GetModuleFileNameW(nullptr, gameDir, MAX_PATH);
    if (wchar_t* sl = wcsrchr(gameDir, L'\\')) sl[1] = 0;
    wchar_t winDir[MAX_PATH] = L""; GetWindowsDirectoryW(winDir, MAX_PATH);
    char line[900]; int o = snprintf(line, sizeof(line), "MONITORS: graphics modules loaded:");
    int found = 0;
    for (const wchar_t* m : kMods)
    {
        HMODULE h = GetModuleHandleW(m);
        if (!h) continue;
        wchar_t path[MAX_PATH] = L""; GetModuleFileNameW(h, path, MAX_PATH);
        const char* where = !_wcsnicmp(path, winDir, wcslen(winDir)) ? "Windows" : (gameDir[0] && !_wcsnicmp(path, gameDir, wcslen(gameDir))) ? "game folder" : "elsewhere";
        if (o < (int)sizeof(line) - 80) o += snprintf(line + o, sizeof(line) - o, " %ls (%s);", m, where);
        ++found;
    }
    if (!found) snprintf(line + o, sizeof(line) - o, " none of the known ones");
    CoopLog("%s", line);
    CoopLog("MONITORS: renderer = %s", GetModuleHandleW(L"d3d12.dll") ? "DirectX 12" : GetModuleHandleW(L"vulkan-1.dll") ? "Vulkan (?)" :
        GetModuleHandleW(L"d3d11.dll") ? "DirectX 11" : "unknown");
}
// f1.285 - f1.289: the multi-monitor SPAN research (the author 2026-11-05: "This cannot be post release, we need this feature
// to be working"). Found: the mod can stretch the game window over the monitors (borderless + SetWindowPos); the game renders
// at ITS resolution setting (r.SetRes WxHw sets it - f1.289); the camera keeps 16:9 unless its bConstrainAspectRatio is
// cleared; its FOV is horizontal (widened for the wide picture); P2 / P3 need window-shaped pictures (30 PipWindowShaped).
// f1.290: the switching between one monitor and all of them lives in pip_hud/31b.
static bool g_SpanWide = false;   // the game's drawing area is currently wider than ~2.2:1 (camera flag cleared)
static void SpanApplyWindow(HWND h, const RECT& r, const char* why)
{
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE), ns = (st & ~(LONG_PTR)(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
    if (ns != st) SetWindowLongPtrW(h, GWL_STYLE, ns);
    BOOL ok = SetWindowPos(h, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    CoopLog("MONITORS: %s - window style %llX -> %llX, moved to %ld,%ld %ldx%ld (%s)", why, (unsigned long long)st, (unsigned long long)ns,
        r.left, r.top, r.right - r.left, r.bottom - r.top, ok ? "SetWindowPos OK" : "SetWindowPos FAILED");
}
static void SpanState(HWND h, const char* when, int* cwOut)
{
    RECT wr{}, cr{}; GetWindowRect(h, &wr); GetClientRect(h, &cr);
    if (cwOut) *cwOut = cr.right;
    CoopLog("SPAN TEST: %s - window %ld,%ld %ldx%ld, drawing area %ldx%ld, canvas %.0fx%.0f, style %llX", when, wr.left, wr.top, wr.right - wr.left,
        wr.bottom - wr.top, cr.right, cr.bottom, g_PipCanvasW, g_PipCanvasH, (unsigned long long)GetWindowLongPtrW(h, GWL_STYLE));
}
// f1.289: the game renders at ITS resolution setting, not the window's (f1.288 09:53: a 3840x1080 window with the setting at
// 1920x1080 = only the left 1920 pixels had a picture, the rest streaks; with the setting at 3840x2160 (09:58) - the whole
// window right). GameUserSettings still said 1920x1080 then - the game keeps it elsewhere. So the engine's own resolution
// command, r.SetRes WxHw (windowed), through KismetSystemLibrary::ExecuteConsoleCommand - before the window is stretched.
static bool SpanConsole(const wchar_t* cmdIn)   // f1.299: any console command (KismetSystemLibrary::ExecuteConsoleCommand)
{
    void* cdo = FindObj("Default__KismetSystemLibrary", "KismetSystemLibrary");
    void* f = cdo ? FindFunc(cdo, "ExecuteConsoleCommand") : nullptr;
    int oW = f ? ParamOff(f, "WorldContextObject") : -1, oC = f ? ParamOff(f, "Command") : -1, oP = f ? ParamOff(f, "SpecificPlayer") : -1;
    if (!f || oW < 0 || oC < 0 || ParmsSize(f) > 64 || !g_P1Pc) { CoopLog("MONITORS: ExecuteConsoleCommand NOT found - \"%ls\" not sent", cmdIn); return false; }
    static wchar_t cmd[96];
    wcsncpy(cmd, cmdIn, 95); cmd[95] = 0;
    FStringRaw fs{cmd, (int)wcslen(cmd) + 1, (int)wcslen(cmd) + 1};
    alignas(16) unsigned char b[64] = {};
    *(void**)(b + oW) = g_P1Pc; memcpy(b + oC, &fs, sizeof(fs)); if (oP >= 0) *(void**)(b + oP) = g_P1Pc;
    Orig_ProcessEvent(cdo, f, b);
    CoopLog("MONITORS: console \"%ls\" sent", cmd);
    return true;
}
static bool SpanSetRes(int w, int h)
{
    wchar_t c[64]; swprintf(c, 64, L"r.SetRes %dx%dw", w, h);
    return SpanConsole(c);
}
// f1.299 - THE HUD SIZE (found by RE, 2026-11-05): the game's UI "screen" rect (0x1508810) is (0,0,1920,1080) - or
// (0,0,3840,2160) when the resolution width (0x53E3AE0) is over 1920 - unless the console variables r.InGameUI.FixedWidth /
// r.InGameUI.FixedHeight are set (> 0), then (0,0,W,H). Its HUD root (SEndRootOverlay, 0x1509D47 / 0x150A4A8) scales by
// that height / 1080 - over two 1080p monitors the 4K branch = the 2x HUD. While spanning they are set to player 1's
// monitor (the HUD box = that size at the top left); 0 (the game's own choice) on one monitor.
static void SpanUiFixed(int w, int h)
{
    wchar_t c[64];
    swprintf(c, 64, L"r.InGameUI.FixedWidth %d", w); SpanConsole(c);
    swprintf(c, 64, L"r.InGameUI.FixedHeight %d", h); SpanConsole(c);
}
static void MonitorSpanPov(unsigned char* pcm)
{
    if (!g_Cfg.monSpanTest || !pcm || !Readable(pcm + 0x400, 0x30)) return;
    static int cOff = -2; static unsigned char cMask = 0;
    if (cOff == -2)
    {
        cOff = -1;
        void* st = FindObj("MinimalViewInfo", "ScriptStruct");
        int o = -1; unsigned char mk = 0;
        if (st && PpBool(st, "bConstrainAspectRatio", &o, &mk) && o > 0x18 && o < 0x100) { cOff = o; cMask = mk; }
        CoopLog("SPAN TEST: camera picture - aspect ratio at +0x28, \"keep the aspect ratio\" flag %s (+0x%X mask %02X)", cOff >= 0 ? "found" : "NOT found", cOff, cMask);
    }
    float aspect = *(float*)(pcm + 0x428);
    bool constrain = cOff >= 0 && (pcm[0x400 + cOff] & cMask);
    // f1.286: judged by the WINDOW's drawing area, not the canvas - with the flag on the game draws a 16:9 picture in the
    // middle (f1.285 test 09:35: window 3840x1080, canvas 1920x1080, black bars both sides), so the canvas never got wide.
    g_SpanWide = g_GameClientAspect > 2.2f;
    static float lastA = -1.f; static int lastC = -1, lastW = -1, logs = 0;
    static float lastFov = 0.f;
    if ((fabsf(aspect - lastA) > 0.01f || (int)constrain != lastC || (int)g_SpanWide != lastW) && logs < 40)
    {
        ++logs; lastA = aspect; lastC = constrain; lastW = g_SpanWide;
        CoopLog("SPAN TEST: camera aspect %.3f, keep-aspect flag %d, canvas %.0fx%.0f, window %.3f:1%s", aspect, (int)constrain, g_PipCanvasW, g_PipCanvasH, g_GameClientAspect,
            g_SpanWide && constrain ? " - flag cleared (wide window)" : "");
    }
    if (g_SpanWide && constrain) { pcm[0x400 + cOff] &= (unsigned char)~cMask; *(float*)(pcm + 0x428) = g_GameClientAspect; }
    if (g_SpanWide)   // f1.289: also with the split screen up (f1.288 09:59: player 1's half was too zoomed in - the lens shows player 1's
                      // share of the whole picture's FOV, so the whole picture needs the widened one: 98 deg -> 60 deg across player 1's monitor)
    {   // f1.287: the game's FOV is horizontal (the f1.286 picture looked zoomed in across 32:9) - widen it so the UP-DOWN view
        // is the normal 16:9 one: tan(new / 2) = tan(fov / 2) x (window aspect / 16:9)
        float* fv = (float*)(pcm + 0x418);
        if (*fv > 5.f && *fv < 150.f)
        {
            float k = g_GameClientAspect / (16.f / 9.f), nf = 2.f * atanf(tanf(*fv * 0.5f * DEG_TO_RAD) * k) * RAD_TO_DEG;
            static int fl = 0; if (fl < 10 && fabsf(nf - lastFov) > 3.f) { ++fl; lastFov = nf; CoopLog("SPAN TEST: camera FOV %.1f -> %.1f deg (x%.2f wider picture)", *fv, nf, k); }
            *fv = nf;
        }
    }
}
// main tick, every 5 s: log the monitors / the game window when they change (step 1: research only)
static void MonitorTick()
{
    static ULONGLONG at = 0; ULONGLONG now = GetTickCount64();
    if (at && now - at < 5000) return;
    at = now;
    MonRect m[8]; int n = MonitorsNow(m);
    RECT wr{}, cr{}; POINT tl{0, 0}; LONG_PTR style = 0;
    HWND h = g_GameHwnd;
    if (h) { GetWindowRect(h, &wr); GetClientRect(h, &cr); ClientToScreen(h, &tl); style = GetWindowLongPtrW(h, GWL_STYLE); }
    g_GameClientAspect = (h && cr.bottom > 0) ? (float)cr.right / (float)cr.bottom : 0.f;
    char sig[400]; int o = snprintf(sig, sizeof(sig), "%d|%ld,%ld,%ld,%ld|%ld,%ld|%lld|%.0f,%.0f", n, wr.left, wr.top, wr.right, wr.bottom, cr.right, cr.bottom, (long long)style, g_PipCanvasW, g_PipCanvasH);
    for (int i = 0; i < n && o < (int)sizeof(sig) - 40; ++i) o += snprintf(sig + o, sizeof(sig) - o, "|%d,%d,%d,%d,%d", m[i].x, m[i].y, m[i].w, m[i].h, (int)m[i].primary);
    static char last[400] = ""; static int logs = 0;
    if (!strcmp(sig, last) || logs >= (g_Cfg.monSpanTest ? 60 : 20)) return;
    strcpy(last, sig); ++logs;
    char line[600]; int lo = snprintf(line, sizeof(line), "MONITORS: %d monitor(s):", n);
    for (int i = 0; i < n && lo < (int)sizeof(line) - 60; ++i)
        lo += snprintf(line + lo, sizeof(line) - lo, " #%d %d,%d %dx%d%s;", i + 1, m[i].x, m[i].y, m[i].w, m[i].h, m[i].primary ? " (Windows main)" : "");
    CoopLog("%s", line);
    if (h)
    {
        int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
        CoopLog("MONITORS: game window %ld,%ld %ldx%ld, drawing area %ld,%ld %dx%d (%s), canvas %.0fx%.0f",
            wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, tl.x, tl.y, cw, ch,
            (style & WS_CAPTION) ? "with title bar" : "borderless", g_PipCanvasW, g_PipCanvasH);
        float reg[3][4];
        int used = MonitorRegions(m, n, tl.x, tl.y, cw, ch, g_PipCanvasW, g_PipCanvasH, 0, true, true, reg);
        if (used >= 2)
            CoopLog("MONITORS: the game window covers %d monitors - the monitors layout would give player 1 %.0f,%.0f %.0fx%.0f, player 2 %.0f,%.0f %.0fx%.0f, player 3 %.0f,%.0f %.0fx%.0f (canvas pixels)",
                used, reg[0][0], reg[0][1], reg[0][2], reg[0][3], reg[1][0], reg[1][1], reg[1][2], reg[1][3], reg[2][0], reg[2][1], reg[2][2], reg[2][3]);
        else CoopLog("MONITORS: the game window covers %d monitor(s) - no monitors layout possible as it is", used);
    }
    static bool gsLogged = false;
    if (!gsLogged && h) { gsLogged = true; MonitorGameSettingsLog(); MonitorGraphicsLog(); }
}
#endif
