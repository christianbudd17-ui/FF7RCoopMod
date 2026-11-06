// ---- f1.53: smooth shapes for the P2 / P3 badges and the off-screen pointers ----
// The author 2026-10-20: "Let's go with the diamond badges" (AI in red, faded gray for no character), keep today's pointer
// design, "Let's try 1" = draw them smooth. Canvas lines / polygons have hard, stair-stepped edges. Instead each shape is
// worked out once on the CPU with soft edges (4 x 4 samples per pixel = how much of the pixel the shape covers), written
// into its own small render target (white, alpha = coverage), and every frame drawn with K2_DrawTexture, tinted with the
// color (translucent), the pointer parts turned with Rotation. Rebuilt when the screen size or StatusCirclesSize changes.
//   Render targets: KismetRenderingLibrary CreateRenderTarget2D (outer = the game instance, so it lives across levels),
//   kept from garbage collection with the RootSet object flag (GUObjectArray item +8, 0x40000000); cleared to (1,1,1,0)
//   with ClearRenderTarget2D; drawn into with BeginDrawCanvasToRenderTarget / EndDrawCanvasToRenderTarget, one
//   K2_DrawTexture tile per run of equal pixels, BLEND_Opaque (writes the pixel's alpha as it is).
//   Built from the game tick (SmoothSpritesTick, core/27), never from the HUD draw itself.
// Until the sprites are ready, or if anything is missing, or with [General] SmoothShapes=0: the old line drawing
// (the diamond as a 4-sided K2_DrawPolygon + lines). Log lines "SMOOTH: ...".
enum SprId { SPR_DFILL, SPR_DOUT, SPR_DINNER, SPR_PDISC, SPR_PRINGD, SPR_PRING, SPR_PRIM, SPR_PARROW, SPR_N };
struct SmoothState
{
    int state = 0;            // 0 = functions not looked up yet, 1 = ready, -1 = can't (old drawing)
    void* krl = nullptr, *gs = nullptr;
    void* fCreate = nullptr, *fClear = nullptr, *fBegin = nullptr, *fEnd = nullptr, *fGI = nullptr;
    int cW = -1, cWd = -1, cH = -1, cF = -1, cR = -1;           // CreateRenderTarget2D
    int xW = -1, xT = -1, xC = -1;                              // ClearRenderTarget2D
    int bW = -1, bT = -1, bCv = -1, bSz = -1, bCx = -1;         // BeginDrawCanvasToRenderTarget
    int eW = -1, eCx = -1;                                      // EndDrawCanvasToRenderTarget
    int gW = -1, gR = -1;                                       // GetGameInstance
    void* fTex = nullptr; int dT = -1, dP = -1, dS = -1, dCP = -1, dCS = -1, dC = -1, dB = -1, dR = -1, dPv = -1;   // K2_DrawTexture
    int idx[SPR_N], ser[SPR_N], size[SPR_N];
    bool ready = false;       // all sprites drawn for builtK / builtSz
    float builtK = -1.f, builtSz = -1.f;
    float wantK = -1.f;       // the HUD canvas's scale (height / 1080), set by the HUD draw
    int fails = 0; ULONGLONG lastTry = 0;
    SmoothState() { for (int i = 0; i < SPR_N; ++i) { idx[i] = -1; ser[i] = 0; size[i] = 0; } }
};
static SmoothState g_Sm;

