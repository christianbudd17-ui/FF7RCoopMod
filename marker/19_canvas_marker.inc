// =================================================================================
// P2 HUD MARKER
// =================================================================================
// The game's HUD actor (HUD_0) runs AHUD::ReceiveDrawHUD through UObject::ProcessEvent every frame,
// and we already hook ProcessEvent. Right after it runs, AHUD::Canvas (+0x3C0) is a live UCanvas
// for this frame, so we draw on it with the engine's own Blueprint canvas calls (also through
// ProcessEvent): K2_Project (world -> screen), K2_DrawLine, K2_DrawText. The functions and their
// parameter offsets are looked up at runtime from the reflection data, so no layout is hard-coded:
//   UObject: Class +0x10, Name +0x18 | UField::Next +0x28 | UStruct: Super +0x30, Children +0x38
//   UProperty::Offset_Internal +0x44 | UFunction::ParmsSize +0x96 (uint16)
// Marker: a green arrow with "P2" floating above player 2's target. When R3-locked it also gets
// green corner brackets around the enemy and the label reads "P2 LOCK".
static constexpr size_t OFF_HUD_CANVAS      = 0x3C0;
static constexpr size_t OFF_CANVAS_CLIPX    = 0x30;
static constexpr size_t OFF_CANVAS_CLIPY    = 0x34;
static constexpr size_t OFF_CAPSULE_HALF_H  = 0x710;

static void* g_FnDrawHUD = nullptr;
// every distinct UFunction going through ProcessEvent is name-checked once and classified:
//   1 = AHUD::ReceiveDrawHUD (draw the P2 marker after it)
//   2 = a "cutscene begins" event, 3 = a "cutscene ends" event (co-op camera stands down in between)
static int ClassifyFn(void* fn)
{
    if (!fn) return 0;
    if (fn == g_FnDrawHUD) return 1;
    if (GetCurrentThreadId() != g_GameThreadId) return 0;
    static void* keys[16384];
    static unsigned char vals[16384];
    uintptr_t h = ((uintptr_t)fn >> 4) & 16383;
    int slot = -1;
    for (int i = 0; i < 16384; ++i, h = (h + 1) & 16383)
    {
        if (keys[h] == fn) return vals[h];
        if (!keys[h]) { slot = (int)h; break; }
    }
    char nm[96]; NameOf(fn, nm, sizeof(nm));
    int c = 0;
    if (strcmp(nm, "ReceiveDrawHUD") == 0) { c = 1; g_FnDrawHUD = fn; CoopLog("HUD marker: found ReceiveDrawHUD (%p)", fn); }
    else if (strcmp(nm, "OnBeginCutScene") == 0 || strcmp(nm, "OnBeginCutSceneToBP") == 0) c = 2;
    else if (strcmp(nm, "OnEndCutScene") == 0 || strcmp(nm, "OnEndCutSceneToBP") == 0) c = 3;
    else if (strcmp(nm, "OnGiveDamage") == 0) c = 4;        // enemy target memory
    else if (strcmp(nm, "OnTakeDamage") == 0) c = 5;
    else if (strcmp(nm, "OnBeginAbility") == 0) c = 6;
    else if (strcmp(nm, "OnCompleteAbility") == 0) c = 7;
    else if (strcmp(nm, "OnPrepareAbility") == 0) c = 8;        // f1.226 BOSSACT (enemies/22g, log only)
    else if (strcmp(nm, "OnSuspendAbility") == 0) c = 9;
    else if (strcmp(nm, "OnInterceptAbility") == 0) c = 10;
    else if (strcmp(nm, "OnArrivalRegisterMove") == 0) c = 11;
    else if (strcmp(nm, "OnCompleteRegisterMove") == 0) c = 12;
    else if (strcmp(nm, "OnChangeStep") == 0) c = 13;
    else if (strcmp(nm, "OnChangePhase") == 0) c = 14;
    else if (strcmp(nm, "OnTakeDamageOnce") == 0) c = 15;     // f1.233: a boss's damage events (enemies/22f BossEventPre)
    else if (strcmp(nm, "OnIsHitDamage") == 0) c = 16;
    if (slot >= 0) { keys[slot] = fn; vals[slot] = (unsigned char)c; }
    return c;
}

