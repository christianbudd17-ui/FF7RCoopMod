// ---- v9.0: HUD copy - now only its widget helpers ----
// (v9.0 - f1.55 drew a copy of the game's HUD over player 2's window; since f1.56 the game draws its own HUD into the
// windows, and f1.75 removed the drawing half.) What is left: the search for the game's shown root widgets (the main
// menu check, the reticle / menus), HcKindOf (widget kind by class), HcResolve (the reflection functions) and HcRect
// (a widget's screen rect) - used by menus/32, menus/35 and pip_hud/36.
static void* g_HcHud = nullptr;              // the AHUD this frame (world context)
struct HcFns
{
    int state = 0;                           // 0 = not resolved, 1 = ready, -1 = unavailable
    void* uwCls = nullptr;
    void* sbl = nullptr; void* fL2V = nullptr; int vW = -1, vG = -1, vL = -1, vP = -1;
    void* fGeo = nullptr; int gR = -1;
};
static HcFns g_Hc;
// (Nothing here draws a HUD copy any more - the game draws its own HUD into the windows. What this file still does - the
//  root-widget search, HcKindOf, HcResolve, HcRect - is how menus/32, menus/35 and pip_hud/36 find and move the game's widgets.)

// GetAllWidgetsOfClass(TopLevelOnly) returned nothing in the v10.0 test (the game doesn't put its
// HUD in the viewport the standard way), so the mod finds the root widgets itself: every live UserWidget
// whose outer isn't another widget's WidgetTree, that isn't Collapsed/Hidden and that has a live Slate
// widget (IsVisible). Rescanned at most once a second; kept as weak references.
static int g_RwIdx[96], g_RwSer[96], g_RwN = 0;          // shown root widgets (published)
static bool g_MainMenuUp = false;                          // main menu / save screen shown - windows hidden
static bool g_LoadingUp = false;                           // f1.303: the game's in-game loading screen (Com_Loading_*) shown - multiple monitors: one monitor
static void* g_RwVisFn = nullptr; static int g_RwVisRet = -1, g_RwVisState = 0;
// candidates (all root user widgets, shown or not) found by a search spread over several calls
static int g_RcIdx[256], g_RcSer[256], g_RcN = 0;          // published candidates
static int g_RnIdx[256], g_RnSer[256], g_RnN = 0;          // being collected
static int g_RwPos = 0; static LONGLONG g_RwPassTicks = 0; static int g_RwPassSteps = 0;
static void* g_RwHashK[8192]; static unsigned char g_RwHashV[8192];   // class -> 1 user widget / 2 not
// f1.337 (pre-release review): the class caches below are keyed by the class's address only - after an area's widget class is
// unloaded, another class can get that address and its objects would pass as widgets (IsVisible / the message log's rect then
// run on a non-widget). Before such a call the object's class chain is walked again (cheap: a few hundred objects per scan).
static bool ClassChainHas(void* cls, void* want)
{
    if (!want) return false;
    int n = 0;
    for (void* c = cls; c && n < 64; c = *(void**)((unsigned char*)c + 0x30), ++n) if (c == want) return true;
    return false;
}
static bool RwIsUserWidget(void* cls)
{
    unsigned h = (unsigned)(((uintptr_t)cls >> 4) * 2654435761u) & 8191;
    for (int probe = 0; probe < 64; ++probe, h = (h + 1) & 8191)
    {
        if (g_RwHashK[h] == cls) return g_RwHashV[h] == 1;
        if (!g_RwHashK[h])
        {
            bool yes = false;
            for (void* c = cls; c; c = *(void**)((unsigned char*)c + 0x30)) if (c == g_Hc.uwCls) { yes = true; break; }
            g_RwHashK[h] = cls; g_RwHashV[h] = yes ? 1 : 2;
            return yes;
        }
    }
    bool yes = false;   // table crowded here - just walk
    for (void* c = cls; c; c = *(void**)((unsigned char*)c + 0x30)) if (c == g_Hc.uwCls) { yes = true; break; }
    return yes;
}
// the game's message log widget (UEndMessageLogs - the battle messages "... defeated", Gil / EXP / items; the cells
// MessageLogs_Cell_C are laid out inside it). Collected by the same spread-out walk as the root widgets (a class pointer
// compare per object; a subclass is recognised through its super chain, cached per class). Placed by pip_hud/36c.
static void* g_MlCls = nullptr; static int g_MlClsState = 0;
static int g_MlIdx[32], g_MlSer[32], g_MlN = 0;       // published (live, not a template)
static int g_MlNIdx[32], g_MlNSer[32], g_MlNN = 0;    // being collected
static void* g_MlHashK[1024]; static unsigned char g_MlHashV[1024];
static bool MlIsLogClass(void* cls)
{
    if (!g_MlCls || !cls) return false;
    if (cls == g_MlCls) return true;
    unsigned h = (unsigned)(((uintptr_t)cls >> 4) * 2654435761u) & 1023;
    for (int probe = 0; probe < 32; ++probe, h = (h + 1) & 1023)
    {
        if (g_MlHashK[h] == cls) return g_MlHashV[h] == 1;
        if (!g_MlHashK[h])
        {
            bool yes = false; int n = 0;
            for (void* c = cls; c && n < 64; c = *(void**)((unsigned char*)c + 0x30), ++n) if (c == g_MlCls) { yes = true; break; }
            g_MlHashK[h] = cls; g_MlHashV[h] = yes ? 1 : 2;
            return yes;
        }
    }
    return false;   // table crowded here - not a log (it is only a handful of classes in practice)
}
static void RootWidgetScan()
{
    if (g_MlClsState == 0)
    {
        g_MlClsState = 1; g_MlCls = FindObj("EndMessageLogs", "Class");
        CoopLog("MSGLOG: the game's message log class %s", g_MlCls ? "found" : "NOT found - the battle messages stay where the game puts them");
    }
    if (!g_Hc.uwCls) g_Hc.uwCls = FindObj("UserWidget", "Class");
    if (!g_Hc.uwCls) return;
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    if (!items || num <= 0) return;
    LARGE_INTEGER t0, t1, fq; QueryPerformanceCounter(&t0);
    // 1) a slice of the object list (40000 objects per call)
    int stop = g_RwPos + 40000; if (stop > num) stop = num;
    for (int i = g_RwPos; i < stop; ++i)
    {
        unsigned char* it = items + (size_t)i * 24;
        unsigned char* o = *(unsigned char**)it;
        if (!o || (*(DWORD*)(it + 8) & 0x30000000)) continue;   // pending kill / unreachable
        void* cls = *(void**)(o + 0x10);
        if (cls && MlIsLogClass(cls) && !(*(DWORD*)(o + 8) & 0x30) && g_MlNN < 32 && MakeWeak(o, &g_MlNIdx[g_MlNN], &g_MlNSer[g_MlNN])) ++g_MlNN;
        if (!cls || !RwIsUserWidget(cls)) continue;
        if (*(DWORD*)(o + 8) & 0x30) continue;                   // class default objects / archetypes
        void* outer = *(void**)(o + 0x20);
        if (outer) { void* oc = *(void**)((unsigned char*)outer + 0x10); if (oc && ClassIs(oc, "WidgetTree")) continue; }
        if (g_RnN < 256 && MakeWeak(o, &g_RnIdx[g_RnN], &g_RnSer[g_RnN])) ++g_RnN;
    }
    g_RwPos = stop;
    bool passDone = g_RwPos >= num;
    if (passDone)
    {
        memcpy(g_RcIdx, g_RnIdx, sizeof(int) * g_RnN); memcpy(g_RcSer, g_RnSer, sizeof(int) * g_RnN); g_RcN = g_RnN;
        g_RnN = 0; g_RwPos = 0;
        memcpy(g_MlIdx, g_MlNIdx, sizeof(int) * g_MlNN); memcpy(g_MlSer, g_MlNSer, sizeof(int) * g_MlNN); g_MlN = g_MlNN; g_MlNN = 0;
    }
    // 2) which candidates are shown right now (cheap: a few dozen widgets)
    int n = 0;
    for (int k = 0; k < g_RcN && n < 96; ++k)
    {
        unsigned char* o = (unsigned char*)ResolveWeak(g_RcIdx[k], g_RcSer[k]);
        if (!o) continue;
        if (!ClassChainHas(*(void**)(o + 0x10), g_Hc.uwCls)) continue;   // f1.337: still a UserWidget (the class cache can be stale)
        unsigned char vis = o[0xB7]; if (vis == 1 || vis == 2) continue;   // Collapsed / Hidden
        if (g_RwVisState == 0)
        {
            g_RwVisState = -1;
            g_RwVisFn = FindFunc(o, "IsVisible");
            if (g_RwVisFn) g_RwVisRet = ParamOff(g_RwVisFn, "ReturnValue");
            if (g_RwVisFn && g_RwVisRet >= 0 && ParmsSize(g_RwVisFn) <= 16) g_RwVisState = 1;
            CoopLog("UI: widget IsVisible %s", g_RwVisState == 1 ? "found" : "NOT found (Visibility only)");
        }
        if (g_RwVisState == 1)
        {
            alignas(16) unsigned char b[32] = {};
            Orig_ProcessEvent(o, g_RwVisFn, b);
            if (!b[g_RwVisRet]) continue;
        }
        g_RwIdx[n] = g_RcIdx[k]; g_RwSer[n] = g_RcSer[k]; ++n;
    }
    g_RwN = n;
    {   // is the main menu (or the save/load screen) up?
        bool mm = false;
        for (int i = 0; i < n && !mm; ++i)
        {
            void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]); if (!w) continue;
            char nm[96]; ObjClassName(w, nm, sizeof(nm));
            if (!strncmp(nm, "MainMenu_", 9) || !strncmp(nm, "SaveData", 8)) mm = true;
        }
        if (mm != g_MainMenuUp) { g_MainMenuUp = mm; CoopLog("UI: main menu %s - windows %s", mm ? "open" : "closed", mm ? "hidden" : "back"); }
    }
    {   // f1.303 (the author, f1.302 13:23 screenshot: "Kind of a weird bug in the loading screen" - over all monitors the
        // loading screen is HUD now, so it sits on player 1's monitor and the other monitor kept the old picture): is the
        // in-game loading screen up? (Com_Loading_Simple_C etc.; Loca_Loading_C - the location loading screen - stays
        // "visible" for ~40 s into play, so it is only logged)
        bool ld = false;
        for (int i = 0; i < n && !ld; ++i)
        {
            void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]); if (!w) continue;
            char nm[96]; ObjClassName(w, nm, sizeof(nm));
            if (!strncmp(nm, "Com_Loading_", 12)) ld = true;
        }
        if (ld != g_LoadingUp) { g_LoadingUp = ld; CoopLog("UI: loading screen %s", ld ? "shown" : "gone"); }
    }
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
    g_RwPassTicks += t1.QuadPart - t0.QuadPart; ++g_RwPassSteps;
    if (passDone)
    {
        static int lastN = -1; static int logs = 0;
        double total = (double)g_RwPassTicks * 1000.0 / (double)fq.QuadPart;
        if ((n != lastN && logs < 40) || logs < 3)
        {
            lastN = n; ++logs;
            char names[900] = ""; int pos = 0;
            for (int i = 0; i < n && pos < 850; ++i) { void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]); char nm[96] = "?"; if (w) NameOf(*(void**)((unsigned char*)w + 0x10), nm, sizeof(nm)); pos += snprintf(names + pos, sizeof(names) - pos, "%s%s", i ? ", " : "", nm); }
            CoopLog("UI: own search - %d root widget(s) shown (of %d, %d objects, %.1f ms over %d steps, max %.1f ms each): %s", n, g_RcN, num,
                total, g_RwPassSteps, g_RwPassSteps ? total / g_RwPassSteps : 0.0, names);
        }
        g_RwPassTicks = 0; g_RwPassSteps = 0;
    }
}