static void SmRoot(void* obj, bool on)
{   // EInternalObjectFlags::RootSet (1 << 30) on the object's GUObjectArray item
    if (!obj) return;
    int i = *(int*)((unsigned char*)obj + 0xC);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    if (!items || i < 0 || i >= num || *(void**)(items + (size_t)i * 24) != obj) return;
    volatile LONG* fl = (volatile LONG*)(items + (size_t)i * 24 + 8);
    if (on) InterlockedOr(fl, 0x40000000); else InterlockedAnd(fl, ~0x40000000);
}
// The K2_DrawTexture of a canvas (the same UFunction for every UCanvas)
static bool SmResolveTex(void* canvas)
{
    if (g_Sm.fTex) return true;
    void* f = FindFunc(canvas, "K2_DrawTexture");
    if (!f) return false;
    g_Sm.dT = ParamOff(f, "RenderTexture"); g_Sm.dP = ParamOff(f, "ScreenPosition"); g_Sm.dS = ParamOff(f, "ScreenSize");
    g_Sm.dCP = ParamOff(f, "CoordinatePosition"); g_Sm.dCS = ParamOff(f, "CoordinateSize"); g_Sm.dC = ParamOff(f, "RenderColor");
    g_Sm.dB = ParamOff(f, "BlendMode"); g_Sm.dR = ParamOff(f, "Rotation"); g_Sm.dPv = ParamOff(f, "PivotPoint");
    if (g_Sm.dT < 0 || g_Sm.dP < 0 || g_Sm.dS < 0 || g_Sm.dCP < 0 || g_Sm.dCS < 0 || g_Sm.dC < 0 || g_Sm.dB < 0 || g_Sm.dR < 0 || g_Sm.dPv < 0 || ParmsSize(f) > 256) return false;
    g_Sm.fTex = f;
    return true;
}
static void SmTile(void* canvas, void* tex, float x, float y, float w, float h, const float col[4], int blend, float rot)
{
    alignas(16) unsigned char b[256] = {};
    float pos[2] = {x, y}, sz[2] = {w, h}, cp[2] = {0.f, 0.f}, cs[2] = {1.f, 1.f}, pv[2] = {0.5f, 0.5f};
    memcpy(b + g_Sm.dT, &tex, 8); memcpy(b + g_Sm.dP, pos, 8); memcpy(b + g_Sm.dS, sz, 8);
    memcpy(b + g_Sm.dCP, cp, 8); memcpy(b + g_Sm.dCS, cs, 8); memcpy(b + g_Sm.dC, col, 16);
    b[g_Sm.dB] = (unsigned char)blend; memcpy(b + g_Sm.dR, &rot, 4); memcpy(b + g_Sm.dPv, pv, 8);
    Orig_ProcessEvent(canvas, g_Sm.fTex, b);
}
static void SmResolve()
{
    g_Sm.state = -1;
    g_Sm.krl = FindObj("Default__KismetRenderingLibrary", "KismetRenderingLibrary");
    g_Sm.gs = FindObj("Default__GameplayStatics", "GameplayStatics");
    if (g_Sm.krl)
    {
        g_Sm.fCreate = FindFunc(g_Sm.krl, "CreateRenderTarget2D");
        g_Sm.fClear = FindFunc(g_Sm.krl, "ClearRenderTarget2D");
        g_Sm.fBegin = FindFunc(g_Sm.krl, "BeginDrawCanvasToRenderTarget");
        g_Sm.fEnd = FindFunc(g_Sm.krl, "EndDrawCanvasToRenderTarget");
    }
    if (g_Sm.gs) g_Sm.fGI = FindFunc(g_Sm.gs, "GetGameInstance");
    if (void* f = g_Sm.fCreate) { g_Sm.cW = ParamOff(f, "WorldContextObject"); g_Sm.cWd = ParamOff(f, "Width"); g_Sm.cH = ParamOff(f, "Height"); g_Sm.cF = ParamOff(f, "Format"); g_Sm.cR = ParamOff(f, "ReturnValue"); }
    if (void* f = g_Sm.fClear) { g_Sm.xW = ParamOff(f, "WorldContextObject"); g_Sm.xT = ParamOff(f, "TextureRenderTarget"); g_Sm.xC = ParamOff(f, "ClearColor"); }
    if (void* f = g_Sm.fBegin) { g_Sm.bW = ParamOff(f, "WorldContextObject"); g_Sm.bT = ParamOff(f, "TextureRenderTarget"); g_Sm.bCv = ParamOff(f, "Canvas"); g_Sm.bSz = ParamOff(f, "Size"); g_Sm.bCx = ParamOff(f, "Context"); }
    if (void* f = g_Sm.fEnd) { g_Sm.eW = ParamOff(f, "WorldContextObject"); g_Sm.eCx = ParamOff(f, "Context"); }
    if (void* f = g_Sm.fGI) { g_Sm.gW = ParamOff(f, "WorldContextObject"); g_Sm.gR = ParamOff(f, "ReturnValue"); }
    bool ok = g_Sm.fCreate && g_Sm.fClear && g_Sm.fBegin && g_Sm.fEnd && g_Sm.fGI &&
        g_Sm.cW >= 0 && g_Sm.cWd >= 0 && g_Sm.cH >= 0 && g_Sm.cF >= 0 && g_Sm.cR >= 0 && ParmsSize(g_Sm.fCreate) <= 256 &&
        g_Sm.xW >= 0 && g_Sm.xT >= 0 && g_Sm.xC >= 0 && ParmsSize(g_Sm.fClear) <= 256 &&
        g_Sm.bW >= 0 && g_Sm.bT >= 0 && g_Sm.bCv >= 0 && g_Sm.bCx >= 0 && ParmsSize(g_Sm.fBegin) <= 256 && g_Sm.bCx + 16 <= ParmsSize(g_Sm.fBegin) &&
        g_Sm.eW >= 0 && g_Sm.eCx >= 0 && ParmsSize(g_Sm.fEnd) <= 256 && g_Sm.eCx + 16 <= ParmsSize(g_Sm.fEnd) &&
        g_Sm.gW >= 0 && g_Sm.gR >= 0;
    CoopLog("SMOOTH: Create %p (W%d w%d h%d F%d R%d) Clear %p (W%d T%d C%d) Begin %p (W%d T%d Cv%d Sz%d Cx%d size %d) End %p (W%d Cx%d size %d) GetGameInstance %p -> %s",
        g_Sm.fCreate, g_Sm.cW, g_Sm.cWd, g_Sm.cH, g_Sm.cF, g_Sm.cR, g_Sm.fClear, g_Sm.xW, g_Sm.xT, g_Sm.xC,
        g_Sm.fBegin, g_Sm.bW, g_Sm.bT, g_Sm.bCv, g_Sm.bSz, g_Sm.bCx, g_Sm.fBegin ? ParmsSize(g_Sm.fBegin) : 0,
        g_Sm.fEnd, g_Sm.eW, g_Sm.eCx, g_Sm.fEnd ? ParmsSize(g_Sm.fEnd) : 0, g_Sm.fGI,
        ok ? "ready" : "NOT available - badges and pointers use the old line drawing");
    if (ok) g_Sm.state = 1;
}