static void* FieldNext(void* f) { return *(void**)((unsigned char*)f + 0x28); }
static bool NameIs(void* obj, const char* want) { char nm[128]; NameOf(obj, nm, sizeof(nm)); return strcmp(nm, want) == 0; }
// (review C11): a function / property by name in a class (or struct) and its parents - SuperStruct +0x30, Children +0x38,
// Next +0x28 (FindFunc, PropOff, PropField and HcResolve's GetCachedGeometry each walked it themselves). ParamOff below stays
// separate: a function's parameters must NOT be looked for in a parent.
static void* FindField(void* cls, const char* name)
{
    for (void* c = cls; c; c = *(void**)((unsigned char*)c + 0x30))
        for (void* f = *(void**)((unsigned char*)c + 0x38); f; f = FieldNext(f))
            if (NameIs(f, name)) return f;
    return nullptr;
}
static void* FindFunc(void* obj, const char* name) { return FindField(*(void**)((unsigned char*)obj + 0x10), name); }
static int ParamOff(void* fn, const char* name)
{
    for (void* p = *(void**)((unsigned char*)fn + 0x38); p; p = FieldNext(p))
        if (NameIs(p, name)) return *(int*)((unsigned char*)p + 0x44);
    return -1;
}
static int ParmsSize(void* fn) { return *(unsigned short*)((unsigned char*)fn + 0x96); }

struct CanvasFns
{
    int state = 0; // 0 = not tried, 1 = ready, -1 = failed
    void* project = nullptr; int pW = -1, pR = -1;
    void* line = nullptr;    int lA = -1, lB = -1, lT = -1, lC = -1;
    void* text = nullptr;    int tF = -1, tS = -1, tP = -1, tC = -1, tK = -1, tSC = -1, tSO = -1, tCX = -1, tCY = -1, tO = -1, tOC = -1;
    void* poly = nullptr;    int gT = -1, gP = -1, gR = -1, gN = -1, gC = -1;   // K2_DrawPolygon
    void* white = nullptr;   // plain white texture for filled shapes
};
static CanvasFns g_CF;
static void* g_MarkerFont = nullptr;

static void ResolveCanvasFns(void* canvas)
{
    g_CF.state = -1;
    g_CF.project = FindFunc(canvas, "K2_Project");
    g_CF.line    = FindFunc(canvas, "K2_DrawLine");
    g_CF.text    = FindFunc(canvas, "K2_DrawText");
    if (g_CF.project) { g_CF.pW = ParamOff(g_CF.project, "WorldLocation"); g_CF.pR = ParamOff(g_CF.project, "ReturnValue"); }
    if (g_CF.line) { g_CF.lA = ParamOff(g_CF.line, "ScreenPositionA"); g_CF.lB = ParamOff(g_CF.line, "ScreenPositionB"); g_CF.lT = ParamOff(g_CF.line, "Thickness"); g_CF.lC = ParamOff(g_CF.line, "RenderColor"); }
    if (g_CF.text)
    {
        void* t = g_CF.text;
        g_CF.tF = ParamOff(t, "RenderFont"); g_CF.tS = ParamOff(t, "RenderText"); g_CF.tP = ParamOff(t, "ScreenPosition");
        g_CF.tC = ParamOff(t, "RenderColor"); g_CF.tK = ParamOff(t, "Kerning"); g_CF.tSC = ParamOff(t, "ShadowColor");
        g_CF.tSO = ParamOff(t, "ShadowOffset"); g_CF.tCX = ParamOff(t, "bCentreX"); g_CF.tCY = ParamOff(t, "bCentreY");
        g_CF.tO = ParamOff(t, "bOutlined"); g_CF.tOC = ParamOff(t, "OutlineColor");
    }
    CoopLog("HUD marker: canvas fns project=%p(W%d R%d size%d) line=%p(A%d B%d T%d C%d size%d) text=%p(F%d S%d P%d C%d size%d)",
        g_CF.project, g_CF.pW, g_CF.pR, g_CF.project ? ParmsSize(g_CF.project) : 0,
        g_CF.line, g_CF.lA, g_CF.lB, g_CF.lT, g_CF.lC, g_CF.line ? ParmsSize(g_CF.line) : 0,
        g_CF.text, g_CF.tF, g_CF.tS, g_CF.tP, g_CF.tC, g_CF.text ? ParmsSize(g_CF.text) : 0);
    bool projOk = g_CF.project && g_CF.pW >= 0 && g_CF.pR >= 0 && ParmsSize(g_CF.project) <= 256;
    bool lineOk = g_CF.line && g_CF.lA >= 0 && g_CF.lB >= 0 && g_CF.lT >= 0 && g_CF.lC >= 0 && ParmsSize(g_CF.line) <= 256;
    bool textOk = g_CF.text && g_CF.tF >= 0 && g_CF.tS >= 0 && g_CF.tP >= 0 && g_CF.tC >= 0 && ParmsSize(g_CF.text) <= 256;
    if (!textOk) g_CF.text = nullptr;
    // K2_DrawPolygon for filled circles (optional - without it the circles are drawn as rings)
    g_CF.poly = FindFunc(canvas, "K2_DrawPolygon");
    if (g_CF.poly)
    {
        void* f = g_CF.poly;
        g_CF.gT = ParamOff(f, "RenderTexture"); g_CF.gP = ParamOff(f, "ScreenPosition"); g_CF.gR = ParamOff(f, "Radius");
        g_CF.gN = ParamOff(f, "NumberOfSides"); g_CF.gC = ParamOff(f, "RenderColor");
        if (g_CF.gT < 0 || g_CF.gP < 0 || g_CF.gR < 0 || g_CF.gN < 0 || g_CF.gC < 0 || ParmsSize(f) > 256) g_CF.poly = nullptr;
    }
    CoopLog("HUD: K2_DrawPolygon=%p (T%d P%d R%d N%d C%d)", g_CF.poly, g_CF.gT, g_CF.gP, g_CF.gR, g_CF.gN, g_CF.gC);
    if (projOk && lineOk) g_CF.state = 1;
    else CoopLog("HUD marker DISABLED: couldn't resolve the canvas functions");
}

