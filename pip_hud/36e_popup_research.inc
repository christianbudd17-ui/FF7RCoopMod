// The game's own damage / heal numbers ("popups") for players 2/3 (f1.171 research -> f1.173; the research-only
// logging and the 0x13FD390 watch hook removed - the tidy-up the author approved).
// The author (2026-10-25): "I'd like to use all the game original elements if we can find them - enemy health and stagger
// bars, damage and healing numbers, etc." Exe RE (claude/FF7R_Game_Original_HUD_Plan.md):
//  - EndMenuAPI ShowDamagePopupMessage / ShowPopupMessage (exec 0x1B29060 / 0x1B29890) both end in 0x14936B0
//    ShowPopup(popup manager, world, actor, FVector* world position, kind (EEndMenuPopupMessageKind), FEndMenuDamageInfo*).
//    The popup manager = the menu system (0x13ACA90) +0x80; its popups are a sparse array at +0xB8 (0xC8 bytes each).
//  - 0x1497050 = the popup manager's tick (one caller 0x13AE343). Per popup: world position (0x148DF10) -> screen through
//    the popup's player controller (entry +0x08 -> 0x1496AD0 -> 0x1348940 ProjectWorldToScreen), / the UI scale (screen
//    height / 1080, or width / 1920 below 16:9; 0x13A94A0) + the stacking offset (entry +0x94 / +0x98), then
//    0x13FD390(layer manager, entry +0x48 handle, FVector2D*) places the widget (one caller: 0x1497ED1).
// the damage numbers turned out to be in the manager's +0x18 / +0x68 lists (0xD0 entries), placed by 0x1496AD0
// itself - see below.
typedef uint64_t (*ShowPopup_t)(void* mgr, void* world, void* actor, float* pos, uint64_t kind, void* info, uint64_t a7, uint64_t a8);
static ShowPopup_t Orig_ShowPopup = nullptr;
// 0x1496AD0 PopPlace(FVector2D* out, player controller, TSharedPtr* canvas, TSharedPtr* widget, FVector* world,
// FVector2D offset (by value), bool clamp, bool* on screen) - projects, scales, adds the offset and writes the widget's
// canvas slot (slot +0x20 = left / top / size) itself; the caller then shows / hides the widget from *on screen
typedef float* (*PopPlace_t)(float* out, void* pc, void* canvasSP, void* widgetSP, float* world, uint64_t offset, uint64_t clamp, unsigned char* onScreen);
static PopPlace_t Orig_PopPlace = nullptr;