enum HcKind { HK_UNKNOWN = 0, HK_USER, HK_PANEL, HK_SWITCH, HK_BORDER, HK_EBORDER, HK_IMG, HK_EIMG, HK_ESET, HK_PB, HK_EPB,
              HK_EGAUGE, HK_ETEXT, HK_TEXT, HK_NUM, HK_TILED, HK_MAP, HK_SKIP };
struct HcClassKind { void* cls; int kind; bool skip; };
static HcClassKind g_HcKinds[512]; static int g_HcKindN = 0;
static bool HcSkipName(const char* nm)
{
    // [PictureInPicture] HudCopySkip: widget classes NOT copied (world-anchored things drawn for player 1's camera etc.)
    const wchar_t* list = g_Cfg.hcSkip;
    char item[64]; int n = 0;
    for (const wchar_t* p = list; ; ++p)
    {
        if (*p == L';' || *p == L',' || *p == 0)
        {
            item[n] = 0;
            while (n > 0 && item[n - 1] == ' ') item[--n] = 0;
            const char* it = item; while (*it == ' ') ++it;
            if (*it && strstr(nm, it)) return true;
            n = 0;
            if (*p == 0) break;
        }
        else if (n < 63) item[n++] = (char)*p;
    }
    return false;
}
static int HcKindOf(void* obj, bool* skip)
{
    void* cls = *(void**)((unsigned char*)obj + 0x10);
    for (int i = 0; i < g_HcKindN; ++i) if (g_HcKinds[i].cls == cls) { *skip = g_HcKinds[i].skip; return g_HcKinds[i].kind; }
    int kind = HK_UNKNOWN;
    char own[96] = "?"; NameOf(cls, own, sizeof(own));
    bool sk = HcSkipName(own);
    static const struct { const char* n; int k; } T[] = {
        {"EndTextBlock", HK_ETEXT}, {"TextBlock", HK_TEXT}, {"EndImageSet", HK_ESET}, {"EndImage", HK_EIMG}, {"Image", HK_IMG},
        {"EndProgressBar", HK_EPB}, {"ProgressBar", HK_PB}, {"EndGauge", HK_EGAUGE}, {"EndNumberLabel", HK_NUM},
        {"EndTiledImage", HK_TILED}, {"EndBorder", HK_EBORDER}, {"Border", HK_BORDER}, {"WidgetSwitcher", HK_SWITCH},
        {"UserWidget", HK_USER}, {"PanelWidget", HK_PANEL},
        {"EndCircle", HK_SKIP}, {"EndPolygon", HK_SKIP}, {"EndPieImage", HK_SKIP}, {"EndNaviMap", HK_SKIP}, {"EndNaviMapOctagon", HK_MAP},
        {"EndBillboard", HK_SKIP}, {"EndMessageLogs", HK_SKIP}, {"EndListBox", HK_SKIP}, {"EndStringListBox", HK_SKIP},
        {"EndStringScrollBox", HK_SKIP}, {"EndVirtualScrollBox", HK_SKIP}, {"Widget", HK_SKIP} };
    for (void* c = cls; c && kind == HK_UNKNOWN; c = *(void**)((unsigned char*)c + 0x30))
    {
        char nm[96]; NameOf(c, nm, sizeof(nm));
        for (auto& t : T) if (strcmp(nm, t.n) == 0) { kind = t.k; break; }
    }
    if (kind == HK_SKIP && strcmp(own, "Widget") != 0)
    {   // a type the copy can't draw - say so once
        static int n = 0; if (n < 30) { ++n; CoopLog("HUDCOPY: widget type %s isn't copied (not supported yet)", own); }
    }
    if (g_HcKindN < 512) g_HcKinds[g_HcKindN++] = {cls, kind, sk};
    *skip = sk;
    return kind;
}

