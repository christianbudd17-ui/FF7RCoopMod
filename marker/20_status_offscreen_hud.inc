// f1.310: player 1's part of the canvas while the game spans all monitors (pip_hud/31b) - x / width in canvas px;
// false = not spanning (the whole canvas)
static bool HudP1Part(float clipX, float clipY, float* x0, float* w);
// ---- v5.4: player status circles ----
// Two small circles in the top-right corner: P2 then P3. Green "P2-H" = a person is playing that
// character, red "P2-AI" = the game's AI is. Dimmed when nobody is in that party slot.
static void FindWhiteTexture(void* canvas)
{
    // UCanvas::DefaultTexture (+0x60) is the engine's plain white square; check it really is a texture.
    void* t = Readable((unsigned char*)canvas + 0x60, 8) ? *(void**)((unsigned char*)canvas + 0x60) : nullptr;
    char cls[64] = "";
    if (t && Readable(t, 0x20)) { void* c = *(void**)((unsigned char*)t + 0x10); if (c) NameOf(c, cls, sizeof(cls)); }
    if (t && strstr(cls, "Texture")) g_CF.white = t;
    else
    {
        unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
        int num = *(int*)(g_Base + RVA_GOBJ_NUM);
        for (int i = 0; items && i < num; ++i)
        {
            void* o = *(void**)(items + (size_t)i * 24);
            if (!o) continue;
            char nm[64]; NameOf(o, nm, sizeof(nm));
            if (strcmp(nm, "WhiteSquareTexture") == 0 || strcmp(nm, "DefaultWhiteTexture") == 0) { g_CF.white = o; break; }
        }
    }
    char nm[64] = "none"; if (g_CF.white) NameOf(g_CF.white, nm, sizeof(nm));
    CoopLog("HUD: fill texture = %s (DefaultTexture class '%s')", nm, cls);
}
static void CanvasDisc(void* canvas, float x, float y, float r, const float col[4])
{
    if (!g_CF.poly || !g_CF.white) return;
    alignas(16) unsigned char buf[256] = {};
    float pos[2] = {x, y}, rad[2] = {r, r}; int sides = 32;
    memcpy(buf + g_CF.gT, &g_CF.white, 8);
    memcpy(buf + g_CF.gP, pos, 8);
    memcpy(buf + g_CF.gR, rad, 8);
    memcpy(buf + g_CF.gN, &sides, 4);
    memcpy(buf + g_CF.gC, col, 16);
    Orig_ProcessEvent(canvas, g_CF.poly, buf);
}
static void CanvasRing(void* canvas, float x, float y, float r, float th, const float col[4], int seg = 28)
{
    float px = x + r, py = y;
    for (int i = 1; i <= seg; ++i)
    {
        float a = 6.2831853f * i / seg;
        float nx = x + r * cosf(a), ny = y + r * sinf(a);
        CanvasLine(canvas, px, py, nx, ny, th, col);
        px = nx; py = ny;
    }
}
// the P2 / P3 badges (the author 2026-10-20: "Let's go with the diamond badges", AI in red, faded gray for no
// character) - replaced v5.4's green / red circles (DrawStatusCircle). A diamond with a dark see-through fill, an outline,
// a faint inner line, "P2" / "P3" in the middle and a small word under it:
//   a person plays it -> outline + "PLAYER" in that player's color (P2 green, P3 orange)
//   the AI plays it   -> outline + "AI" in red
//   nobody in that party slot -> everything faded gray, word "-"
// Smooth sprites (marker/20a) when ready, else the diamond as a 4-sided K2_DrawPolygon + lines.
struct BadgeLayout { float k, R, t, y, x2, x3, bottom, left; };
static void GetBadgeLayout(float clipX, float k, BadgeLayout* L)
{
    L->k = k; SmBadgeSize(k, g_Cfg.statusSize, &L->R, &L->t);
    float word = 52.f;                                   // "PLAYER" in the small font, roughly (pixels - that font doesn't scale)
    float half = L->R > word * 0.5f ? L->R : word * 0.5f;
    float gap = 10.f * k * g_Cfg.statusSize / 0.5f;
    L->y = g_Cfg.statusTop * k + L->R;
    L->x3 = clipX - g_Cfg.statusRight * k - half;           // P3 = the right-hand badge
    L->x2 = L->x3 - 2.f * half - gap;                       // P2 = to its left
    L->bottom = L->y + L->R + 6.f * k + 16.f;               // under the word
    L->left = L->x2 - half;
}
static void DrawBadge(void* canvas, const BadgeLayout& L, float x, const wchar_t* who, int kind, const float pcol[4], bool smooth)
{   // kind: 1 = a person, 0 = the AI, -1 = no character
    static const float red[4] = {1.f, 0.25f, 0.22f, 0.95f};
    static const float gray[4] = {0.55f, 0.57f, 0.60f, 0.55f};
    const float* oc = kind > 0 ? pcol : kind == 0 ? red : gray;
    float fill[4] = {8.f / 255.f, 14.f / 255.f, 24.f / 255.f, kind < 0 ? 0.45f : 0.80f};
    float inner[4] = {1.f, 1.f, 1.f, kind < 0 ? 0.10f : 0.22f};
    float txt[4] = {1.f, 1.f, 1.f, 0.95f};
    if (kind < 0) memcpy(txt, gray, 16);
    float y = L.y, R = L.R;
    if (smooth)
    {
        SmDraw(canvas, SPR_DFILL, x, y, fill);
        SmDraw(canvas, SPR_DINNER, x, y, inner);
        SmDraw(canvas, SPR_DOUT, x, y, oc);
    }
    else
    {
        if (g_CF.poly && g_CF.white)
        {   // K2_DrawPolygon with 4 sides starts at angle 0 = a diamond
            alignas(16) unsigned char buf[256] = {};
            float pos[2] = {x, y}, rad[2] = {R, R}; int sides = 4;
            memcpy(buf + g_CF.gT, &g_CF.white, 8); memcpy(buf + g_CF.gP, pos, 8); memcpy(buf + g_CF.gR, rad, 8);
            memcpy(buf + g_CF.gN, &sides, 4); memcpy(buf + g_CF.gC, fill, 16);
            Orig_ProcessEvent(canvas, g_CF.poly, buf);
        }
        float px[4] = {x + R, x, x - R, x}, py[4] = {y, y + R, y, y - R};
        for (int i = 0; i < 4; ++i) CanvasLine(canvas, px[i], py[i], px[(i + 1) % 4], py[(i + 1) % 4], L.t, oc);
    }
    CanvasText(canvas, who, x, y, txt);
    const wchar_t* word = kind > 0 ? L"PLAYER" : kind == 0 ? L"AI" : L"-";
    SmText(canvas, g_SmallFont ? g_SmallFont : g_MarkerFont, word, x, y + R + 6.f * L.k + 7.f, oc);
}
static void DrawStatusCircles(void* canvas)
{
    if (!g_Cfg.showStatus || g_InCutscene) return;
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    float k = clipY / 1080.f;
    BadgeLayout L; GetBadgeLayout(clipX, k, &L);
    bool smooth = SmReady(canvas, k);
    int n = PartyCount();
    void* p2 = P2Char();
    bool p2Human = g_Player->joined && p2 != nullptr;
    DrawBadge(canvas, L, L.x2, L"P2", n < 2 ? -1 : p2Human ? 1 : 0, g_Player->color, smooth);   // player 2's color (was fixed green)
    DrawBadge(canvas, L, L.x3, L"P3", n < 3 ? -1 : g_P3Human ? 1 : 0, g_OtherPlayer->color, smooth);   // (drawn in player 2's state: player 3's color is the swapped copy)
    static int lastSmooth = -1;
    if ((int)smooth != lastSmooth)
    {
        lastSmooth = smooth;
        CoopLog("HUD: P2 / P3 badges at (%.0f,%.0f) & (%.0f,%.0f), diamond %.0f px, canvas %.0fx%.0f - %s",
            L.x2, L.y, L.x3, L.y, L.R, clipX, clipY, smooth ? "smooth" : "line drawing");
    }
}

