// (the author): player 1's off-screen pointer inside player 2/3's window - the same circle as the
// P2 / P3 pointers (direction on the rim, facing arrow inside), at the edge of the window, when the
// leader's character isn't in that window's view. Runs inside PipDraw (the window's camera is loaded).
static void PipDraw(void* canvas)
{
    {   // keep the widget search going (main menu check) even with the HUD copy off
        static ULONGLONG last = 0; ULONGLONG now = GetTickCount64();
        if (now - last >= 120) { last = now; RootWidgetScan(); }
        if (g_MainMenuUp || g_GamePaused) return;   // paused too
    }
    if (!g_Player->pipVisible && !g_InCutscene && g_Player->ownBcmOn)
    {   // (the author): no split-screen window - players 2/3's own menus go along the bottom of the main screen
        float cx = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), cy = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
        if (cx >= 200.f && cy >= 100.f) Bcm2IntoWindow(canvas, cx, cy, true);
    }
    if (!g_Player->pipVisible || g_InCutscene || g_ScriptedShot || g_Pip.state != 1 || g_Pip.drawState < 0) return;
    void* rt = ResolveWeak(g_Player->pipRtIdx, g_Player->pipRtSer);
    if (!rt || !ResolveWeak(g_Player->pipActIdx, g_Player->pipActSer)) return;
    if (g_Pip.drawState == 0)
    {
        g_Pip.drawState = -1;
        void* f = FindFunc(canvas, "K2_DrawTexture");
        if (f)
        {
            g_Pip.dT = ParamOff(f, "RenderTexture"); g_Pip.dP = ParamOff(f, "ScreenPosition"); g_Pip.dS = ParamOff(f, "ScreenSize");
            g_Pip.dCP = ParamOff(f, "CoordinatePosition"); g_Pip.dCS = ParamOff(f, "CoordinateSize"); g_Pip.dC = ParamOff(f, "RenderColor");
            g_Pip.dB = ParamOff(f, "BlendMode"); g_Pip.dR = ParamOff(f, "Rotation"); g_Pip.dPv = ParamOff(f, "PivotPoint");
            if (g_Pip.dT >= 0 && g_Pip.dP >= 0 && g_Pip.dS >= 0 && g_Pip.dCS >= 0 && g_Pip.dC >= 0 && ParmsSize(f) <= 256) { g_Pip.fDraw = f; g_Pip.drawState = 1; }
        }
        CoopLog("PIP: K2_DrawTexture %p (T%d P%d S%d CP%d CS%d C%d B%d R%d Pv%d) %s", f, g_Pip.dT, g_Pip.dP, g_Pip.dS, g_Pip.dCP, g_Pip.dCS,
            g_Pip.dC, g_Pip.dB, g_Pip.dR, g_Pip.dPv, g_Pip.drawState == 1 ? "ok" : "- can't draw the window");
        if (g_Pip.drawState < 0) return;
    }
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    float k = clipY / 1080.f;
    g_PipCanvasW = clipX; g_PipCanvasH = clipY;
    float x, y, w, h; PipRect(clipX, clipY, &x, &y, &w, &h);

    struct R { float x1, y1, x2, y2; };
    R pieces[1] = {{x, y, x + w, y + h}}; int np = 1;
    for (int i = 0; i < np; ++i)
    {
        const R& p = pieces[i];
        if (p.x2 - p.x1 < 1.f || p.y2 - p.y1 < 1.f) continue;
        alignas(16) unsigned char b[256] = {};
        float pos[2] = {p.x1, p.y1}, size[2] = {p.x2 - p.x1, p.y2 - p.y1};
        float cp[2] = {p.x1 / clipX, p.y1 / clipY}, cs[2] = {(p.x2 - p.x1) / clipX, (p.y2 - p.y1) / clipY};   // the window's part of the screen-sized picture
        if (PipWindowShaped()) { cp[0] = cp[1] = 0.f; cs[0] = cs[1] = 1.f; }   // f1.288: the picture IS the window
        // GammaFix draws it a second time with Modulate blending (screen = picture x picture), which undoes the
        // extra brightness correction (approx. gamma 2). f1.54: always on, brightness always 100% (no longer settings)
        float col[4] = {1.f, 1.f, 1.f, 1.f}, pv[2] = {0.5f, 0.5f}, r0 = 0.f;
        memcpy(b + g_Pip.dT, &rt, 8); memcpy(b + g_Pip.dP, pos, 8); memcpy(b + g_Pip.dS, size, 8);
        if (g_Pip.dCP >= 0) memcpy(b + g_Pip.dCP, cp, 8);
        memcpy(b + g_Pip.dCS, cs, 8); memcpy(b + g_Pip.dC, col, 16);
        if (g_Pip.dB >= 0) b[g_Pip.dB] = 0;   // BLEND_Opaque
        if (g_Pip.dR >= 0) memcpy(b + g_Pip.dR, &r0, 4);
        if (g_Pip.dPv >= 0) memcpy(b + g_Pip.dPv, pv, 8);
        alignas(16) unsigned char b2[256]; memcpy(b2, b, sizeof(b2));
        Orig_ProcessEvent(canvas, g_Pip.fDraw, b);
        if (g_Pip.dB >= 0)
        {
            float one[4] = {1.f, 1.f, 1.f, 1.f};
            memcpy(b2 + g_Pip.dC, one, 16);
            b2[g_Pip.dB] = 4;                   // BLEND_Modulate: screen = screen x picture
            Orig_ProcessEvent(canvas, g_Pip.fDraw, b2);
        }
    }
    MenuDimView(canvas, g_Player->num, x, y, w, h);   // f1.208: their window darkens while their command menu is open (menus/39)
    {   // this player's own near-death red edge in their window (DangerPerScreen)
        bool on = g_Cfg.dangerPerScreen && CharInDanger(P2Char());
        DangerLog(g_Player->num, on);
        if (on) DangerEdge(canvas, x, y, w, h);
    }
    Bcm2IntoWindow(canvas, clipX, clipY);
    PartyPanelIntoWindow(canvas, clipX, clipY);   // PartyPanel=Individual (pip_hud/36b)
    if (g_Player->num == 2) EnemyNameLearn(canvas);   // (the stagger learning for the drawn bar is gone)

    // frame + label
    const float* green = g_Player->color;   // the player's own color (P2 green, P3 orange)
    static const float dark[4] = {0.f, 0.f, 0.f, 0.8f};
    float t = 2.f * k;
    auto E = [&](float x1, float y1, float x2, float y2) { CanvasLine(canvas, x1, y1, x2, y2, t + 3.f * k, dark); CanvasLine(canvas, x1, y1, x2, y2, t, green); };
    E(x, y, x + w, y); E(x + w, y, x + w, y + h); E(x + w, y + h, x, y + h); E(x, y + h, x, y);
    CanvasText(canvas, g_Player->num == 3 ? L"P3" : L"P2", x + 22.f * k, y + 16.f * k, green);
    static bool logged = false;
    if (!logged) { logged = true; CoopLog("PIP: first window drawn at (%.0f,%.0f) size %.0fx%.0f", x, y, w, h); }
}