static bool HcResolve(void* canvas)
{
    g_Hc.state = -1;
    g_Hc.sbl = FindObj("Default__SlateBlueprintLibrary", "SlateBlueprintLibrary");
    g_Hc.uwCls = FindObj("UserWidget", "Class");
    if (g_Hc.sbl) { g_Hc.fL2V = FindFunc(g_Hc.sbl, "LocalToViewport"); if (g_Hc.fL2V) { g_Hc.vW = ParamOff(g_Hc.fL2V, "WorldContextObject"); g_Hc.vG = ParamOff(g_Hc.fL2V, "Geometry"); g_Hc.vL = ParamOff(g_Hc.fL2V, "LocalCoordinate"); g_Hc.vP = ParamOff(g_Hc.fL2V, "PixelPosition"); } }
    if (g_Hc.uwCls)
    {   // UWidget::GetCachedGeometry (found through the UserWidget class chain)
        if (!g_Hc.fGeo) g_Hc.fGeo = FindField(g_Hc.uwCls, "GetCachedGeometry");
        if (g_Hc.fGeo) g_Hc.gR = ParamOff(g_Hc.fGeo, "ReturnValue");
    }
    CoopLog("HUDCOPY: LocalToViewport %p (W%d G%d L%d P%d) GetCachedGeometry %p (R%d size %d) UserWidget class %p",
        g_Hc.fL2V, g_Hc.vW, g_Hc.vG, g_Hc.vL, g_Hc.vP, g_Hc.fGeo, g_Hc.gR, g_Hc.fGeo ? ParmsSize(g_Hc.fGeo) : 0, g_Hc.uwCls);
    // (BUG-1): no longer needs the split-screen window's draw function (g_Pip.fDraw, only set once a window has been
    // drawn) - with split screen off in the first battle (the release default) this failed for good: no own menus along the
    // bottom, no menus in the windows, no enemy names and no reticle hiding for the rest of the session
    bool ok = g_Hc.fL2V && g_Hc.vW >= 0 && g_Hc.vG >= 0 && g_Hc.vL >= 0 && g_Hc.vP >= 0 && ParmsSize(g_Hc.fL2V) <= 256 &&
              g_Hc.fGeo && g_Hc.gR >= 0 && ParmsSize(g_Hc.fGeo) <= 128 && g_Hc.uwCls;
    if (ok) g_Hc.state = 1; else CoopLog("HUDCOPY: widget helpers unavailable - menus in the windows, the enemy names and the reticle hiding may not work");
    return ok;
}