// One-time scan of all objects for an engine font (Roboto) so we can draw the "P2" label.
static void FindMarkerFont()
{
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    void* anyFont = nullptr;
    ULONGLONG t0 = GetTickCount64();
    for (int i = 0; items && i < num; ++i)
    {
        void* o = *(void**)(items + (size_t)i * 24);
        if (!o) continue;
        char nm[64]; NameOf(o, nm, sizeof(nm));
        if (strncmp(nm, "Roboto", 6) != 0) continue;
        void* cls = *(void**)((unsigned char*)o + 0x10);
        if (!cls || !NameIs(cls, "Font")) continue;
        if (strcmp(nm, "Roboto") == 0) { g_MarkerFont = o; break; }
        if (!anyFont) anyFont = o;
    }
    if (!g_MarkerFont) g_MarkerFont = anyFont;
    char fn[64] = "none"; if (g_MarkerFont) NameOf(g_MarkerFont, fn, sizeof(fn));
    CoopLog("HUD marker: font = %s (scanned %d objects in %llu ms)", fn, num, (unsigned long long)(GetTickCount64() - t0));
}

static bool CanvasProject(void* canvas, const float w[3], float out[3])
{
    alignas(16) unsigned char buf[256] = {};
    memcpy(buf + g_CF.pW, w, 12);
    Orig_ProcessEvent(canvas, g_CF.project, buf);
    memcpy(out, buf + g_CF.pR, 12);
    return out[2] > 0.f; // Z = 0 means "behind the camera"
}
static void CanvasLine(void* canvas, float x1, float y1, float x2, float y2, float thick, const float col[4])
{
    alignas(16) unsigned char buf[256] = {};
    float a[2] = {x1, y1}, b[2] = {x2, y2};
    memcpy(buf + g_CF.lA, a, 8); memcpy(buf + g_CF.lB, b, 8);
    memcpy(buf + g_CF.lT, &thick, 4); memcpy(buf + g_CF.lC, col, 16);
    Orig_ProcessEvent(canvas, g_CF.line, buf);
}
// a filled rectangle. The engine draws a thick line with square ends that reach half its thickness past each end
// point (the controls list's black box stuck out left and right by half its height), so the line runs along the longer
// side, pulled in by half the thickness at each end.
static void CanvasBox(void* canvas, float x, float y, float w, float h, const float col[4])
{
    if (!(w > 0.f && h > 0.f)) return;
    float t = w < h ? w : h, len = (w < h ? h : w) - t;
    if (len < 0.5f) len = 0.5f;   // a square: a (nearly) zero-length line has no direction
    if (w >= h) CanvasLine(canvas, x + w * 0.5f - len * 0.5f, y + h * 0.5f, x + w * 0.5f + len * 0.5f, y + h * 0.5f, t, col);
    else        CanvasLine(canvas, x + w * 0.5f, y + h * 0.5f - len * 0.5f, x + w * 0.5f, y + h * 0.5f + len * 0.5f, t, col);
}
// (review C5): Canvas K2_DrawText, outlined in black - one body for CanvasText, CanvasTextL and SmText (marker/20a),
// which were copies of each other. centreX = centred across (CanvasTextL: left-aligned); always centred up/down.
static void CanvasTextF(void* canvas, void* font, const wchar_t* str, float x, float y, const float col[4], bool centreX)
{
    if (!g_CF.text || !font) return;
    alignas(16) unsigned char buf[256] = {};
    static const float black[4] = {0.f, 0.f, 0.f, 1.f};
    FStringRaw fs{ (wchar_t*)str, (int)wcslen(str) + 1, (int)wcslen(str) + 1 };
    float pos[2] = {x, y}, so[2] = {1.f, 1.f}, kern = 0.f;
    memcpy(buf + g_CF.tF, &font, 8);
    memcpy(buf + g_CF.tS, &fs, sizeof(fs));
    memcpy(buf + g_CF.tP, pos, 8);
    memcpy(buf + g_CF.tC, col, 16);
    if (g_CF.tK >= 0)  memcpy(buf + g_CF.tK, &kern, 4);
    if (g_CF.tSC >= 0) memcpy(buf + g_CF.tSC, black, 16);
    if (g_CF.tSO >= 0) memcpy(buf + g_CF.tSO, so, 8);
    if (g_CF.tCX >= 0 && centreX) buf[g_CF.tCX] = 1;
    if (g_CF.tCY >= 0) buf[g_CF.tCY] = 1;
    if (g_CF.tO >= 0)  buf[g_CF.tO] = 1;
    if (g_CF.tOC >= 0) memcpy(buf + g_CF.tOC, black, 16);
    Orig_ProcessEvent(canvas, g_CF.text, buf);
}
static void CanvasText(void* canvas, const wchar_t* str, float x, float y, const float col[4]) { CanvasTextF(canvas, g_MarkerFont, str, x, y, col, true); }
static void CanvasTextL(void* canvas, const wchar_t* str, float x, float y, const float col[4]) { CanvasTextF(canvas, g_MarkerFont, str, x, y, col, false); }   // left-aligned, vertically centred
// Line with a dark outline underneath, so it reads on bright and dark backgrounds.

