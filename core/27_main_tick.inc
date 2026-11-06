static void StickAskLog();     // (pip_hud/30, f1.223)
static void DynQualityTick();   // (pip_hud/30b)
static void MonitorTick();     // (pip_hud/30c)
static void MonitorSwitchTick();   // (pip_hud/31b, f1.290)
static void PerspRegionsTick();   // (pip_hud/36h, f1.307)
static void In2Tick();   // the new input modules (input/40b_input2_glue.inc): seats
// each player's color from [PlayerN] Color (g_P2Cfg / g_P3Cfg)
// (the author: "P2 and P3 should be disallowed from selecting the same reticle color"): if both end up with the same
// color (a hand-edited ini - the settings app no longer allows it), player 3 gets the first other one: Orange, Green,
// Yellow, Purple, Pink, Red. Logged once per change.
static void ApplyPlayerColors()
{
    auto eff = [](int idx, int player) { return idx > 0 ? idx - 1 : (player == 3 ? 1 : 0); };
    int c2 = g_P2Cfg.color, c3 = g_P3Cfg.color;
    int e2 = eff(c2, 2), e3 = eff(c3, 3);
    static int lastClash = -1;
    if (e3 == e2)
    {
        static const int order[] = {1, 0, 2, 3, 4, 5};
        for (int k : order) if (k != e2) { e3 = k; break; }
        if (lastClash != e2) { lastClash = e2; CoopLog("SETTINGS: players 2 and 3 both have the color %ls - player 3 uses %ls instead", kPlayerColors[e2].name, kPlayerColors[e3].name); }
    }
    else lastClash = -1;
    auto set = [](float* d, int i) { d[0] = kPlayerColors[i].c[0]; d[1] = kPlayerColors[i].c[1]; d[2] = kPlayerColors[i].c[2]; };
    set(g_Player->color, g_P3Loaded ? e3 : e2);
    set(g_OtherPlayer->color, g_P3Loaded ? e2 : e3);
}
static void Hook_Tick(void* pcv, float dt)
{
    g_GameThreadId = GetCurrentThreadId();
    g_P1Pc = pcv;
    ApplyPlayerColors();
    TalkDiagTick(pcv);   // talking scenes (core/27a; only the field-controls signal is left)
    {   // frame rate from real time between ticks, smoothed
        static LARGE_INTEGER ticksPerSecond = {}, lastFrame = {};   // (finding #5: were "f" / "last", on one line with the read)
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (!ticksPerSecond.QuadPart) QueryPerformanceFrequency(&ticksPerSecond);
        if (lastFrame.QuadPart)
        {
            double s = (double)(now.QuadPart - lastFrame.QuadPart) / (double)ticksPerSecond.QuadPart;   // seconds since the last frame
            if (s > 0.0 && s < 1.0)
            {
                float inst = (float)(1.0 / s);
                g_Fps = g_Fps <= 0.f ? inst : g_Fps + (inst - g_Fps) * FPS_SMOOTHING;
            }
        }
        {   // PERF (the author: "the frame rates were getting pretty bad at times ... want to make sure there weren't other
            // factors"): every 30 s - average frame rate, the slowest frame, frames slower than 30 fps, and what was on
            // (split screen, battle, co-op players), so slow stretches can be matched with what the mod was doing.
            static double sum = 0.0, worst = 0.0;            // seconds: all frames, the slowest one
            static double menuSum = 0.0, menuWorst = 0.0;    // ... the frames with a command menu open
            static int frames = 0, slow = 0, splitF = 0, battleF = 0, menuF = 0;   // frame counts: all, slower than 30 fps, split screen on, battle, menu open
            static ULONGLONG t0 = 0;                         // start of this 30 s
            ULONGLONG ms = GetTickCount64();
            if (!t0) t0 = ms;
            if (lastFrame.QuadPart)
            {
                double s = (double)(now.QuadPart - lastFrame.QuadPart) / (double)ticksPerSecond.QuadPart;
                if (s > 0.0 && s < 5.0)
                {
                    sum += s;
                    ++frames;
                    if (s > worst) worst = s;
                    if (s > 1.0 / 30.0) ++slow;
                    if (PipShowing()) ++splitF;
                    if (InBattleNow()) ++battleF;
                    if (ms - g_MenuOpenTick < 150) { menuSum += s; ++menuF; if (s > menuWorst) menuWorst = s; }   // a command menu open
                }
            }
            if (ms - t0 >= 30000 && frames > 0)
            {
                char mb[96] = "";
                if (menuF > 0 && menuF < frames && menuSum > 0.0 && sum - menuSum > 0.0)
                    snprintf(mb, sizeof(mb), " | menus open %d%%: %.0f fps with, %.0f fps without (slowest %.0f ms with)", menuF * 100 / frames, menuF / menuSum, (frames - menuF) / (sum - menuSum), menuWorst * 1000.0);
                CoopLog("PERF: last 30 s - %.0f fps on average, slowest frame %.0f ms, %d of %d frames slower than 30 fps | split screen on %d%%, battle %d%%, players in: 2 %s, 3 %s%s%s",
                    frames / sum, worst * 1000.0, slow, frames, splitF * 100 / frames, battleF * 100 / frames, g_Player->joined ? "yes" : "no", g_P3Human ? "yes" : "no",
                    mb, g_GamePaused ? " (paused at the end)" : "");
                sum = worst = menuSum = menuWorst = 0.0; frames = slow = splitF = battleF = menuF = 0; t0 = ms;
            }
        }
        lastFrame = now;
    }
    WithP2 base;
    {   // normal frame: player 1 -> leader (the v7.1 / v7.9 / v13.9 "player 2 / 3 leads" stick paths are gone with
        // the KO hand-off, f1.126; player 2's stick values are still put back after the game's tick, as before)
        float rawX = g_Player->stickX, rawY = g_Player->stickY;
        Orig_Tick(pcv, dt);
        g_Player->stickX = rawX; g_Player->stickY = rawY;
    }
    KoKeepTick();   // test switch (seats/13, logs only)

    {   // pause screens / tutorials - stay out of the way
        bool paused = GamePausedNow(pcv);
        if (paused != g_GamePaused)
        {
            g_GamePaused = paused;
            CoopLog("PAUSE: game %s", paused ? "PAUSED - extra player passes stop, keyboard fully player 1's" : "running again");
        }
    }
    KeySnapTake();        // game window in front + Ctrl / Shift / Alt, read once for every hotkey this frame
    PollSwapKey();
    QuietLeadTick();      // f1.248: a quiet lead hand-off - hand back / end (seats/24b)
    GatePosTick();        // f1.249 (log only): GATEPOS (seats/13b)
    StoryShotTick();      // f1.253 (camera/38b)
    Ko2WatchTick();       // f1.253 (log only): KO2 (seats/24b)
    PollReloadKey();
    PollHelpKey();
    PipKeysTick();        // F7 / Ctrl+F7 once a frame (was inside each PipTick) - right after the key snapshot
    PlayerFrameInput();
    if (g_P3Enabled) { WithP3 w; PlayerFrameInput(); }
    LeaderSwitchWatch();   // the leader's D-pad switch (log + "no AI teammate" notice)
    RumbleTick();
    if (g_P3Enabled) { WithP3 w; RumbleTick(); }

    PollCameraToggleKey();
    P3WinUpdate();
    CmdMenuTick();
    In2Tick();             // the controller module: seats (input/40b)
    IconTick();            // players 2/3's own button icons (a refresh after their command systems come on / F6)
    AttackRateTick();      // EnemyAttackRate - classify the AI controllers the timer hook saw
    PipTick(pcv);          // picture-in-picture (does nothing unless it's switched on)
    if (g_P3Enabled) { WithP3 w; PipTick(pcv); }   // player 3's window
    PerspRegionsTick();    // f1.307: the players' parts for the tilted menus' eye (pip_hud/36h)
    DynQualityTick();      // dynamic window quality (pip_hud/30b)
    MonitorTick();         // multi-monitor research - MONITORS lines (pip_hud/30c, log only)
    MonitorSwitchTick();   // f1.290 multiple monitors (experimental, MultiMonitor=1): one monitor / all monitors
    SmoothSpritesTick(pcv);   // smooth badge / pointer shapes (marker/20a), built when needed
    ListenerTick(pcv);     // sound listener at the game's own camera
    NoteGameWindow();      // the game's window, once it has been in front (menus/08)

    if (g_GamePaused) return;   // nobody moves while the game is paused (the v9.7 mini-game stand-down is gone)
    void* human[2] = {nullptr, nullptr};
    int ty[2] = {-1, -1};
    human[0] = PlayerMovePass(pcv, dt);
    if (human[0]) ty[0] = g_Player->charType;
    if (g_P3Enabled) { WithP3 w; human[1] = PlayerMovePass(pcv, dt); if (human[1]) ty[1] = g_Player->charType; }
    g_P3HumanChar = human[1]; g_P3Human = human[1] != nullptr;
    SetHumanMasks(ty, 2);              // player rules for human-played characters
    AIGuardCopyTick(ty, 2);
    EnemyBalanceTick(human, 2);
    StickAskLog();   // f1.223 (pip_hud/30): STICK lines
}