// ---- v5.5: off-screen arrow for player 2 ----
// The camera manager remembers itself here each frame (Hook_UpdateCamera) so the HUD can read the
// final camera position/rotation (CameraCache POV: location +0x400, rotation +0x40C).
static unsigned char* volatile g_LastPcm = nullptr;
static volatile ULONGLONG g_LastPcmTick = 0;
static bool CameraView(float loc[3], float rot[3])
{
    unsigned char* pcm = g_LastPcm;
    if (!pcm || GetTickCount64() - g_LastPcmTick > 500 || !Readable(pcm + 0x400, 0x18)) return false;
    memcpy(loc, pcm + 0x400, 12); memcpy(rot, pcm + 0x40C, 12);
    return true;
}
// Circle at the screen edge in the direction of the character, arrow inside = the way they face
// (seen from above, turned to match the camera: arrow up = facing away from the camera).
static void DrawOffscreenArrow(void* canvas, void* who, const wchar_t* label, const float col[4])
{
    if (!who) return;
    float loc[3]; if (!ActorLoc(who, loc)) return;
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    float k = clipY / 1080.f;
    float px0 = 0.f, pw = clipX; HudP1Part(clipX, clipY, &px0, &pw);   // f1.310: player 1's monitor while spanning
    float cx = px0 + pw * 0.5f, cy = clipY * 0.5f;

    float scr[3];
    bool front = CanvasProject(canvas, loc, scr);
    float m = 10.f * k;   // a little slack before we call it "off screen"
    if (front && scr[0] > px0 - m && scr[0] < px0 + pw + m && scr[1] > -m && scr[1] < clipY + m) return;   // visible

    float camLoc[3], camRot[3];
    if (!CameraView(camLoc, camRot)) return;
    const float D = DEG_TO_RAD;
    float cp = cosf(camRot[0] * D), sp = sinf(camRot[0] * D), cyw = cosf(camRot[1] * D), syw = sinf(camRot[1] * D);
    float fwd[3] = {cp * cyw, cp * syw, sp}, right[3] = {-syw, cyw, 0.f}, up[3] = {-sp * cyw, -sp * syw, cp};
    float d[3] = {loc[0] - camLoc[0], loc[1] - camLoc[1], loc[2] - camLoc[2]};
    float dx, dy;   // screen direction from the centre (y down)
    if (front) { dx = scr[0] - cx; dy = scr[1] - cy; }
    else
    {
        dx = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
        dy = -(d[0] * up[0] + d[1] * up[1] + d[2] * up[2]);
        // behind the camera: if they're nearly straight behind, put it at the bottom edge
        if (fabsf(dx) < 1.f && fabsf(dy) < 1.f) dy = 1.f;
        float depth = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
        (void)depth;
    }
    float len = sqrtf(dx * dx + dy * dy); if (len < 0.001f) return;
    dx /= len; dy /= len;

    // where that direction leaves the screen, pulled in so the whole circle shows
    float r = 30.f * k, inset = r + 18.f * k;
    float hx = pw * 0.5f - inset, hy = cy - inset;
    float tx = fabsf(dx) > 1e-4f ? hx / fabsf(dx) : 1e9f;
    float ty = fabsf(dy) > 1e-4f ? hy / fabsf(dy) : 1e9f;
    float t = tx < ty ? tx : ty;
    float x = cx + dx * t, y = cy + dy * t;
    // keep clear of the status circles in the top-right
    if (g_Cfg.showStatus)
    {   // the badges' real box (diamonds + the word under them)
        BadgeLayout bl; GetBadgeLayout(clipX, k, &bl);
        float topBand = bl.bottom + 12.f * k, leftEdge = bl.left - 20.f * k;
        if (y - r < topBand && x + r > leftEdge) y = topBand + r;
    }

    static const float dark[4] = {0.f, 0.f, 0.f, 0.55f};
    static const float black[4] = {0.f, 0.f, 0.f, 0.85f};
    float rel = (ActorYaw(who) - camRot[1]) * D;   // facing: character yaw relative to camera yaw. 0 = facing away (up), 90 = right.
    if (SmReady(canvas, k))
    {   // the same design from smooth sprites; the rim pointer and the arrow are drawn pointing up and turned
        SmDraw(canvas, SPR_PDISC, x, y, dark);
        SmDraw(canvas, SPR_PRINGD, x, y, black);
        SmDraw(canvas, SPR_PRING, x, y, col);
        SmDraw(canvas, SPR_PRIM, x, y, col, atan2f(dx, -dy) / D);
        SmDraw(canvas, SPR_PARROW, x, y, col, rel / D);
    }
    else
    {
    CanvasDisc(canvas, x, y, r, dark);
    CanvasRing(canvas, x, y, r + 1.5f * k, 4.f * k, black);
    CanvasRing(canvas, x, y, r, 2.5f * k, col);
    // small pointer on the rim, toward the character
    float px = x + dx * (r + 9.f * k), py = y + dy * (r + 9.f * k);
    float nx = -dy, ny = dx;
    CanvasLine(canvas, px, py, x + dx * r + nx * 6.f * k, y + dy * r + ny * 6.f * k, 2.5f * k, col);
    CanvasLine(canvas, px, py, x + dx * r - nx * 6.f * k, y + dy * r - ny * 6.f * k, 2.5f * k, col);

    float ax = sinf(rel), ay = -cosf(rel);
    float L = r * 0.62f, hw = r * 0.30f;
    float tipX = x + ax * L, tipY = y + ay * L, tailX = x - ax * L, tailY = y - ay * L;
    float bx = x + ax * (L - hw * 1.3f), by = y + ay * (L - hw * 1.3f);
    float qx = -ay, qy = ax;
    float th = 3.f * k;
    CanvasLine(canvas, tailX, tailY, bx, by, th, col);
    CanvasLine(canvas, tipX, tipY, bx + qx * hw, by + qy * hw, th, col);
    CanvasLine(canvas, tipX, tipY, bx - qx * hw, by - qy * hw, th, col);
    CanvasLine(canvas, bx + qx * hw, by + qy * hw, bx - qx * hw, by - qy * hw, th, col);
    }
    // label on the inside of the circle (toward the middle of the screen)
    CanvasText(canvas, label, x - dx * (r + 16.f * k), y - dy * (r + 16.f * k), col);

    static ULONGLONG lastLog = 0;
    if (GetTickCount64() - lastLog > 5000) { lastLog = GetTickCount64(); CoopLog("HUD: %ls off screen - edge circle at (%.0f,%.0f), %s camera, facing %.0f deg vs camera", label, x, y, front ? "in front of" : "behind", rel / D); }
}
static void DrawHelp(void* canvas);   // (defined with the key names)
static void DrawNotice(void* canvas)
{
    if (!g_Notice[0] || GetTickCount64() - g_NoticeAt > 4000) return;
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    float a = 1.f; ULONGLONG age = GetTickCount64() - g_NoticeAt; if (age > 3000) a = 1.f - (age - 3000) / 1000.f;
    float col[4] = {1.f, 0.95f, 0.6f, a};
    { float px0 = 0.f, pw = clipX; HudP1Part(clipX, clipY, &px0, &pw); CanvasText(canvas, g_Notice, px0 + pw * 0.5f, clipY * 0.16f, col); }   // f1.310: on player 1's monitor
}
// Everything the mod draws on the HUD, once per frame after the game's own HUD.
static void DrawCoopHud(void* hud)
{
    if (g_CF.state < 0 || GetCurrentThreadId() != g_GameThreadId) return;
    WithP2 base;
    void* canvas = Readable((unsigned char*)hud + OFF_HUD_CANVAS, 8) ? *(void**)((unsigned char*)hud + OFF_HUD_CANVAS) : nullptr;
    if (!canvas || !Readable(canvas, 0x70)) return;
    if (g_CF.state == 0)
    {
        ResolveCanvasFns(canvas); if (g_CF.state < 0) return; FindMarkerFont(); FindWhiteTexture(canvas);
        g_SmallFont = FindObj("RobotoTiny", "Font");   // the word under the badges
        CoopLog("HUD: small font %s", g_SmallFont ? "RobotoTiny" : "not found - the big one is used");
    }
    {   // (finding #35): hooks that couldn't go in (the game's bytes differ - usually a game update) - told on screen once,
        // 5 s into the first time there is a party (not on the title screen / while loading); the details are in the log
        static ULONGLONG partySince = 0; static bool told = false;
        if (g_HooksFailed > 0 && !told && GetPartyMember)
        {
            ULONGLONG now = GetTickCount64();
            if (PartyCount() < 1) partySince = 0;
            else if (!partySince) partySince = now;
            else if (now - partySince > 5000)
            {
                told = true;
                wchar_t msg[160]; swprintf(msg, 160, L"Co-op mod: %d part(s) didn't start (game updated?) - see FF7RCoopMod_Coop.log", g_HooksFailed);
                HudNotice(msg);
                CoopLog("HUD: told the player that %d hook(s) / patch(es) failed (HOOK FAILED lines above)", g_HooksFailed);
            }
        }
    }
    // PipDraw now runs before the game's HUD (PipDrawUnderHud)
    DrawNotice(canvas);   // join / leave / controller messages
    DrawHelp(canvas);     // controls overlay
    if (g_Cfg.showFps && g_Fps > 0.f)
    {
        float cX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), cY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
        static wchar_t txt[32]; static ULONGLONG lastT = 0; ULONGLONG nowT = GetTickCount64();
        if (nowT - lastT > 250) { lastT = nowT; swprintf(txt, 32, L"%.0f FPS", g_Fps); }
        static const float col[4] = {1.f, 1.f, 1.f, 0.9f};
        float px0 = 0.f, pw = cX; HudP1Part(cX, cY, &px0, &pw);   // f1.310 (the author: "I can't see the FPS counter or timer"): on player 1's monitor while spanning
        CanvasText(canvas, txt, px0 + pw * 0.5f, 14.f * (cY / 1080.f), col);
    }
    if (g_Cfg.showClock)
    {   // (the author: "make a clock active that I can see ... a toggle in the troubleshooting part of the settings"):
        // the PC's local time, the same clock the log lines use, top right. Drawn with the rest of the mod's HUD, so it
        // shows wherever the game draws its HUD.
        float cX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), cY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
        if (cX >= 200.f && cY >= 100.f)
        {
            SYSTEMTIME t; GetLocalTime(&t);
            wchar_t txt[16]; swprintf(txt, 16, L"%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
            static const float col[4] = {1.f, 1.f, 1.f, 0.9f};
            float k = cY / 1080.f;
            // (the author, f1.118 test: "Move the clock to the left a bit, it is underneath the diamonds and I can't see
            // it well"): f1.118 had it at 90 x 20 px in from the corner = under the P2 / P3 badges (1820 / 1882, 38). Now on
            // the badges' row, just left of them (their layout follows StatusSize / StatusTop / StatusRight).
            BadgeLayout L; GetBadgeLayout(cX, k, &L);
            float px0 = 0.f, pw = cX;
            if (HudP1Part(cX, cY, &px0, &pw)) CanvasText(canvas, txt, px0 + pw - 70.f * k, L.y, col);   // f1.310: top right of player 1's monitor while spanning
            else CanvasText(canvas, txt, L.left - 60.f * k, L.y, col);
        }
    }
    DrawStatusCircles(canvas);
    bool singleCam = !AnyWindowShowing();   // any window (was player 2's only)   // v12.8 (the author): player pointers only in single-camera mode
    if (g_Cfg.showOffscreen && g_Player->joined && !g_InCutscene && singleCam)
    {
        DrawOffscreenArrow(canvas, P2Char(), L"P2", g_Player->color);   // player 2's color (was fixed green)
    }
    if (g_Cfg.showOffscreen && !g_InCutscene && singleCam && (g_Player->joined || g_P3Human))
    {   // player 1's pointer too (the shared camera can leave player 1 behind)
        void* leader = LeaderChar();   // (L3): the character player 1 plays
        static const float blue[4] = {0.40f, 0.70f, 1.00f, 0.95f};
        if (leader) DrawOffscreenArrow(canvas, leader, L"P1", blue);
    }
    DrawP2Marker(hud);
    if (g_P3Enabled)   // player 3's marker and off-screen arrow
    {
        WithP3 w;
        if (g_Cfg.showOffscreen && g_Player->joined && !g_InCutscene && g_P3Human && singleCam) DrawOffscreenArrow(canvas, P2Char(), L"P3", g_Player->color);
        DrawP2Marker(hud);
    }
    MarkerStatsTick();   // (was in BtFrameEnd)
}

static int PropOff(void* cls, const char* name);   // (defined in the v5.6 section)