static bool CameraView(float loc[3], float rot[3]);   // (defined in the v5.5 section)
// player 2's window camera this frame (set by PipTick) - for drawing their marker inside the window
static bool PipShowing();   // defined after pipVisible
static bool AnyWindowShowing();   // pip_hud/30 - player 2's or player 3's window
static void PipRect(float clipX, float clipY, float* x, float* y, float* w, float* h);
static float PipTanHalf(float rw, float rh);   // the window's horizontal half-angle (tall windows keep the 16:9 height)
static bool  g_EdgePip = false; static float g_EdgeRect[4] = {0, 0, 0, 0};   // edge pointer inside the window rect
static void PipCamAxes(float fwd[3], float right[3], float up[3])
{
    const float D = DEG_TO_RAD;
    float cp = cosf(g_Player->pipCamRot[0] * D), sp = sinf(g_Player->pipCamRot[0] * D), cy = cosf(g_Player->pipCamRot[1] * D), sy = sinf(g_Player->pipCamRot[1] * D);
    fwd[0] = cp * cy; fwd[1] = cp * sy; fwd[2] = sp;
    right[0] = -sy; right[1] = cy; right[2] = 0.f;
    up[0] = -sp * cy; up[1] = -sp * sy; up[2] = cp;
}
static bool PipProject(const float w[3], float rx, float ry, float rw, float rh, float o[3])
{
    float f[3], r[3], u[3]; PipCamAxes(f, r, u);
    float d[3] = {w[0] - g_Player->pipCamPos[0], w[1] - g_Player->pipCamPos[1], w[2] - g_Player->pipCamPos[2]};
    float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    float x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
    float y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    if (z < 1.f) { o[0] = rx + rw * 0.5f; o[1] = ry + rh * 0.5f; o[2] = 0.f; return false; }
    float th = PipTanHalf(rw, rh);   // (was the FOV setting as is)
    float s = (rw * 0.5f) / th;   // the capture FOV is horizontal, pixels are square
    o[0] = rx + rw * 0.5f + x / z * s; o[1] = ry + rh * 0.5f - y / z * s; o[2] = z;
    return true;
}
// player 2's target is off screen - small chevron at the screen edge pointing toward it,
// in the player's color (like the game's blue pointer for player 1's target).
static void DrawTargetEdgePointer(void* canvas, const float loc[3], bool front, const float scr[3], const float col[4])
{
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    float k = clipY / 1080.f, cx = clipX * 0.5f, cy = clipY * 0.5f;
    float halfW = cx, halfH = cy;
    if (g_EdgePip) { halfW = g_EdgeRect[2] * 0.5f; halfH = g_EdgeRect[3] * 0.5f; cx = g_EdgeRect[0] + halfW; cy = g_EdgeRect[1] + halfH; }
    float dx, dy;
    if (front) { dx = scr[0] - cx; dy = scr[1] - cy; }
    else if (g_EdgePip)
    {
        float f[3], r[3], u[3]; PipCamAxes(f, r, u);
        float d[3] = {loc[0] - g_Player->pipCamPos[0], loc[1] - g_Player->pipCamPos[1], loc[2] - g_Player->pipCamPos[2]};
        dx = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
        dy = -(d[0] * u[0] + d[1] * u[1] + d[2] * u[2]);
        if (fabsf(dx) < 1.f && fabsf(dy) < 1.f) dy = 1.f;
    }
    else
    {
        float camLoc[3], camRot[3];
        if (!CameraView(camLoc, camRot)) return;
        const float D = DEG_TO_RAD;
        float cp = cosf(camRot[0] * D), sp = sinf(camRot[0] * D), cyw = cosf(camRot[1] * D), syw = sinf(camRot[1] * D);
        float right[3] = {-syw, cyw, 0.f}, up[3] = {-sp * cyw, -sp * syw, cp};
        float d[3] = {loc[0] - camLoc[0], loc[1] - camLoc[1], loc[2] - camLoc[2]};
        dx = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
        dy = -(d[0] * up[0] + d[1] * up[1] + d[2] * up[2]);
        if (fabsf(dx) < 1.f && fabsf(dy) < 1.f) dy = 1.f;
    }
    float len = sqrtf(dx * dx + dy * dy); if (len < 0.001f) return;
    dx /= len; dy /= len;
    float inset = 30.f * k;
    float hx = halfW - inset, hy = halfH - inset;
    float tx = fabsf(dx) > 1e-4f ? hx / fabsf(dx) : 1e9f;
    float ty = fabsf(dy) > 1e-4f ? hy / fabsf(dy) : 1e9f;
    float t = tx < ty ? tx : ty;
    float x = cx + dx * t, y = cy + dy * t;
    float s = 11.f * k, w = 10.f * k, nx = -dy, ny = dx;
    float tipX = x + dx * s, tipY = y + dy * s;
    float b1x = x - dx * s * 0.4f + nx * w, b1y = y - dy * s * 0.4f + ny * w;
    float b2x = x - dx * s * 0.4f - nx * w, b2y = y - dy * s * 0.4f - ny * w;
    static const float dark[4] = {0.f, 0.f, 0.f, 0.8f};
    CanvasLine(canvas, b1x, b1y, tipX, tipY, 6.f * k, dark);
    CanvasLine(canvas, b2x, b2y, tipX, tipY, 6.f * k, dark);
    CanvasLine(canvas, b1x, b1y, tipX, tipY, 3.f * k, col);
    CanvasLine(canvas, b2x, b2y, tipX, tipY, 3.f * k, col);
    static ULONGLONG lastLog = 0;
    if (GetTickCount64() - lastLog > 5000) { lastLog = GetTickCount64(); CoopLog("HUD: P%d target off screen - edge pointer at (%.0f,%.0f), %s camera", g_Player->num, x, y, front ? "in front of" : "behind"); }
}

