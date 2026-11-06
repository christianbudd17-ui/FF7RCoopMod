// f1.307 - the game's TILTED menus in each player's own part of the screen (the author, f1.305/f1.306 tests: player 2's
// Tactical menu "squished" on the left of their window, their party panel "stretched" on the right; "the field menu may
// have been flatter without the 'tilted' look ... in battle different" - and he guessed normal windows have it too, less).
// RE 2026-11-05: the battle menus / party panel sit on SEndCanvas panels drawn in 3D (EndCanvasPanel ProjectionMode
// perspective - SEndCanvas::OnPaint 0x14E57B0, vtable slots 0x4BED310 / 0x4BF1928 / 0x4BF77A0): when SEndCanvas +0x434 is 0
// it builds an off-centre frustum whose eye looks at the middle of the WINDOW the HUD is drawn into (OutDrawElements +0x10
// -> SWindow, its size at +0x5A0, or +0x598 when that is 0; FOV at +0x438). So every tilted panel leans away from the
// middle of the WHOLE HUD - right for one screen, wrong for a player's own part: player 2's menu moved into their window
// leans as if it were near the middle (flat), their panel at the far right leans hard (stretched). Over all monitors the
// middle is the border between the monitors.
// Fix: for the paint of a canvas that lies in a player's part (player 1's part / player 2 or 3's window - the regions
// below, in window pixels, set once a frame from the game tick), the window size it reads is set to twice that part's
// centre (in the HUD target's pixels) for the call, so the eye looks at the middle of THAT player's part; put back right
// after. [PictureInPicture] MenuTiltFix=0 = the game's own (research key, not in the app).
typedef int (*CanvasPaint_t)(void* self, void* args, float* geo, void* cull, unsigned char* out, int layer, void* style, uint64_t parentEnabled);
static CanvasPaint_t g_CanvasPaintOrig = nullptr;
static float g_PerspReg[3][4] = {};   // window px: left, top, right, bottom (player 1, 2, 3)
static int g_PerspRegN = 0;           // 0 = no split: the game's own eye (the middle of the HUD)
static int g_TiltState = 0;   // 0 game's bytes, 1 patched (render position), -1 bytes didn't match (never touched)
static const uintptr_t g_TiltAt[4] = {0x14E6B75, 0x14E6C10, 0x14E6C23, 0x14E6C3E};
static const unsigned char g_TiltLay[4] = {0x08, 0x0C, 0x10, 0x10}, g_TiltRen[4] = {0x1C, 0x2C, 0x30, 0x30};
static bool g_TiltRwx = false;
static bool g_TiltBytesRender = false;   // f1.328 (code review #2): what the bytes say right now (nested paints put back the previous)
// f1.326: one call with the game's bytes (the page was made writable once by TiltRenderPosPatch - no VirtualProtect per call)
static inline void TiltBytes(bool render)
{
    for (int i = 0; i < 4; ++i) *((unsigned char*)(g_Base + g_TiltAt[i]) + 5) = render ? g_TiltRen[i] : g_TiltLay[i];
    g_TiltBytesRender = render;
}
// f1.326 (the author: the summon gauge "slanted and all kinds of not right" in every layout): the summon gauges are
// screen-sized widgets the GAME puts in place with its own render translation, so with the render-position patch their
// tilt centre moved far off. A 3D panel whose render-minus-layout offset is a summon gauge's translation keeps the game's
// own (layout) centre for its paint.
static bool TiltGaugeCanvas(const float* geo)
{
    if (g_SgORT < 0) return false;
    float dx = geo[11] - geo[3], dy = geo[12] - geo[4], m = geo[7] != 0.f ? geo[7] : 1.f;
    for (int k = 0; k < 2; ++k)
    {
        unsigned char* g = (unsigned char*)g_SgLive[k];
        if (!g || !Readable(g + g_SgORT, 8)) continue;
        const float* t = (const float*)(g + g_SgORT);
        if (fabsf(t[0]) < 1.f && fabsf(t[1]) < 1.f) continue;
        if (fabsf(dx - t[0] * m) < 3.f + 0.02f * fabsf(t[0] * m) && fabsf(dy - t[1] * m) < 3.f + 0.02f * fabsf(t[1] * m)) return true;
    }
    return false;
}
typedef int (*CanvasPaintI_t)(void* iface, void* a2, float* geo, void* a4, int* rect, void* a6, void* a7, uint64_t a8);
static CanvasPaintI_t g_CanvasPaintIOrig = nullptr;   // f1.316 (below)
static int g_CpN[6] = {};   // f1.309 diag: calls, no regions, no window, outside every part, changed, not a rectangle (f1.317)
static float g_CpLast[6] = {};
static void CanvasPaintDiag()
{
    static ULONGLONG next = 0; static int lines = 0; ULONGLONG now = GetTickCount64();
    if (now < next || lines >= 12) return;
    next = now + 5000;
    if (!g_CpN[0]) { static int z = 0; if (z < 3 && (g_CanvasPaintOrig || g_CanvasPaintIOrig)) { ++z; CoopLog("MENUTILT DIAG: last 5 s - no tilted-panel paint went through the hook (%d players' parts)", g_PerspRegN); } return; }
    ++lines;
    CoopLog("MENUTILT DIAG: last 5 s - %d tilted-panel paints: %d with no players' parts (%d parts now), %d no window, %d outside every part (last at %.0f,%.0f target px = %.0f,%.0f window px), %d leaned to their part, %d not a rectangle (left alone)",
        g_CpN[0], g_CpN[1], g_PerspRegN, g_CpN[2], g_CpN[3], g_CpLast[0], g_CpLast[1], g_CpLast[2], g_CpLast[3], g_CpN[4], g_CpN[5]);
    for (int i = 0; i < 6; ++i) g_CpN[i] = 0;
}
static int Hook_CanvasPaint(void* self, void* args, float* geo, void* cull, unsigned char* out, int layer, void* style, uint64_t pe)
{
    ++g_CpN[0];
    if (g_PerspRegN <= 0) ++g_CpN[1];
    if (g_Cfg.menuTiltFix != 2 || g_PerspRegN <= 0 || !g_GameHwnd || !geo || !out || !Readable(geo, 56) || !Readable(out + 0x10, 8))
        return g_CanvasPaintOrig(self, args, geo, cull, out, layer, style, pe);
    unsigned char* win = *(unsigned char**)(out + 0x10);
    if (!win || !Readable(win + 0x598, 16)) { ++g_CpN[2]; return g_CanvasPaintOrig(self, args, geo, cull, out, layer, style, pe); }
    float* sz = (float*)(win + 0x5A0);
    if (sz[0] == 0.f) sz = (float*)(win + 0x598);
    float Wrt = sz[0], Hrt = sz[1];
    RECT cr{}; GetClientRect(g_GameHwnd, &cr);
    float Wv = (float)cr.right, Hv = (float)cr.bottom;
    if (Wrt < 2.f || Hrt < 2.f || Wv < 2.f || Hv < 2.f) return g_CanvasPaintOrig(self, args, geo, cull, out, layer, style, pe);
    float k = Wrt / Wv;   // HUD target pixels per window pixel (16:9 by width, centred - see 31b Hook_RootDraw)
    // the canvas's middle as the perspective code reads it (absolute position + half its size), in target pixels
    float gx = geo[3] + geo[0] * geo[2] * 0.5f, gy = geo[4] + geo[1] * geo[2] * 0.5f;
    // ... and where it really is drawn (the accumulated render transform - includes the mod's moves)
    float rx = geo[11] + geo[7] * geo[0] * 0.5f + geo[9] * geo[1] * 0.5f, ry = geo[12] + geo[8] * geo[0] * 0.5f + geo[10] * geo[1] * 0.5f;
    float wx = (rx - Wrt * 0.5f) / k + Wv * 0.5f, wy = (ry - Hrt * 0.5f) / k + Hv * 0.5f;   // -> window px
    int p = -1;
    for (int i = 0; i < g_PerspRegN && i < 3; ++i)
        if (wx >= g_PerspReg[i][0] && wx < g_PerspReg[i][2] && wy >= g_PerspReg[i][1] && wy < g_PerspReg[i][3]) { p = i; break; }
    if (p < 0) { ++g_CpN[3]; g_CpLast[0] = rx; g_CpLast[1] = ry; g_CpLast[2] = wx; g_CpLast[3] = wy; return g_CanvasPaintOrig(self, args, geo, cull, out, layer, style, pe); }
    ++g_CpN[4];
    float cx = (g_PerspReg[p][0] + g_PerspReg[p][2]) * 0.5f, cy = (g_PerspReg[p][1] + g_PerspReg[p][3]) * 0.5f;   // the part's middle, window px
    float tcx = (cx - Wv * 0.5f) * k + Wrt * 0.5f, tcy = (cy - Hv * 0.5f) * k + Hrt * 0.5f;   // ... in target px
    // the code leans the canvas by (its read middle - window size / 2); we want (where it really is - the part's middle)
    float nx = 2.f * (gx - rx + tcx), ny = 2.f * (gy - ry + tcy);
    if (!(nx > 1.f && ny > 1.f)) return g_CanvasPaintOrig(self, args, geo, cull, out, layer, style, pe);
    static int lg = 0;
    if (lg < 16)
    {
        static float lastP[3][2] = {{-1e9f, -1e9f}, {-1e9f, -1e9f}, {-1e9f, -1e9f}};
        if (fabsf(lastP[p][0] - rx) > 40.f || fabsf(lastP[p][1] - ry) > 40.f)
        {
            ++lg; lastP[p][0] = rx; lastP[p][1] = ry;
            CoopLog("MENUTILT: player %d's tilted panel (mode %d, FOV %.1f) - read middle %.0f,%.0f, drawn at %.0f,%.0f (target px), window %.0fx%.0f -> eye at their part's middle %.0f,%.0f (window size for the call %.0fx%.0f)",
                p + 1, Readable((unsigned char*)self + 0x434, 8) ? *(int*)((unsigned char*)self + 0x434) : -1, Readable((unsigned char*)self + 0x438, 4) ? *(float*)((unsigned char*)self + 0x438) : 0.f,
                gx, gy, rx, ry, Wrt, Hrt, tcx, tcy, nx, ny);
        }
    }
    float save0 = sz[0], save1 = sz[1];
    sz[0] = nx; sz[1] = ny;
    int r = g_CanvasPaintOrig(self, args, geo, cull, out, layer, style, pe);
    sz[0] = save0; sz[1] = save1;
    return r;
}
// f1.316 - THE REAL 3D PATH (RE 2026-11-05 evening, the author: "Do more research on the menu display issue"): f1.308's
// hook on SEndCanvas::OnPaint (main vtable slot 61) never ran - but the FOV constant (pi/360, 0x4F919C8) has a second user,
// 0x14E69F0, slot 2 of SEndCanvas's SECOND vtable (0x4BED110, an interface at this+0x330: "this - 0x330" in its first
// lines). That is the perspective paint the HUD uses. Its 5th argument is a rectangle (ints: +8 left, +0xC top, +0x10
// right, +0x14 bottom) whose size the eye is placed by ((right-left) - the canvas middle ...), FOV at iface +0x108, the 3D
// switch at iface +0x104 (0 = 3D). Same idea as f1.307: for a canvas in a player's part, that rectangle's right / bottom
// are set (for the call) so its middle is that part's middle; put back after. MENUTILT2 lines (raw values, first calls).
static int Hook_CanvasPaintI(void* iface, void* a2, float* geo, void* a4, int* rect, void* a6, void* a7, uint64_t a8)
{
    ++g_CpN[0];
    static int raw = 0;
    if (raw < 8 && geo && rect && Readable(geo, 56) && Readable(rect, 0x18))
    {
        ++raw; unsigned char* ib = (unsigned char*)iface;
        if (Readable(rect, 0x20)) { unsigned long long* qq = (unsigned long long*)rect; CoopLog("MENUTILT2: paint #%d's 5th argument, first 4 qwords: %016llX %016llX %016llX %016llX", raw, qq[0], qq[1], qq[2], qq[3]); }
        CoopLog("MENUTILT2: 3D panel paint #%d - rect %d,%d %d,%d %d,%d (+0..+0x14), geometry %.1fx%.1f scale %.3f at %.1f,%.1f, render T %.1f,%.1f M %.3f %.3f, 3D %d FOV %.1f, %d parts",
            raw, rect[0], rect[1], rect[2], rect[3], rect[4], rect[5], geo[0], geo[1], geo[2], geo[3], geo[4], geo[11], geo[12], geo[7], geo[10],
            Readable(ib + 0x104, 8) ? *(int*)(ib + 0x104) : -1, Readable(ib + 0x108, 4) ? *(float*)(ib + 0x108) : 0.f, g_PerspRegN);
    }
    if (g_Cfg.menuTiltFix == 1)
    {   // f1.326: render-position patch on - the summon gauges' panels keep the game's own tilt centre
        if (g_TiltState == 1 && geo && Readable(geo, 56) && TiltGaugeCanvas(geo))
        {
            static int lg = 0; if (lg < 3) { ++lg; CoopLog("MENUTILT3: a summon gauge's 3D panel - painted with the game's own tilt centre (offset %.0f, %.0f)", geo[11] - geo[3], geo[12] - geo[4]); }
            bool prev = g_TiltBytesRender;
            TiltBytes(false);
            int r = g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8);
            TiltBytes(prev && g_TiltState == 1);
            return r;
        }
        return g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8);
    }
    if (g_Cfg.menuTiltFix != 2 || g_PerspRegN <= 0 || !g_GameHwnd || !geo || !rect || !Readable(geo, 56) || !Readable(rect, 0x18))
    { if (g_PerspRegN <= 0) ++g_CpN[1]; return g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8); }
    // f1.317 guard (RE 2026-11-05 late): the 5th argument is OutDrawElements (SWidget::Paint 0x3A0A780 passes its element
    // list, clipping stack at +0xA0, to slot 0 of [widget + 0x330]). Whether its +8..+0x14 really are the target's rectangle
    // (the frustum reads them as ints) can't be told from the code alone - the sister copy 0x14E57B0 treats +0x10 as an
    // SWindow pointer. So nothing is changed unless they look like a rectangle: left/top 0..8191, size 256..16384 x
    // 144..16384, aspect 0.5..6 (pointer bits never pass that). A rejected call is logged (first 6) and left alone.
    {
        int L = rect[2], T = rect[3], Wi = rect[4] - rect[2], Hi = rect[5] - rect[3];
        bool ok = L >= 0 && L < 8192 && T >= 0 && T < 8192 && Wi >= 256 && Wi <= 16384 && Hi >= 144 && Hi <= 16384
               && (float)Wi >= 0.5f * (float)Hi && (float)Wi <= 6.f * (float)Hi;
        if (!ok)
        {
            ++g_CpN[5];
            static int rej = 0;
            if (rej < 6) { ++rej; CoopLog("MENUTILT2: the paint's +8..+0x14 (%d,%d %d,%d) don't look like a rectangle - this panel left as the game draws it", rect[2], rect[3], rect[4], rect[5]); }
            return g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8);
        }
    }
    float Wrt = (float)(rect[4] - rect[2]), Hrt = (float)(rect[5] - rect[3]);
    RECT cr{}; GetClientRect(g_GameHwnd, &cr);
    float Wv = (float)cr.right, Hv = (float)cr.bottom;
    if (Wrt < 2.f || Hrt < 2.f || Wv < 2.f || Hv < 2.f) { ++g_CpN[2]; return g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8); }
    // f1.317: target px <-> window px from the renderer's InGameUI target (FSceneRenderTargets 0x5923AD0 +0x240 / +0x244 -
    // the overlay maps it by width, centred) - so finding the player's part doesn't depend on what the rectangle means
    float Tw = Wrt, Th = Hrt;
    {
        unsigned char* sc = (unsigned char*)(g_Base + 0x5923AD0);
        if (Readable(sc + 0x240, 8)) { int tw = *(int*)(sc + 0x240), th = *(int*)(sc + 0x244); if (tw >= 256 && th >= 144 && tw <= 16384 && th <= 16384) { Tw = (float)tw; Th = (float)th; } }
    }
    float k = Tw / Wv;
    float gx = geo[3] + geo[0] * geo[2] * 0.5f, gy = geo[4] + geo[1] * geo[2] * 0.5f;
    float rx = geo[11] + geo[7] * geo[0] * 0.5f + geo[9] * geo[1] * 0.5f, ry = geo[12] + geo[8] * geo[0] * 0.5f + geo[10] * geo[1] * 0.5f;
    float wx = (rx - Tw * 0.5f) / k + Wv * 0.5f, wy = (ry - Th * 0.5f) / k + Hv * 0.5f;
    int p = -1;
    for (int i = 0; i < g_PerspRegN && i < 3; ++i)
        if (wx >= g_PerspReg[i][0] && wx < g_PerspReg[i][2] && wy >= g_PerspReg[i][1] && wy < g_PerspReg[i][3]) { p = i; break; }
    if (p < 0) { ++g_CpN[3]; g_CpLast[0] = rx; g_CpLast[1] = ry; g_CpLast[2] = wx; g_CpLast[3] = wy; return g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8); }
    float cx = (g_PerspReg[p][0] + g_PerspReg[p][2]) * 0.5f, cy = (g_PerspReg[p][1] + g_PerspReg[p][3]) * 0.5f;
    float tcx = (cx - Wv * 0.5f) * k + Tw * 0.5f, tcy = (cy - Hv * 0.5f) * k + Th * 0.5f;
    // RE of the frustum (0x14E6D1D - 0x14E6D86): the lean comes from the canvas middle in the rect's NDC,
    // ndc = 2 * middle / size - 1 (x; y the same, flipped) - the rect's left / top are not used. The wanted lean is the one
    // it would have on a screen the size of the player's part: d = (drawn middle - part middle) / (part size / 2). So the
    // rect's size for the call = 2 * read middle / (1 + d).
    float hwx = (g_PerspReg[p][2] - g_PerspReg[p][0]) * 0.5f * k, hwy = (g_PerspReg[p][3] - g_PerspReg[p][1]) * 0.5f * k;   // the part's half size, target px
    float dx = hwx > 1.f ? (rx - tcx) / hwx : 0.f, dy = hwy > 1.f ? (ry - tcy) / hwy : 0.f;
    dx = dx < -0.95f ? -0.95f : dx > 3.f ? 3.f : dx;
    dy = dy < -0.95f ? -0.95f : dy > 3.f ? 3.f : dy;
    float nx = 2.f * gx / (1.f + dx), ny = 2.f * gy / (1.f + dy);
    if (!(nx > 1.f && ny > 1.f && nx < 100000.f && ny < 100000.f)) { ++g_CpN[2]; return g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8); }
    ++g_CpN[4];
    static int lg = 0;
    if (lg < 16)
    {
        static float lastP[3][2] = {{-1e9f, -1e9f}, {-1e9f, -1e9f}, {-1e9f, -1e9f}};
        if (fabsf(lastP[p][0] - rx) > 40.f || fabsf(lastP[p][1] - ry) > 40.f)
        {
            ++lg; lastP[p][0] = rx; lastP[p][1] = ry;
            CoopLog("MENUTILT2: part %d's 3D panel - read middle %.0f,%.0f, drawn at %.0f,%.0f, rect %dx%d -> %.0fx%.0f (part middle %.0f,%.0f, lean %.2f,%.2f of the part - was %.2f,%.2f)",
                p + 1, gx, gy, rx, ry, rect[4] - rect[2], rect[5] - rect[3], nx, ny, tcx, tcy, dx, dy,
                rect[4] - rect[2] > 0 ? 2.f * gx / (float)(rect[4] - rect[2]) - 1.f : 0.f, rect[5] - rect[3] > 0 ? 2.f * gy / (float)(rect[5] - rect[3]) - 1.f : 0.f);
        }
    }
    int s4 = rect[4], s5 = rect[5];
    rect[4] = rect[2] + (int)(nx + 0.5f); rect[5] = rect[3] + (int)(ny + 0.5f);
    int r = g_CanvasPaintIOrig(iface, a2, geo, a4, rect, a6, a7, a8);
    rect[4] = s4; rect[5] = s5;
    return r;
}
static void CanvasPaintInstall()
{
    {   // f1.316: the interface slot (the path the HUD really uses)
        static bool doneI = false;
        if (!doneI)
        {
            doneI = true;
            void** slot = (void**)(g_Base + 0x4BED120);
            DWORD old;
            if (Readable(slot, 8) && (uintptr_t)*slot == g_Base + 0x14E69F0 && VirtualProtect(slot, 8, PAGE_READWRITE, &old))
            {
                g_CanvasPaintIOrig = (CanvasPaintI_t)(g_Base + 0x14E69F0);   // f1.328 (code review #8): before the slot
                *slot = (void*)&Hook_CanvasPaintI; VirtualProtect(slot, 8, old, &old);
                CoopLog("MENUTILT2: the 3D panel paint (0x14E69F0, interface slot 0x4BED120) hooked");
            }
            else CoopLog("MENUTILT2: interface slot 0x4BED120 doesn't hold 0x14E69F0 - not hooked");
        }
    }
    static bool done = false; if (done) return; done = true;
    static const uintptr_t vts[3] = {0x4BED310, 0x4BF1928, 0x4BF77A0};   // f1.308: the real slots (f1.307 had wrong ones - nothing hooked)
    int ok = 0;
    g_CanvasPaintOrig = (CanvasPaint_t)(g_Base + 0x14E57B0);   // f1.328 (code review #8): before any slot
    for (uintptr_t v : vts)
    {
        void** slot = (void**)(g_Base + v);
        if (!Readable(slot, 8) || (uintptr_t)*slot != g_Base + 0x14E57B0) { CoopLog("MENUTILT: vtable slot %llX doesn't hold 0x14E57B0 - skipped", (unsigned long long)v); continue; }
        DWORD old; if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) continue;
        *slot = (void*)&Hook_CanvasPaint;
        VirtualProtect(slot, 8, old, &old);
        ++ok;
    }
    if (!ok) g_CanvasPaintOrig = nullptr;
    CoopLog("MENUTILT: tilted-panel paint hooked in %d of 3 tables (MenuTiltFix=%d)", ok, (int)g_Cfg.menuTiltFix);
}
// once a frame from the game tick (core/27): the players' parts in window pixels
// f1.320 - THE CAUSE (RE 2026-11-05 night, emulating 0x14E6B75-0x14E70B6 with unicorn): the 3D panel matrix is
//   x' = 2x/W - 1 + ndc_c * k * z,  y' = 1 - 2y/H + ndcy_c * k * z,  w' = 1 + k * z   (W x H = 1920 x 1080, k = 0.001 / scale)
// - the perspective centre is the PANEL'S OWN middle (ndc_c), so a panel normally tilts the same anywhere on the screen. But
// ndc_c comes from the canvas's LAYOUT position (FGeometry AbsolutePosition +0xC / +0x10, Scale +8) while the vertices are
// drawn where its RENDER transform puts them (+0x2C / +0x30, M +0x1C). Our split screen moves players' menus / panels with
// render translations (+ render scale for CommandMenuScale): player 2's / 3's menus are drawn far from their layout spot, so
// they lean as if seen from there - the further moved, the worse (P3 > P2 > P1, also a little in ordinary split windows).
// Fix: the four reads that build ndc_c / size / k read the render transform instead (disp8 bytes: 0x14E6B75 +8 -> +0x1C,
// 0x14E6C10 +0xC -> +0x2C, 0x14E6C23 / 0x14E6C3E +0x10 -> +0x30). For panels nobody moved both are the same (no change).
// On while split windows show (f1.320: or spanning); put back otherwise. MenuTiltFix=0 off, 1 this (default), 2 = f1.318 hook.
static void TiltRenderPosPatch(bool want)
{
    int& state = g_TiltState;
    if (state < 0 || (want ? 1 : 0) == state) return;
    static const struct { uintptr_t at; unsigned char orig[6]; unsigned char nd; } P[4] = {
        {0x14E6B75, {0xF3, 0x45, 0x0F, 0x10, 0x6E, 0x08}, 0x1C},
        {0x14E6C10, {0xF3, 0x45, 0x0F, 0x10, 0x4E, 0x0C}, 0x2C},
        {0x14E6C23, {0xF3, 0x41, 0x0F, 0x10, 0x7E, 0x10}, 0x30},
        {0x14E6C3E, {0xF3, 0x45, 0x0F, 0x58, 0x66, 0x10}, 0x30}};
    if (state == 0)
        for (auto& e : P)
        {
            unsigned char* a = (unsigned char*)(g_Base + e.at);
            if (!Readable(a, 6) || memcmp(a, e.orig, 6) != 0)
            {
                state = -1;
                CoopLog("MENUTILT3: bytes at %llX don't match this game version - tilted menus left as the game draws them", (unsigned long long)e.at);
                return;
            }
        }
    if (!g_TiltRwx)
    {   // f1.326: writable once (stays) - the summon gauges' paints switch the bytes per call (TiltBytes)
        DWORD old; g_TiltRwx = VirtualProtect((void*)(g_Base + 0x14E6B75), 0x14E6C44 - 0x14E6B75, PAGE_EXECUTE_READWRITE, &old) != 0;
        if (!g_TiltRwx) { state = -1; CoopLog("MENUTILT3: can't make the paint code writable - tilted menus left as the game draws them"); return; }
    }
    for (auto& e : P) *((unsigned char*)(g_Base + e.at) + 5) = want ? e.nd : e.orig[5];
    g_TiltBytesRender = want;
    FlushInstructionCache(GetCurrentProcess(), (void*)(g_Base + 0x14E6B75), 0x14E6C44 - 0x14E6B75);
    state = want ? 1 : 0;
    CoopLog("MENUTILT3: tilted menus centred on where they're %s (0x14E69F0 reads the %s position)", want ? "DRAWN - moved menus tilt like unmoved ones" : "laid out - the game's own", want ? "render" : "layout");
}
static void PerspRegionsTick()
{
    CanvasPaintDiag();
    if (g_Cfg.menuTiltFix == 1) SummonGaugeTrack();   // f1.328: whatever the party panel setting
    TiltRenderPosPatch(g_Cfg.menuTiltFix == 1 && AnyWindowShowing());   // f1.321: only while windows show (the summon gauge in a
    // wide-screen cutscene looked wrong with it on - the game moves some of its own panels by render transform)
    if (g_Cfg.menuTiltFix != 2) { g_PerspRegN = 0; if (g_Cfg.menuTiltFix == 1) CanvasPaintInstall(); return; }   // f1.326: the hook (summon gauges) with the patch
    CanvasPaintInstall();
    if ((!g_CanvasPaintOrig && !g_CanvasPaintIOrig) || !g_GameHwnd) { g_PerspRegN = 0; return; }
    RECT cr{}; GetClientRect(g_GameHwnd, &cr);
    float Wv = (float)cr.right, Hv = (float)cr.bottom;
    int n = 0;
    if (g_HudSpanOk)
    {   // over all monitors (31b): player 1's monitor + players 2/3's parts, fractions of the window (f1.309: + mode 3 = player 1's only)
        for (int p = 0; p < 3; ++p)
        {
            if (g_MonRegF[p][2] <= 0.f || g_MonRegF[p][3] <= 0.f) continue;
            g_PerspReg[n][0] = g_MonRegF[p][0] * Wv; g_PerspReg[n][1] = g_MonRegF[p][1] * Hv;
            g_PerspReg[n][2] = (g_MonRegF[p][0] + g_MonRegF[p][2]) * Wv; g_PerspReg[n][3] = (g_MonRegF[p][1] + g_MonRegF[p][3]) * Hv;
            ++n;
        }
    }
    else if (AnyWindowShowing())
    {   // one screen, split: player 1's part + the windows (36f EsRegion, canvas px -> window px)
        float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f;
        float sx = Wv / W, sy = Hv / H;
        for (int pn = 1; pn <= 3; ++pn)
        {
            float r[4];
            if (!EsRegion(pn, W, H, r)) continue;
            g_PerspReg[n][0] = r[0] * sx; g_PerspReg[n][1] = r[1] * sy; g_PerspReg[n][2] = r[2] * sx; g_PerspReg[n][3] = r[3] * sy; ++n;
        }
    }
    // player 1's region first (EsRegion order / g_MonRegF order) - a canvas is matched to the first part that holds it
    g_PerspRegN = n;
}