// ---- the shapes (sprite pixel space, centre = (0,0), y down). k = screen height / 1080. ----
struct SmGeo
{
    float k, R, t;            // badge: diamond half-diagonal, outline thickness
    float r;                  // pointer: circle radius
};
static void SmBadgeSize(float k, float sz, float* R, float* t)
{
    *R = 34.f * k * sz * 1.5f;                     // (the old circle was 34 * k * size; a diamond needs more room for "P2")
    float tt = 3.f * k * (sz / 0.5f);              // "3 px outline" at the default size, 1080p
    tt = tt < 1.5f ? 1.5f : (tt > 8.f ? 8.f : tt);
    *t = tt;
}
static float SmSegDist(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float l2 = vx * vx + vy * vy, u = l2 > 0.f ? (wx * vx + wy * vy) / l2 : 0.f;
    if (u < 0.f) u = 0.f; else if (u > 1.f) u = 1.f;
    float dx = px - (ax + vx * u), dy = py - (ay + vy * u);
    return sqrtf(dx * dx + dy * dy);
}
static bool SmInside(int id, const SmGeo& g, float x, float y)
{
    float k = g.k;
    switch (id)
    {
    case SPR_DFILL:  return (fabsf(x) + fabsf(y) - g.R) * 0.70710678f <= 0.f;
    case SPR_DOUT:   { float d = (fabsf(x) + fabsf(y) - g.R) * 0.70710678f; return fabsf(d) <= g.t * 0.5f; }
    case SPR_DINNER: { float d = (fabsf(x) + fabsf(y) - g.R) * 0.70710678f; float in = g.t * 0.5f + 2.f * k; float w = k < 1.f ? 1.f : k; return d <= -in && d >= -in - w; }
    case SPR_PDISC:  return sqrtf(x * x + y * y) <= g.r;
    case SPR_PRINGD: return fabsf(sqrtf(x * x + y * y) - (g.r + 1.5f * k)) <= 2.f * k;
    case SPR_PRING:  return fabsf(sqrtf(x * x + y * y) - g.r) <= 1.25f * k;
    case SPR_PRIM:
    {   // pointing up: tip outside the circle, two strokes back to the rim (DrawOffscreenArrow's "small pointer on the rim")
        float tipY = -(g.r + 9.f * k), h = 1.25f * k;
        return SmSegDist(x, y, 0.f, tipY, 6.f * k, -g.r) <= h || SmSegDist(x, y, 0.f, tipY, -6.f * k, -g.r) <= h;
    }
    case SPR_PARROW:
    {   // pointing up: shaft + outlined head (DrawOffscreenArrow's facing arrow)
        float L = g.r * 0.62f, hw = g.r * 0.30f, by = -(L - hw * 1.3f), h = 1.5f * k;
        return SmSegDist(x, y, 0.f, L, 0.f, by) <= h || SmSegDist(x, y, 0.f, -L, hw, by) <= h ||
               SmSegDist(x, y, 0.f, -L, -hw, by) <= h || SmSegDist(x, y, hw, by, -hw, by) <= h;
    }
    }
    return false;
}
static int SmSpriteSize(int id, const SmGeo& g)
{
    float e = (id <= SPR_DINNER) ? g.R + g.t * 0.5f + 2.f : g.r + 9.f * g.k + 1.25f * g.k + 2.f;
    int s = 2 * (int)ceilf(e) + 2;
    return s < 8 ? 8 : (s > 512 ? 512 : s);
}
// Draws one sprite's pixels into the render target's canvas. Returns the number of tiles.
static int SmPaint(void* rtCanvas, int id, const SmGeo& g, int S)
{
    void* white = g_CF.white;
    float c = S * 0.5f;
    int tiles = 0;
    for (int py = 0; py < S; ++py)
    {
        int runX = -1, runQ = 0;
        auto Flush = [&](int endX)
        {
            if (runX >= 0 && runQ > 0)
            {
                float col[4] = {1.f, 1.f, 1.f, runQ / 255.f};
                SmTile(rtCanvas, white, (float)runX, (float)py, (float)(endX - runX), 1.f, col, 0 /*BLEND_Opaque*/, 0.f);
                ++tiles;
            }
        };
        for (int px = 0; px <= S; ++px)
        {
            int q = 0;
            if (px < S)
            {
                int n = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx)
                        if (SmInside(id, g, px + (sx + 0.5f) * 0.25f - c, py + (sy + 0.5f) * 0.25f - c)) ++n;
                q = (n * 255 + 8) / 16;
            }
            if (px == S || q != runQ) { Flush(px); runX = px; runQ = q; }
        }
    }
    return tiles;
}
// Game tick: (re)build the sprites when needed. pc = player 1's controller (any world context object).
static void SmoothSpritesTick(void* pc)
{
    if (!g_Cfg.smoothShapes || g_Sm.state < 0 || !pc || g_Sm.wantK <= 0.f || !g_CF.white || GetCurrentThreadId() != g_GameThreadId) return;
    float k = g_Sm.wantK, sz = g_Cfg.statusSize;
    bool valid = g_Sm.ready && fabsf(k - g_Sm.builtK) < 0.001f && fabsf(sz - g_Sm.builtSz) < 0.001f;
    for (int i = 0; valid && i < SPR_N; ++i) if (!ResolveWeak(g_Sm.idx[i], g_Sm.ser[i])) valid = false;
    if (valid) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_Sm.lastTry < 1000) return;
    g_Sm.lastTry = now;
    if (g_Sm.state == 0) { SmResolve(); if (g_Sm.state < 0) return; }
    if (!g_Sm.fTex) return;   // set by the first HUD draw
    g_Sm.ready = false;
    auto Fail = [&](const char* why)
    {
        ++g_Sm.fails;
        CoopLog("SMOOTH: couldn't build the shapes (%s)%s", why, g_Sm.fails >= 3 ? " - giving up, old line drawing" : " - trying again in 1 s");
        if (g_Sm.fails >= 3) g_Sm.state = -1;
    };
    alignas(16) unsigned char b[256];
    memset(b, 0, sizeof(b)); memcpy(b + g_Sm.gW, &pc, 8);
    Orig_ProcessEvent(g_Sm.gs, g_Sm.fGI, b);
    void* gi = *(void**)(b + g_Sm.gR);
    if (!gi) { Fail("no game instance"); return; }
    SmGeo g; g.k = k; SmBadgeSize(k, sz, &g.R, &g.t); g.r = 30.f * k;
    ULONGLONG t0 = GetTickCount64();
    int total = 0;
    for (int i = 0; i < SPR_N; ++i)
    {
        int S = SmSpriteSize(i, g);
        void* rt = ResolveWeak(g_Sm.idx[i], g_Sm.ser[i]);
        if (rt && g_Sm.size[i] != S) { SmRoot(rt, false); rt = nullptr; g_Sm.idx[i] = -1; }   // a new size: let the old one go
        if (!rt)
        {
            memset(b, 0, sizeof(b));
            memcpy(b + g_Sm.cW, &gi, 8); memcpy(b + g_Sm.cWd, &S, 4); memcpy(b + g_Sm.cH, &S, 4);
            b[g_Sm.cF] = 2;   // RTF_RGBA8
            Orig_ProcessEvent(g_Sm.krl, g_Sm.fCreate, b);
            rt = *(void**)(b + g_Sm.cR);
            if (!rt || !MakeWeak(rt, &g_Sm.idx[i], &g_Sm.ser[i])) { Fail("CreateRenderTarget2D returned nothing"); return; }
            SmRoot(rt, true);
            g_Sm.size[i] = S;
        }
        memset(b, 0, sizeof(b));
        float clear[4] = {1.f, 1.f, 1.f, 0.f};   // white, see-through: the edges stay white, only alpha fades
        memcpy(b + g_Sm.xW, &gi, 8); memcpy(b + g_Sm.xT, &rt, 8); memcpy(b + g_Sm.xC, clear, 16);
        Orig_ProcessEvent(g_Sm.krl, g_Sm.fClear, b);
        memset(b, 0, sizeof(b));
        memcpy(b + g_Sm.bW, &gi, 8); memcpy(b + g_Sm.bT, &rt, 8);
        Orig_ProcessEvent(g_Sm.krl, g_Sm.fBegin, b);
        void* cv = *(void**)(b + g_Sm.bCv);
        unsigned char ctx[16]; memcpy(ctx, b + g_Sm.bCx, 16);
        int tiles = 0;
        if (cv) tiles = SmPaint(cv, i, g, S);
        memset(b, 0, sizeof(b));
        memcpy(b + g_Sm.eW, &gi, 8); memcpy(b + g_Sm.eCx, ctx, 16);
        Orig_ProcessEvent(g_Sm.krl, g_Sm.fEnd, b);
        if (!cv) { Fail("BeginDrawCanvasToRenderTarget gave no canvas"); return; }
        total += tiles;
    }
    g_Sm.ready = true; g_Sm.builtK = k; g_Sm.builtSz = sz; g_Sm.fails = 0;
    static int logs = 0;
    if (logs < 10) { ++logs; CoopLog("SMOOTH: shapes built for screen scale %.2f, badge size %.2f (diamond %.0f px, pointer circle %.0f px): %d tiles in %llu ms",
        k, sz, g.R, g.r, total, (unsigned long long)(GetTickCount64() - t0)); }
}
// HUD draw: are the sprites ready for this canvas? (records the canvas scale for the tick)
static bool SmReady(void* canvas, float k)
{
    if (!g_Cfg.smoothShapes || g_Sm.state < 0) return false;
    g_Sm.wantK = k;
    if (!SmResolveTex(canvas)) { if (g_Sm.state == 0) { g_Sm.state = -1; CoopLog("SMOOTH: K2_DrawTexture not found - old line drawing"); } return false; }
    return g_Sm.ready && fabsf(k - g_Sm.builtK) < 0.001f && fabsf(g_Cfg.statusSize - g_Sm.builtSz) < 0.001f;
}
// One sprite centred at (x, y), tinted, turned by rotDeg (clockwise on screen). Unturned ones snap to whole pixels.
static void SmDraw(void* canvas, int id, float x, float y, const float col[4], float rotDeg = 0.f)
{
    void* rt = ResolveWeak(g_Sm.idx[id], g_Sm.ser[id]);
    if (!rt) return;
    float S = (float)g_Sm.size[id];
    float px = x - S * 0.5f, py = y - S * 0.5f;
    if (rotDeg == 0.f) { px = floorf(px + 0.5f); py = floorf(py + 0.5f); }
    SmTile(canvas, rt, px, py, S, S, col, 2 /*BLEND_Translucent*/, rotDeg);
}
// A small engine font for the word under the badges (RobotoTiny; Roboto if it's missing)
static void* g_SmallFont = nullptr;
static void SmText(void* canvas, void* font, const wchar_t* str, float x, float y, const float col[4]) { CanvasTextF(canvas, font, str, x, y, col, true); }   // (marker/19's body)