// the f1.171 run showed every damage number (kinds 1 / 3) lands in NEITHER list f1.171 watched (+0xB8 stayed 0) -
// the manager has three sparse arrays: +0x18 and +0x68 (0xD0 bytes per entry: +0x00 TSharedPtr<SWidget>, +0x10 weak player
// controller, +0x18 weak followed actor, +0x20 weak target, +0x78 world, +0x9C / +0xA0 offset, +0xA4 kind, +0xB0 / +0xB4
// rolling number, +0xBC state) and +0xB8 (0xC8 bytes, f1.171's layout). Sparse array at B: data B, ArrayNum B+8, allocation
// bits inline B+0x10 / heap B+0x20, NumFree B+0x34.
struct PopArr { int base, stride; const char* name; };
static const PopArr kPopArr[3] = {{0x18, 0xD0, "A"}, {0x68, 0xD0, "B"}, {0xB8, 0xC8, "C"}};
static int PopNum(unsigned char* m, const PopArr& a) { return Readable(m + a.base, 0x38) ? *(int*)(m + a.base + 8) : 0; }
static bool PopAlloc(unsigned char* m, const PopArr& a, int i)
{
    unsigned* bits = *(unsigned**)(m + a.base + 0x20); if (!bits) bits = (unsigned*)(m + a.base + 0x10);
    return Readable(bits + (i >> 5), 4) && (bits[i >> 5] >> (i & 31) & 1);
}
static unsigned char* PopEntry(unsigned char* m, const PopArr& a, int i)
{
    unsigned char* d = *(unsigned char**)(m + a.base);
    unsigned char* e = d ? d + (size_t)i * a.stride : nullptr;
    return (e && Readable(e, a.stride)) ? e : nullptr;
}
static void* PopWidgetOf(unsigned char* e, const PopArr& a)   // the 0xD0 lists hold a Slate pointer, the 0xC8 list a weak UObject
{
    return a.stride == 0xD0 ? *(void**)e : ResolveWeak(*(int*)e, *(int*)(e + 4));
}
struct PopSnap { void* w[3][128]; int n[3]; };
static void PopTake(unsigned char* m, PopSnap& sn)
{
    for (int a = 0; a < 3; ++a)
    {
        sn.n[a] = PopNum(m, kPopArr[a]); if (sn.n[a] > 128) sn.n[a] = 128;
        for (int i = 0; i < sn.n[a]; ++i) { unsigned char* e = PopAlloc(m, kPopArr[a], i) ? PopEntry(m, kPopArr[a], i) : nullptr; sn.w[a][i] = e ? PopWidgetOf(e, kPopArr[a]) : nullptr; }
    }
}
static void PopCopies(unsigned char* m, void* world, void* actor, float* pos, uint64_t kind, void* info, uint64_t a7, uint64_t a8);
static uint64_t Hook_ShowPopup(void* mgr, void* world, void* actor, float* pos, uint64_t kind, void* info, uint64_t a7, uint64_t a8)
{
    unsigned char* m = (unsigned char*)mgr;
    bool watch = g_Player->joined && m && GetCurrentThreadId() == g_GameThreadId && Readable(m, 0xF0);
    uint64_t r = Orig_ShowPopup(mgr, world, actor, pos, kind, info, a7, a8);
    if (!watch) return r;
    int k8 = (int)(kind & 0xFF);
    if (k8 >= 1 && k8 <= 7) PopCopies(m, world, actor, pos, kind, info, a7, a8);
    return r;
}
// 0x1496AD0 places the damage numbers itself (canvas slot)
static int PopOursFind(void* w, bool* own);
static float* PopPlaceOurs(int pn, bool own, float* out, void* pc, void* canvasSP, void* widgetSP, float* world, uint64_t offset, unsigned char* onScreen);
static void PopHideOverWindows(const float* out, void* w, unsigned char* onScreen);
static float* Hook_PopPlace(float* out, void* pc, void* canvasSP, void* widgetSP, float* world, uint64_t offset, uint64_t clamp, unsigned char* onScreen)
{
    bool gt = GetCurrentThreadId() == g_GameThreadId;
    if (gt && widgetSP && world && out)
    {   // one of players 2/3's copies - placed with their window's camera
        bool own = false; int pn = PopOursFind(*(void**)widgetSP, &own);
        if (pn) return PopPlaceOurs(pn, own, out, pc, canvasSP, widgetSP, world, offset, onScreen);
    }
    float* r = Orig_PopPlace(out, pc, canvasSP, widgetSP, world, offset, clamp, onScreen);
    if (gt && out && onScreen && *onScreen && widgetSP && AnyWindowShowing())
        PopHideOverWindows(out, *(void**)widgetSP, onScreen);   // player 1's own numbers never over the windows
    return r;
}
// ---- f1.173: players 2/3 get copies of the game's OWN damage / heal numbers ([General] GamePopups=1, default) ----
// The author (2026-10-25): "I'd like to use all the game original elements if we can find them"; "The numbers the game uses
// have some fade to them ... Red at the top and fade down to white." f1.172's log (22:44) proved the layout: every hit =
// a NEW entry in the manager's +0x18 list (no merging - Barret's gatling made A0 ... A11, one per hit, each with its own
// random scatter offset), and the hand projection = the game's position exactly (UI scale 1.000 at 1080p).
//  - Making: after the game's own call (player 1's number), ShowPopup 0x14936B0 is called once more per showing window
//    (player 2 / 3) with the same arguments - the new entries' widgets are remembered as that player's (kinds 1 - 7 only:
//    damage, heal, party damage, MP damage / heal, dodge, enemy dodge).
//  - Placing: 0x1496AD0 projects with player 1's camera. For a copy the mod works out where the character is in that
//    player's window (PipProject, their window camera), turns that screen point back into a world point on player 1's
//    camera ray (GameplayStatics DeprojectScreenToWorld - the exact inverse of the game's projection) and lets the game
//    place the copy from that point - so the game still does the UI scale, the scatter offset, the canvas slot and the
//    show / hide. Outside their window = hidden; a number on THEIR OWN character is kept inside their window's edge
//    instead (the game does that for player 1's own character).
//  - Player 1's numbers: while a window shows, a number whose centre lands over a window is hidden (the game's widgets
//    are drawn on top of the windows - the author: "P1's damage numbers are now bleeding through our new windows").
struct PopOurs { void* w = nullptr; int pn = 0; bool own = false; ULONGLONG t = 0; };
static PopOurs g_PopOurs[128]; static int g_PopOursNext = 0;
static int PopOursFind(void* w, bool* own)
{
    if (!w) return 0;
    ULONGLONG now = GetTickCount64();
    for (auto& o : g_PopOurs) if (o.w == w && o.t && now - o.t < 4000) { *own = o.own; return o.pn; }
    return 0;
}
// that player's window shows with a camera this frame; *ch = their character
static bool PopWinShowing(int pn, void** ch)
{
    if (pn == 2) { WithP2 b; *ch = P2Char(); return g_P2WinOn && g_Player->pipVisible && g_Player->pipCamOk; }
    if (pn == 3 && g_P3Enabled) { WithP3 b; *ch = P2Char(); return g_P3WinOn && g_Player->pipVisible && g_Player->pipCamOk; }
    return false;
}
static void PopCopies(unsigned char* m, void* world, void* actor, float* pos, uint64_t kind, void* info, uint64_t a7, uint64_t a8)
{
    static LogRate logs, nologs;
    for (int pn = 2; pn <= 3; ++pn)
    {
        void* ch = nullptr;
        if (!PopWinShowing(pn, &ch)) continue;
        static PopSnap sb; PopTake(m, sb);
        Orig_ShowPopup(m, world, actor, pos, kind, info, a7, a8);   // the game makes the copy exactly like player 1's
        int made = 0;
        for (int a = 0; a < 2; ++a)   // lists A / B (the 0xD0 entries); list C isn't used by kinds 1 - 7
        {
            const PopArr& A = kPopArr[a]; int na = PopNum(m, A); if (na > 128) na = 128;
            for (int i = 0; i < na; ++i)
            {
                unsigned char* e = PopAlloc(m, A, i) ? PopEntry(m, A, i) : nullptr; if (!e) continue;
                void* w = PopWidgetOf(e, A);
                if (!w || (i < sb.n[a] && sb.w[a][i] == w)) continue;
                PopOurs& o = g_PopOurs[g_PopOursNext]; g_PopOursNext = (g_PopOursNext + 1) % 128;
                o.w = w; o.pn = pn; o.own = actor && actor == ch; o.t = GetTickCount64(); ++made;
                if (LogOk(logs, 8)) { char an[96] = "-"; if (actor) NameOf(actor, an, sizeof(an));
                    CoopLog("GAMEPOPUP: player %d gets a copy of the game's number (kind %d on %s%s) - list %s entry %d", pn, (int)(kind & 0xFF), an,
                            o.own ? ", their own character" : "", A.name, i); }
            }
        }
        if (!made && LogOk(nologs, 5)) { CoopLog("GAMEPOPUP: player %d - the game made no copy (kind %d)", pn, (int)(kind & 0xFF)); }
    }
}
// where that player's window shows the world point (canvas pixels); false = not in their window (hidden)
static bool PopWinPoint(int pn, bool own, const float* world, float S[2])
{
    auto f = [&]() -> bool {
        if (!g_Player->pipVisible || !g_Player->pipCamOk) return false;
        float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f;
        float rx, ry, rw, rh; PipRect(W, H, &rx, &ry, &rw, &rh);
        float o[3]; bool ok = PipProject(world, rx, ry, rw, rh, o);
        float mg = 30.f * H / 1080.f;
        bool inside = ok && o[0] >= rx + mg && o[0] <= rx + rw - mg && o[1] >= ry + mg && o[1] <= ry + rh - mg;
        if (!inside)
        {
            if (!own) return false;
            if (!ok) { o[0] = rx + rw * 0.5f; o[1] = ry + rh - 3.f * mg; }   // behind their camera: bottom middle of the window
            if (o[0] < rx + mg) o[0] = rx + mg;
            if (o[0] > rx + rw - 3.f * mg) o[0] = rx + rw - 3.f * mg;
            if (o[1] < ry + mg) o[1] = ry + mg;
            if (o[1] > ry + rh - 2.f * mg) o[1] = ry + rh - 2.f * mg;
        }
        S[0] = o[0]; S[1] = o[1]; return true;
    };
    if (pn == 3) { WithP3 b; return f(); }
    WithP2 b; return f();
}
// GameplayStatics::DeprojectScreenToWorld(Player, ScreenPosition, out WorldPosition, out WorldDirection)
static void* g_DpGs = nullptr; static void* g_DpFn = nullptr;
static int g_DpP = -1, g_DpS = -1, g_DpW = -1, g_DpD = -1, g_DpR = -1, g_DpState = 0;
static bool PopDeproject(void* pc, const float S[2], float o[3], float d[3])
{
    if (g_DpState == 0)
    {
        g_DpState = -1;
        g_DpGs = g_Pip.gs ? g_Pip.gs : FindObj("Default__GameplayStatics", "GameplayStatics");
        if (g_DpGs) g_DpFn = FindFunc(g_DpGs, "DeprojectScreenToWorld");
        if (g_DpFn)
        {
            g_DpP = ParamOff(g_DpFn, "Player"); g_DpS = ParamOff(g_DpFn, "ScreenPosition"); g_DpW = ParamOff(g_DpFn, "WorldPosition");
            g_DpD = ParamOff(g_DpFn, "WorldDirection"); g_DpR = ParamOff(g_DpFn, "ReturnValue");
            if (g_DpP >= 0 && g_DpS >= 0 && g_DpW >= 0 && g_DpD >= 0 && g_DpR >= 0 && ParmsSize(g_DpFn) <= 128) g_DpState = 1;
        }
        CoopLog("GAMEPOPUP: DeprojectScreenToWorld %s (P%d S%d W%d D%d R%d)", g_DpState == 1 ? "found - players 2/3 get the game's own numbers" :
                "NOT found - players 2/3's copies stay hidden", g_DpP, g_DpS, g_DpW, g_DpD, g_DpR);
    }
    if (g_DpState != 1 || !pc) return false;
    alignas(16) unsigned char b[128] = {};
    memcpy(b + g_DpP, &pc, 8); memcpy(b + g_DpS, S, 8);
    Orig_ProcessEvent(g_DpGs, g_DpFn, b);
    memcpy(o, b + g_DpW, 12); memcpy(d, b + g_DpD, 12);
    return b[g_DpR] != 0;
}
static float* PopPlaceOurs(int pn, bool own, float* out, void* pc, void* canvasSP, void* widgetSP, float* world, uint64_t offset, unsigned char* onScreen)
{
    float S[2], o[3], d[3];
    bool show = PopWinPoint(pn, own, world, S) && PopDeproject(pc, S, o, d);
    float wp[3];
    if (show) { wp[0] = o[0] + d[0] * 300.f; wp[1] = o[1] + d[1] * 300.f; wp[2] = o[2] + d[2] * 300.f; }   // 3 m down player 1's camera ray through S
    float* r = Orig_PopPlace(out, pc, canvasSP, widgetSP, show ? wp : world, offset, 0, onScreen);
    if (!show && onScreen) *onScreen = 0;   // not in their window: hidden
    static LogRate logs;
    if (LogOk(logs, 8)) { CoopLog("GAMEPOPUP: player %d's copy %s (window point %.0f, %.0f -> the game put it at %.1f, %.1f, on screen %d)", pn,
                                    show ? "placed in their window" : "hidden (not in their window)", show ? S[0] : -1.f, show ? S[1] : -1.f,
                                    out ? out[0] : -1.f, out ? out[1] : -1.f, onScreen ? *onScreen : -1); }
    return r;
}
static void PopHideOverWindows(const float* out, void* w, unsigned char* onScreen)
{
    float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f;
    float sc = (W / H >= 1.7777f) ? H / 1080.f : W / 1920.f; if (sc < 0.01f) sc = 0.01f;
    float sw = 0.f, sh = 0.f;   // the widget's size (UI units, +0x2C / +0x30 - what 0x1496AD0 reads for the slot)
    if (w && Readable((unsigned char*)w + 0x2C, 8)) { sw = *(float*)((unsigned char*)w + 0x2C); sh = *(float*)((unsigned char*)w + 0x30); }
    if (!(sw >= 0.f && sw < 1000.f)) sw = 0.f;
    if (!(sh >= 0.f && sh < 1000.f)) sh = 0.f;
    float cx = (out[0] + sw * 0.5f) * sc, cy = (out[1] + sh * 0.5f) * sc;
    bool inP1;
    switch (SplitSide())   // as menus/35b (player 1's reticle): player 1's part of the screen
    {
        case 0: inP1 = cx >= W * 0.5f; break;
        case 1: inP1 = cx <= W * SplitP1Frac(); break;
        case 2: inP1 = cy >= H * 0.5f; break;
        default: inP1 = cy <= H * 0.5f; break;
    }
    if (inP1) return;
    *onScreen = 0;
    static LogRate logs;
    if (LogOk(logs, 6)) { CoopLog("GAMEPOPUP: player 1's number over a window (%.0f, %.0f) - hidden there", cx, cy); }
}
static void InstallPopupResearch()
{
    static const unsigned char exShow[20] = {0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xE0,0xEE,0xFF,0xFF};
    void* t = MakeInlineHook(g_Base + 0x14936B0, (void*)Hook_ShowPopup, 20, exShow, "ShowPopup (GamePopups)");
    if (t) Orig_ShowPopup = (ShowPopup_t)t;
    static const unsigned char exPlace[18] = {0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    void* t3 = MakeInlineHook(g_Base + 0x1496AD0, (void*)Hook_PopPlace, 18, exPlace, "popup placing (GamePopups)");
    if (t3) Orig_PopPlace = (PopPlace_t)t3;
    CoopLog("GAMEPOPUP: hooks %s / %s (players 2/3 get copies of the game's own damage / heal numbers)",
            t ? "installed" : "NOT installed", t3 ? "installed" : "NOT installed");
}