// ---- drawing state for one frame ----
struct HcClip { float x1, y1, x2, y2; };
struct HcFrame
{
    void* canvas; HcClip win;            // the window (nothing is drawn outside it)
    float offX, offY; bool offOk;        // desktop -> viewport pixels
    int leaves, drawn, quads, visits;
    float dx, dy;                        // moves what's drawn (0 since f1.56 - was for the HUD copy's party rows)
};
static HcFrame g_HcF;
// (the v18.9 MKDBG walk log and the v19.1 per-image transforms were only for the marker copy - removed)

// widget geometry -> screen rect (viewport pixels) and pixels per Slate unit
static bool HcRect(void* w, float* x, float* y, float* rw, float* rh, float* scale)
{
    alignas(16) unsigned char b[128] = {};
    Orig_ProcessEvent(w, g_Hc.fGeo, b);
    const float* g = (const float*)(b + g_Hc.gR);
    float sx = g[0], sy = g[1];                         // local size
    const float* M = g + 7; const float* T = g + 11;    // AccumulatedRenderTransform: 2x2 matrix + translation
    float ax, ay, bx, by;
    if (M[0] != 0.f || M[3] != 0.f)
    {
        ax = T[0]; ay = T[1];
        bx = T[0] + sx * M[0] + sy * M[2]; by = T[1] + sx * M[1] + sy * M[3];
        *scale = sqrtf(M[0] * M[0] + M[1] * M[1]);
    }
    else
    {   // no render transform: AbsolutePosition + Size * Scale
        ax = g[3]; ay = g[4]; bx = ax + sx * g[2]; by = ay + sy * g[2]; *scale = g[2];
    }
    if (!(sx > 0.f && sy > 0.f) || !(*scale > 0.f) || *scale > 50.f) return false;
    if (!g_HcF.offOk)
    {   // desktop -> viewport pixel offset, once per frame
        alignas(16) unsigned char v[256] = {};
        void* wc = g_HcHud;
        memcpy(v + g_Hc.vW, &wc, 8); memcpy(v + g_Hc.vG, g, 0x38);
        Orig_ProcessEvent(g_Hc.sbl, g_Hc.fL2V, v);
        const float* px = (const float*)(v + g_Hc.vP);
        g_HcF.offX = px[0] - ax; g_HcF.offY = px[1] - ay; g_HcF.offOk = true;
        static bool once = false; if (!once) { once = true; CoopLog("HUDCOPY: desktop->viewport offset (%.1f, %.1f), first scale %.3f", g_HcF.offX, g_HcF.offY, *scale); }
    }
    float x1 = (ax < bx ? ax : bx) + g_HcF.offX + g_HcF.dx, y1 = (ay < by ? ay : by) + g_HcF.offY + g_HcF.dy;
    *x = x1; *y = y1; *rw = fabsf(bx - ax); *rh = fabsf(by - ay);
    return true;
}

static void HcResetCaches()   // settings reloaded (skip list, font choice)
{
    g_HcKindN = 0;
}
