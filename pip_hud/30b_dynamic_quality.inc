// =================================================================================
// DYNAMIC WINDOW QUALITY (the author 2026-10-23: "a dynamic mode to the graphics settings for P2 and P3? So if the
// frame rate dropped below a certain threshold, it would automatically drop the graphics detail level in those windows ...
// The user should be able to set up what the minimum graphics they want to drop to would be, and what the minimum frame
// rate they want to maintain"; "select which things it would attempt to adjust and in what order").
// [PictureInPicture] DynamicQuality / DynamicTargetFps / DynamicOrder / DynamicLowestDetail / DynamicLowestDrawDistance /
// DynamicLowestSharpness (settings/03). Only while a window shows (not paused, not in a cutscene / scripted shot):
//  - the frame rate, averaged per second: 3 seconds in a row below the target -> one step plainer;
//    10 seconds in a row at or above it (f1.117b; was 15 % above) -> one step back (waits longer each time a step back had to be undone
//    within 20 s: 10 -> 20 -> 40 -> 60 s, back to 10 s after a minute without that).
//  - the steps, in the user's order, from the settings' own values down to the "lowest" ones:
//      Detail       (LODDistanceFactor, live)          1 -> 1.5 -> 2.5 -> 4
//      DrawDistance (MaxViewDistanceOverride, live)    no limit -> 100 m -> 60 m -> 40 m
//      Sharpness    (render target size - the window's picture is made again: a short flicker) 100 -> 90 -> 75 -> 50 %
//      HalfRate     (the capture's tick interval = 2 / target fps: the windows draw about every other frame, live)
//  - each change is logged (DYNQ lines). F6 with new settings starts again from the top.
// =================================================================================
struct DynStep { int lever; float v; };   // lever 1 Detail, 2 DrawDistance, 3 Sharpness, 4 HalfRate
static DynStep g_DynSteps[16]; static int g_DynN = 0, g_DynLevel = 0; static double g_DynFpsNow = 0.0;   // the last second's frame rate
static void DynBuild()
{
    g_DynN = 0;
    static const float det[4] = {1.f, 1.5f, 2.5f, 4.f};
    static const float dist[3] = {10000.f, 6000.f, 4000.f};
    static const int sharp[4] = {100, 90, 75, 50};
    for (int o = 0; o < 4; ++o)
    {
        int lv = g_Cfg.pipDynOrder[o];
        if (lv == 1) { for (float d : det) if (d > g_Cfg.pipDetail + 0.01f && d <= g_Cfg.pipDynMaxDetail + 0.01f && g_DynN < 16) g_DynSteps[g_DynN++] = {1, d}; }
        else if (lv == 2) { float base = g_Cfg.pipViewDist > 0.f ? g_Cfg.pipViewDist : 1e12f;
            for (float d : dist) if (d < base - 1.f && d >= g_Cfg.pipDynMinDist - 1.f && g_DynN < 16) g_DynSteps[g_DynN++] = {2, d}; }
        else if (lv == 3) { for (int q : sharp) if (q < g_Cfg.pipFullPct && q >= g_Cfg.pipDynMinSharp && g_DynN < 16) g_DynSteps[g_DynN++] = {3, (float)q}; }
        else if (lv == 4) { if (g_DynN < 16) g_DynSteps[g_DynN++] = {4, 1.f}; }
    }
}
static const char* DynLeverName(int lv) { return lv == 1 ? "detail" : lv == 2 ? "draw distance" : lv == 3 ? "sharpness" : lv == 4 ? "half-rate windows" : "?"; }
// the capture's tick interval (half-rate windows) - also called by PipSpawn for a new window camera
static void DynApplyInterval(void* comp)
{
    static void* fn = nullptr; static int oI = -1, st = 0;
    if (!comp) return;
    if (st == 0)
    {
        st = -1; fn = FindFunc(comp, "SetComponentTickInterval");
        if (fn) oI = ParamOff(fn, "TickInterval");
        if (fn && oI >= 0 && ParmsSize(fn) <= 16) st = 1;
        CoopLog("DYNQ: the window cameras' SetComponentTickInterval %s", st == 1 ? "found" : "NOT available - no half-rate windows");
    }
    if (st != 1) return;
    double ref = g_DynFpsNow > 20.0 ? g_DynFpsNow : (g_Cfg.pipDynFps > 20.f ? g_Cfg.pipDynFps : 20.f);   // review: from the frame rate when it's applied
    float iv = g_DynHalf ? (float)(1.9 / ref) : 0.f;   // just under 2 frames: a capture every other frame
    alignas(16) unsigned char b[16] = {}; memcpy(b + oI, &iv, 4);
    Orig_ProcessEvent(comp, fn, b);
}
// put the current level's values on both windows (this player's and, if on, player 3's)
static void DynApply(bool sharpChanged, bool halfChanged)
{
    auto one = [&]()
    {
        void* actor = ResolveWeak(g_Player->pipActIdx, g_Player->pipActSer);
        if (sharpChanged) { if (actor) PipForceRespawn(); return; }   // a new picture at the new size (made on the next tick)
        unsigned char* c = (unsigned char*)PipComponent(actor);
        if (!c) return;
        if (g_Pip.cLod >= 0) *(float*)(c + g_Pip.cLod) = PipDetailNow();
        if (g_Pip.cDist >= 0) *(float*)(c + g_Pip.cDist) = PipViewDistNow() > 0.f ? PipViewDistNow() : -1.f;   // -1 = no limit (the engine's default)
        if (halfChanged) DynApplyInterval(c);
    };
    one();
    if (g_P3Enabled) { WithP3 w; one(); }
}
static void DynSetLevel(int lv, const char* why)
{
    if (lv < 0) lv = 0;
    if (lv > g_DynN) lv = g_DynN;
    float det = -1.f, dist = -1.f; int sharp = -1; bool half = false;
    for (int i = 0; i < lv; ++i)
    {
        const DynStep& s = g_DynSteps[i];
        if (s.lever == 1) det = s.v; else if (s.lever == 2) dist = s.v; else if (s.lever == 3) sharp = (int)s.v; else if (s.lever == 4) half = true;
    }
    bool sharpChanged = sharp != g_DynSharp, halfChanged = half != g_DynHalf;
    bool any = sharpChanged || halfChanged || det != g_DynDetail || dist != g_DynDist;
    int old = g_DynLevel;
    g_DynDetail = det; g_DynDist = dist; g_DynSharp = sharp; g_DynHalf = half; g_DynLevel = lv;
    if (any) DynApply(sharpChanged, halfChanged);
    static int logs = 0;
    if (why && logs < 200)
    {
        ++logs;
        char dm[24]; if (PipViewDistNow() > 0.f) snprintf(dm, sizeof(dm), "%.0f m", PipViewDistNow() / 100.f); else snprintf(dm, sizeof(dm), "no limit");
        const DynStep* s = lv > old ? &g_DynSteps[lv - 1] : (old > 0 && old <= g_DynN ? &g_DynSteps[old - 1] : nullptr);
        CoopLog("DYNQ: %s - step %d of %d%s%s | windows now: detail %.1f, draw distance %s, sharpness %d%%, %s", why, lv, g_DynN,
            s ? (lv > old ? ", lowered " : ", raised ") : "", s ? DynLeverName(s->lever) : "",
            PipDetailNow(), dm,
            PipSharpNow(), g_DynHalf ? "every other frame" : "every frame");
    }
}
// (the author: "a guardrail to the dynamic quality - if the game FPS is set lower than the value in the settings app, then
// it is automatically disabled"): the game's own frame rate limit = UGameUserSettings::FrameRateLimit (+0x60, 0 = no limit),
// the object from the static GameUserSettings::GetGameUserSettings (GEngine's). Read every 2 s. NOT KNOWN yet whether the
// game's own Frame Rate option is stored there - the DYNQ line logs the value read, compare it with the game's setting.
// (A limit from V-Sync, the driver or another tool can't be seen this way.) Returns the limit, 0 = none / not known.
static float DynGameFpsLimit()
{
    static int st = 0; static int gIdx = -1, gSer = 0; static ULONGLONG lastRead = 0; static float lim = 0.f;
    ULONGLONG now = GetTickCount64();
    if (st < 0 || now - lastRead < 2000) return st > 0 ? lim : 0.f;
    lastRead = now;
    void* gus = ResolveWeak(gIdx, gSer);
    if (!gus)
    {
        void* cdo = FindObj("Default__GameUserSettings", "GameUserSettings");
        void* fn = cdo ? FindFunc(cdo, "GetGameUserSettings") : nullptr; int pr = fn ? ParamOff(fn, "ReturnValue") : -1;
        if (fn && pr >= 0 && ParmsSize(fn) <= 32)
        {
            alignas(16) unsigned char b[32] = {}; Orig_ProcessEvent(cdo, fn, b);
            gus = *(void**)(b + pr);
        }
        if (!gus || !Readable((unsigned char*)gus + 0x60, 4) || !MakeWeak(gus, &gIdx, &gSer))
        {
            if (st == 0) { st = -1; CoopLog("DYNQ: the game's frame rate limit can't be read (GameUserSettings not found) - no frame rate guardrail"); }
            return 0.f;
        }
    }
    float v = *(float*)((unsigned char*)gus + 0x60);   // FrameRateLimit
    if (!(v >= 0.f && v < 10000.f)) v = 0.f;
    if (st == 0 || fabsf(v - lim) > 0.5f)
        CoopLog("DYNQ: the game's frame rate limit reads %.0f%s (GameUserSettings FrameRateLimit)", v, v <= 0.f ? " = no limit" : " fps");
    st = 1; lim = v;
    return lim;
}
// main tick, once a frame (player 2's state loaded), after the windows' PipTick
static void DynQualityTick()
{
    static LARGE_INTEGER f = {}, last = {};
    LARGE_INTEGER now; QueryPerformanceCounter(&now); if (!f.QuadPart) QueryPerformanceFrequency(&f);
    double dt = last.QuadPart ? (double)(now.QuadPart - last.QuadPart) / (double)f.QuadPart : 0.0;
    last = now;
    ULONGLONG ms = GetTickCount64();
    // the settings changed (start / F6)? -> new step list, back to the top
    static unsigned sig = 0;
    unsigned sg = (unsigned)g_Cfg.pipDyn * 7919u + (unsigned)(g_Cfg.pipDynFps * 10.f) * 31u + (unsigned)(g_Cfg.pipDynMaxDetail * 100.f) * 17u +
                  (unsigned)g_Cfg.pipDynMinDist * 3u + (unsigned)g_Cfg.pipDynMinSharp * 131u + (unsigned)(g_Cfg.pipDetail * 100.f) * 13u +
                  (unsigned)g_Cfg.pipViewDist * 5u + (unsigned)g_Cfg.pipFullPct * 257u;
    for (int k = 0; k < 4; ++k) sg = sg * 11u + (unsigned)g_Cfg.pipDynOrder[k];
    static double secT = 0.0; static int secF = 0, below = 0, above = 0, raiseWait = 10; static ULONGLONG lastStep = 0, lastRaise = 0, lastBackoff = 0;
    if (sg != sig)
    {
        if (g_DynLevel) DynSetLevel(0, "settings changed - back to the settings' own quality");
        sig = sg; DynBuild();
        g_DynDetail = -1.f; g_DynDist = -1.f; g_DynSharp = -1; g_DynHalf = false; g_DynLevel = 0;   // (F6 re-makes the windows if the base changed)
        secT = 0.0; secF = 0; below = above = 0; raiseWait = 10;
        if (g_Cfg.pipDyn) CoopLog("DYNQ: %d step(s) available: %s%s%s%s%s", g_DynN, g_DynN ? "" : "none (everything is already at its lowest)",
            g_DynN > 0 ? DynLeverName(g_DynSteps[0].lever) : "", g_DynN > 1 ? ", then ..." : "", g_DynN > 1 ? DynLeverName(g_DynSteps[g_DynN - 1].lever) : "", g_DynN > 1 ? " last" : "");
    }
    if (!g_Cfg.pipDyn || g_DynN == 0) { if (g_DynLevel) DynSetLevel(0, "dynamic quality off"); return; }
    {   // the guardrail - the game's own limit below the target = the target can never be reached -> off
        float lim = DynGameFpsLimit();
        bool capped = lim > 0.5f && lim < g_Cfg.pipDynFps - 0.5f;
        static int was = -1;
        if ((int)capped != was)
        {
            if (capped || was == 1) CoopLog("DYNQ: %s (the game's frame rate limit %.0f fps, target %.0f fps)", capped ? "OFF - the game's frame rate limit is below the target"
                : "on again - the game's frame rate limit is no longer below the target", lim, g_Cfg.pipDynFps);
            was = capped;
        }
        if (capped) { if (g_DynLevel) DynSetLevel(0, "the game's frame rate limit is below the target - back to the settings' own quality"); secT = 0.0; secF = 0; below = above = 0; return; }
    }
    bool watch = PipShowing() && !g_GamePaused && !g_InCutscene && !g_ScriptedShot;
    if (!watch || dt <= 0.0 || dt > 1.0) { secT = 0.0; secF = 0; below = above = 0; return; }
    secT += dt; ++secF;
    if (secT < 1.0) return;
    double fps = secF / secT; secT = 0.0; secF = 0;
    g_DynFpsNow = fps;
    if (ms - lastStep < 3000) return;   // let a change settle (a new picture costs a frame or two)
    // review: "below" = 5 % under the target (a 60 fps cap / V-Sync reads 59.9). f1.117b: (the review edit had commented
    // out the rest of this line - quality never went back up) "room again" = at or above the target (was 15 % above: with a
    // frame-rate cap that's never reached - the author caps at 60); a step up that doesn't hold is undone and the wait doubles.
    if (fps < g_Cfg.pipDynFps * 0.95) { ++below; above = 0; }
    else if (fps >= g_Cfg.pipDynFps) { ++above; below = 0; }
    else { below = above = 0; }
    if (below >= 3 && g_DynLevel < g_DynN)
    {
        if (lastRaise && ms - lastRaise < 20000) { raiseWait = raiseWait * 2 > 60 ? 60 : raiseWait * 2; lastBackoff = ms; }   // a step back didn't hold
        char why[64]; snprintf(why, sizeof(why), "%.0f fps for 3 s (keeping %.0f)", fps, g_Cfg.pipDynFps);
        DynSetLevel(g_DynLevel + 1, why); lastStep = ms; below = above = 0; lastRaise = 0;
    }
    else if (above >= raiseWait && g_DynLevel > 0)
    {
        char why[64]; snprintf(why, sizeof(why), "%.0f fps for %d s (keeping %.0f)", fps, raiseWait, g_Cfg.pipDynFps);
        DynSetLevel(g_DynLevel - 1, why); lastStep = ms; lastRaise = ms; below = above = 0;
    }
    if (lastBackoff && ms - lastBackoff > 60000 && raiseWait > 10) { raiseWait = 10; lastBackoff = 0; }
}