// enemy HP through the game's battle API (UEndBattleAPI::GetCharaHP / GetCharaHPMax, static)
static const wchar_t* EnemyNameOf(void* chara);   // (defined with the window code)
static bool EnemyHP(void* chara, int* hp, int* mx)
{
    static int st = 0; static void* cdo = nullptr; static void* fHp = nullptr, *fMax = nullptr; static int iC = -1, iR = -1, mC = -1, mR = -1;
    if (st == 0)
    {
        st = -1;
        cdo = FindObj("Default__EndBattleAPI", "EndBattleAPI");
        if (cdo) { fHp = FindFunc(cdo, "GetCharaHP"); fMax = FindFunc(cdo, "GetCharaHPMax"); }
        if (fHp) { iC = ParamOff(fHp, "InChara"); iR = ParamOff(fHp, "ReturnValue"); }
        if (fMax) { mC = ParamOff(fMax, "InChara"); mR = ParamOff(fMax, "ReturnValue"); }
        if (fHp && fMax && iC >= 0 && iR >= 0 && mC >= 0 && mR >= 0 && ParmsSize(fHp) <= 32 && ParmsSize(fMax) <= 32) st = 1;
        CoopLog("HUD: enemy HP via EndBattleAPI %s (GetCharaHP %p, GetCharaHPMax %p)", st == 1 ? "ready" : "NOT available", fHp, fMax);
    }
    if (st != 1 || !chara) return false;
    alignas(16) unsigned char b[32] = {};
    memcpy(b + iC, &chara, 8); Orig_ProcessEvent(cdo, fHp, b); *hp = *(int*)(b + iR);
    memset(b, 0, sizeof(b)); memcpy(b + mC, &chara, 8); Orig_ProcessEvent(cdo, fMax, b); *mx = *(int*)(b + mR);
    return *mx > 0 && *hp >= 0;
}
// what happened to each player's marker in the last 5 s (logged by MarkerStatsTick, the TARGETCOPY stats are gone)
struct MkSt { int calls = 0, noTgt = 0, off = 0, drawn = 0; };
static MkSt g_MkSt[2];
static void MarkerStatsTick()
{
    static ULONGLONG t0 = 0; ULONGLONG now = GetTickCount64();
    if (!t0) t0 = now;
    if (now - t0 < 5000) return;
    t0 = now;
    static LogRate logs;
    for (int i = 0; i < 2; ++i)
    {
        MkSt& m = g_MkSt[i];
        if (m.calls && LogOk(logs, 30)) { CoopLog("MARKER: player %d last 5 s - %d frame(s): no target %d, off screen %d, drawn %d", i + 2, m.calls, m.noTgt, m.off, m.drawn); }
        m = MkSt();
    }
}
static void* TsMenuTarget(int p);   // (menus/35b)
static void* TargetCharOf(void* ch, int slot);   // (menus/35b)
static bool GameLockedOn();                      // (menus/35b)
static bool TiDraw(int pn, void* tgt, bool locked, const float* mid, bool inPip);   // (pip_hud/36g)
static float TiSharedNudge(int pn);   // (pip_hud/36g)
static void DrawP2Marker(void* hud)
{
    MkSt* ms = (g_Player->num == 2 || g_Player->num == 3) ? &g_MkSt[g_Player->num - 2] : nullptr;
    if (ms && g_Player->joined) { ms->calls++; if (!g_Player->drawValid) ms->noTgt++; }
    void* selT = TsMenuTarget(g_Player->num);   // the target they are picking in their own menu
    bool lockedLook = selT ? false : g_Player->tgtLocked;
    if (!g_Cfg.showMarker || (!g_Player->drawValid && !selT) || !g_Player->joined || g_CF.state < 0 || GetCurrentThreadId() != g_GameThreadId) return;
    // f1.223 (the author's screenshot 20:52:21: "P2's lock on stayed on screen during the cutscene" - the Eligor fight): no
    // reticle / "P2" label in a cutscene or a scripted / cinematic camera shot (their reticle is put away 0.15 s later -
    // pip_hud/36g Hook_TiTick - and made again after it)
    if (g_InCutscene || g_ScriptedShot) return;
    void* canvas = Readable((unsigned char*)hud + OFF_HUD_CANVAS, 8) ? *(void**)((unsigned char*)hud + OFF_HUD_CANVAS) : nullptr;
    if (!canvas || !Readable(canvas, 0x40)) return;
    if (g_CF.state == 0) { ResolveCanvasFns(canvas); if (g_CF.state < 0) return; FindMarkerFont(); }

    void* tgt = selT ? selT : ResolveWeak(g_Player->tgtIdx, g_Player->tgtSerial);
    if (!tgt) return;
    float loc[3]; if (!ActorLoc(tgt, loc)) return;

    // styled after the game's own lock-on reticle (corner brackets + small angled ticks + a
    // centre diamond with a crosshair), in green with a "P2" tag so it never looks like player 1's.
    //   soft target : centre diamond + crosshair + "P2"
    //   locked (R3) : the same plus the corner brackets and ticks, label "P2 LOCK"
    float mid[3] = {loc[0], loc[1], loc[2]};
    // (the author, f1.185 test: "P1's sits slightly higher on the enemy"): the game puts player 1's reticle on the
    // target's aim point - its target object's vfunc +0x48 (0x994C10: the mesh socket the game aims at, 0x9B98A6) - not the
    // actor's location. Players 2/3's reticles use the same point now (the actor's location if it can't be had).
    {
        unsigned char* t = (unsigned char*)tgt;
        void* obj = Readable(t + 0xCB8, 16) ? *(void**)(t + 0xCB8) : nullptr;
        unsigned char* ctl = obj ? *(unsigned char**)(t + 0xCC0) : nullptr;
        void** vt = (obj && Readable(obj, 8)) ? *(void***)obj : nullptr;
        if (vt && ctl && Readable(ctl, 16) && *(int*)(ctl + 8) > 0 && Readable(vt + 9, 8) && (uintptr_t)vt[9] == g_Base + 0x994C10)   // only the known aim-point code
        {
            float aim[3] = {0, 0, 0};
            ((float* (*)(void*, float*))vt[9])(obj, aim);
            float dx = aim[0] - loc[0], dy = aim[1] - loc[1], dz = aim[2] - loc[2];
            if (aim[0] == aim[0] && aim[1] == aim[1] && aim[2] == aim[2] && dx * dx + dy * dy + dz * dz < 1000.f * 1000.f)
            { mid[0] = aim[0]; mid[1] = aim[1]; mid[2] = aim[2]; }
        }
    }
    float c[3];
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipY < 100.f) return;
    // player 2's window is showing -> their marker goes inside it, seen from their window camera
    bool inPip = (g_Player->num == 2 || g_Player->num == 3) && PipShowing() && g_Player->pipCamOk;
    float rx = 0.f, ry = 0.f, rw = clipX, rh = clipY;
    if (inPip) PipRect(clipX, clipY, &rx, &ry, &rw, &rh);
    auto Proj = [&](const float w[3], float o[3]) { return inPip ? PipProject(w, rx, ry, rw, rh, o) : CanvasProject(canvas, w, o); };
    bool front = Proj(mid, c);
    float k = clipY / 1080.f; // scale with resolution
    float cx = c[0], cy = c[1];
    if (!front || cx < rx || cy < ry || cx > rx + rw || cy > ry + rh)   // off screen -> edge pointer
    {
        if (ms) ms->off++;
        g_EdgePip = inPip; g_EdgeRect[0] = rx; g_EdgeRect[1] = ry; g_EdgeRect[2] = rw; g_EdgeRect[3] = rh;
        DrawTargetEdgePointer(canvas, mid, front, c, g_Player->color);
        g_EdgePip = false;
        return;
    }

    // (the drawn target bar (v11.6 - f1.168: HP, name, stagger, Staggered / Pressured, EnemyHPNumbers, EnemyNameSize) is
    //  gone - players 2/3 always get the game's own enemy bar, pip_hud/36f)
    // the GAME'S reticle in the player's color (pip_hud/36g), "P2" / "P3" above it
    TiDraw(g_Player->num, tgt, lockedLook, mid, inPip);
    if (ms) ms->drawn++;
    float nudge = inPip ? 0.f : TiSharedNudge(g_Player->num);   // the label moves with the reticle on the shared screen
    { wchar_t gl[16]; swprintf(gl, 16, L"P%d", g_Player->num); CanvasText(canvas, gl, cx, cy + nudge - 50.f * k - 6.f * k, g_Player->color); }
    float x = cx, y = cy;

    static bool loggedOnce = false;
    if (!loggedOnce) { loggedOnce = true; CoopLog("HUD marker: first draw at screen (%.0f, %.0f), canvas %.0fx%.0f", x, y, clipX, clipY); }
}