static void CmdMenuP1Place(void* canvas);   // menus/32
static void CmdMenuP1Scale();               // menus/32
static void LensSelfCheck(void* canvas);    // camera/38
static void PipDrawUnderHud(void* hud)
{
    if (g_CF.state != 1 || GetCurrentThreadId() != g_GameThreadId) return;
    WithP2 base;
    g_HcHud = hud;
    void* canvas = Readable((unsigned char*)hud + OFF_HUD_CANVAS, 8) ? *(void**)((unsigned char*)hud + OFF_HUD_CANVAS) : nullptr;
    if (!canvas || !Readable(canvas, 0x70)) return;
    PipDraw(canvas);
    PartyOwnTidy();   // player 2's own party panel hidden again when it isn't wanted (pip_hud/36b)
    OwnCmdHudCutscene();   // f1.209: player 2's command HUD hidden during a cutscene (menus/10)
    if (g_P3Enabled) { WithP3 w; PipDraw(canvas); PartyOwnTidy(); OwnCmdHudCutscene(); }   // player 3's window (+ their panel tidy, command HUD in cutscenes)
    MenuDimP1(canvas);  // f1.208: player 1's part darkens while their command menu is open (menus/39)
    DangerP1(canvas);   // player 1's near-death red edge in their part of the screen (DangerPerScreen)
    HideP1ReticleInWindows(canvas);   // (menus/35b)
    LensSelfCheck(canvas);
    PartyPanelP1Place(canvas);   // PartyPanel=Individual - the game's panel into player 1's part (pip_hud/36b)
    CmdMenuP1Place(canvas);   // player 1's menu into player 1's part of the screen (players 2/3 on the left / at the bottom)
    MessageLogPlace(canvas);  // the battle messages inside player 1's part + MessageLogX / MessageLogY (pip_hud/36c)
    CmdMenuP1Scale();         // player 1's command HUD / menus size for player 1's part (after the move: it measures last frame's size)
}
static void Hook_UpdateCamera_Impl(void* pcmv, float dt);
static float g_GameCamLoc[3], g_GameCamRot[3];   // the game's own camera this frame (before our changes)
static volatile bool g_CamMoved = false;           // we moved the camera this frame
