// player 1's split-screen view through an OFF-CENTRE LENS (FMinimalViewInfo::OffCenterProjectionOffset, in the
// camera cache POV at pcm+0x400 - the engine shifts the projection, ndc.x = x*a/z - offset.x). The direction the camera
// faces then lands in the middle of player 1's part instead of the screen's - no turn needed, and the part isn't squeezed
// at the edge of a wide lens (a third at the far left only gained ~11 deg however wide the FOV, f1.82 test). Offset found
// by reflection (ScriptStruct MinimalViewInfo, FOV must be at +0x18 = the known +0x418). Self-check: a few frames after
// the lens is first on, the straight-ahead point is projected on the HUD canvas - mirrored = the sign flips, otherwise
// wrong 3 times = the lens is dropped (the turn method takes over). [Camera] SplitLens=0 = always the turn method.
static int g_LensOff = -2;                 // the offset's place in the POV, -1 = not available
static bool g_LensSet = false;             // we set an offset on an earlier frame
static void LensClear(unsigned char* pcm)  // lens not used this frame -> make sure no offset is left behind
{
    if (!g_LensSet) return;
    g_LensSet = false;
    if (pcm && g_LensOff > 0 && Readable(pcm + 0x400 + g_LensOff, 8)) { float* xy = (float*)(pcm + 0x400 + g_LensOff); xy[0] = xy[1] = 0.f; }
}
static float g_LensSign = 1.f;
static int g_LensChecks = 0;               // 100 = checked OK
static bool g_LensChkArm = false, g_LensChkOn = false;
static float g_LensChkAxis = 0.f, g_LensChkWant = 0.f, g_LensChkPt[3];
static bool LensResolve()
{
    if (g_LensOff != -2) return g_LensOff >= 0;
    g_LensOff = -1;
    void* st = FindObj("MinimalViewInfo", "ScriptStruct");
    int fovOff = st ? PropOff(st, "FOV") : -1, o = st ? PropOff(st, "OffCenterProjectionOffset") : -1;
    if (st && fovOff == 0x18 && o > 0x18 && o < 0xA00) g_LensOff = o;
    CoopLog("CAMERA LENS: MinimalViewInfo %p, FOV +0x%X, OffCenterProjectionOffset +0x%X - %s", st, fovOff, o,
        g_LensOff >= 0 ? "player 1's split-screen view uses an off-centre lens" : "not available - player 1's view is turned instead");
    return g_LensOff >= 0;
}
static int SplitSide(); static float SplitP1Frac();   // pip_hud/30
// f1.210: a far-away game camera in a battle (a grab) with the split screen showing - the game's shot moved into player 1's
// part of the screen without turning it. true = done (the lens stays set: g_LensSet), false = nothing to do (lens cleared).
static bool GrabShotIntoP1Part(unsigned char* pcm, float fov, float aspect)
{
    if (!(fov > 10.f && fov < 170.f)) fov = 90.f;
    if (!(aspect > 0.5f && aspect < 4.f)) aspect = 16.f / 9.f;
    int side = SplitSide();
    static bool logged = false;
    if (side >= 2)
    {   // windows at the bottom / top: the same small look-down / look-up as normal split-screen play
        float th = tanf(fov * 0.5f * DEG_TO_RAD);
        float dv = atanf(0.5f * th / aspect) * RAD_TO_DEG;
        ((float*)(pcm + 0x40C))[0] += side == 3 ? -dv : dv;
        LensClear(pcm);
        if (!logged) { logged = true; CoopLog("CAMERA: far-away game shot in a battle (grab) - tilted into player 1's half (windows at the %s)", side == 3 ? "top" : "bottom"); }
        return true;
    }
    if (!g_Cfg.camSplitLens || !LensResolve() || !Readable(pcm + 0x400 + g_LensOff, 8)) return false;
    float axis = SplitP1Axis();   // (f1.290: from the monitors layout too)
    float* lensXY = (float*)(pcm + 0x400 + g_LensOff);
    lensXY[0] = -axis * g_LensSign; lensXY[1] = 0.f;
    g_LensSet = true;
    if (!logged) { logged = true; CoopLog("CAMERA: far-away game shot in a battle (grab) - its centre moved into player 1's part with the off-centre lens"); }
    return true;
}
static void LensSelfCheck(void* canvas)   // HUD draw (pip_hud/37), the same frame as the camera update that armed it
{
    if (!g_LensChkOn) return;
    g_LensChkOn = false;
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    if (clipX < 200.f) return;
    float s[3]; if (!CanvasProject(canvas, g_LensChkPt, s)) return;
    float ndc = s[0] / clipX * 2.f - 1.f;
    ++g_LensChecks;
    if (fabsf(ndc - g_LensChkWant) < 0.12f) { CoopLog("CAMERA LENS: check OK - straight ahead lands at %.2f across the screen (player 1's part centre %.2f)", ndc, g_LensChkWant); g_LensChecks = 100; }
    else if (fabsf(ndc + g_LensChkWant) < 0.12f) { g_LensSign = -g_LensSign; CoopLog("CAMERA LENS: straight ahead landed at %.2f, mirrored (want %.2f) - lens direction flipped", ndc, g_LensChkWant); }
    else
    {
        CoopLog("CAMERA LENS: check %d - straight ahead lands at %.2f, want %.2f", g_LensChecks, ndc, g_LensChkWant);
        if (g_LensChecks >= 3) { g_LensOff = -1; CoopLog("CAMERA LENS: the lens doesn't move the picture as expected - off, player 1's view is turned instead"); }
    }
}
static void GrabCamTick(unsigned char* pcm);   // camera/38b (f1.241, log only)
// f1.309: multiple monitors, mode 3 (pip_hud/31b - the window stays over all monitors, the others black): player 1's view
// goes onto their monitor with the off-centre lens (the FOV is already widened for the wide window - 30c MonitorSpanPov),
// after everything else this frame (the camera code may clear the lens when no split screen shows).
static void MonSoloLens(unsigned char* pcm)
{
    if (g_MonMode != 3 || g_MonPend || !pcm || !LensResolve() || g_LensOff <= 0 || !Readable(pcm + 0x400 + g_LensOff, 8)) return;
    float axis = (g_MonRegF[0][0] + g_MonRegF[0][2] * 0.5f) * 2.f - 1.f;
    float* xy = (float*)(pcm + 0x400 + g_LensOff);
    xy[0] = -axis * g_LensSign; xy[1] = 0.f; g_LensSet = true;
    static int logs = 0; if (logs < 3) { ++logs; CoopLog("MONITORS: player 1's view onto their monitor (lens %.2f) - the window stays over all monitors", xy[0]); }
}
// f1.309 (the author: "We had a brightness correction for split screen. Is that still in effect? It seems like the right
// side is brighter."): it is - the windows' pictures have no vignette (WindowVignette=0) - but player 1's own view kept the
// game's vignette, centred on the WHOLE picture: over two monitors that is the border between them, so player 1's monitor
// darkens toward its left side and the right monitor looks brighter (on one screen the same, less). Now player 1's view
// gets the same: no vignette while windows show or while spanning (the camera's view info, FMinimalViewInfo
// PostProcessSettings - only when its blend weight is already 1, so no other camera setting gets switched on).
static void MainVignetteOff(unsigned char* pcm)
{
    if (g_Cfg.pipVignette || !pcm || g_PpS.state != 1) return;
    if (!(AnyWindowShowing() || g_MonMode >= 2)) return;
    static int ppOff = -2, wOff = -2;
    if (ppOff == -2)
    {
        void* st = FindObj("MinimalViewInfo", "ScriptStruct");
        ppOff = st ? PropOff(st, "PostProcessSettings") : -1; wOff = st ? PropOff(st, "PostProcessBlendWeight") : -1;
        CoopLog("VIGNETTE: player 1's view info - PostProcessSettings +0x%X, blend weight +0x%X", ppOff, wOff);
    }
    if (ppOff <= 0 || wOff <= 0 || !Readable(pcm + 0x400 + ppOff, g_PpS.vig.f + 8) || !Readable(pcm + 0x400 + wOff, 4)) return;
    float w = *(float*)(pcm + 0x400 + wOff);
    unsigned char* s = pcm + 0x400 + ppOff;
    static int logs = 0;
    if (w < 0.99f) { if (logs < 3) { ++logs; CoopLog("VIGNETTE: player 1's view blend weight %.2f - left alone", w); } return; }
    *(float*)(s + g_PpS.vig.f) = 0.f; s[g_PpS.vig.b] |= g_PpS.vig.m;
    if (logs < 3) { ++logs; CoopLog("VIGNETTE: player 1's view - vignette off like the windows' (blend weight %.2f)", w); }
}
static void Hook_UpdateCamera(void* pcmv, float dt)
{
    WithP2 base; Hook_UpdateCamera_Impl(pcmv, dt);
    unsigned char* pcm = (unsigned char*)pcmv;
    MonSoloLens(pcm);       // f1.309
    MainVignetteOff(pcm);   // f1.309
    GrabCamTick(pcm);   // f1.241 GRABCAM (log only): the final game camera this frame
    if (g_LensChkArm && pcm && Readable(pcm + 0x400, 0x18))
    {   // the final camera this frame - the point 10 m straight ahead, for LensSelfCheck
        g_LensChkArm = false;
        const float D = DEG_TO_RAD; float* l = (float*)(pcm + 0x400); float* r = (float*)(pcm + 0x40C);
        float f[3] = {cosf(r[0] * D) * cosf(r[1] * D), cosf(r[0] * D) * sinf(r[1] * D), sinf(r[0] * D)};
        for (int j = 0; j < 3; ++j) g_LensChkPt[j] = l[j] + f[j] * 1000.f;
        g_LensChkWant = g_LensChkAxis; g_LensChkOn = true;
    }
    if (pcm && Readable(pcm + 0x400, 0x18))
    {
        float* l = (float*)(pcm + 0x400);
        float dx = l[0] - g_GameCamLoc[0], dy = l[1] - g_GameCamLoc[1], dz = l[2] - g_GameCamLoc[2];
        g_CamMoved = dx * dx + dy * dy + dz * dz > 25.f;
    }
}
// put the sound listener at the game's own camera position while ours is moved (zoomed out /
// slid for split screen), so voices sound as close as in the normal game.
static void ListenerTick(void* pc)
{
    static int state = 0; static void* fSet = nullptr; static void* fClear = nullptr;
    static int pA = -1, pL = -1, pR = -1, sz = 0;
    static bool overridden = false;
    if (state == 0)
    {
        state = -1;
        fSet = FindFunc(pc, "SetAudioListenerOverride"); fClear = FindFunc(pc, "ClearAudioListenerOverride");
        if (fSet) { pA = ParamOff(fSet, "AttachToComponent"); pL = ParamOff(fSet, "Location"); pR = ParamOff(fSet, "Rotation"); sz = ParmsSize(fSet); }
        if (fSet && fClear && pL >= 0 && pR >= 0 && sz <= 128) state = 1;
        CoopLog("AUDIO: listener override %s (Set %p A%d L%d R%d size %d, Clear %p)", state == 1 ? "ready" : "NOT available", fSet, pA, pL, pR, sz, fClear);
    }
    if (state != 1) return;
    bool want = g_CamMoved && !g_InCutscene;
    if (want)
    {
        alignas(16) unsigned char b[128] = {};
        memcpy(b + pL, g_GameCamLoc, 12); memcpy(b + pR, g_GameCamRot, 12);
        Orig_ProcessEvent(pc, fSet, b);
        if (!overridden) { overridden = true; CoopLog("AUDIO: listener held at the game camera (co-op camera moved)"); }
    }
    else if (overridden)
    {
        alignas(16) unsigned char b[64] = {};
        Orig_ProcessEvent(pc, fClear, b);
        overridden = false; CoopLog("AUDIO: listener back on the camera");
    }
}
// (review #27) the part of player 1's split-screen view the lens and the turn method share: the yaw toward player 1, the
// lock-on follow (SplitAimFollowsLockOn: while player 1 is locked on, the direction to the enemy is what lands in the middle
// of player 1's part - eased in / out), the lock zoom (SplitAimLockZoom: the FOV widens, up to +60% / max 110, just enough
// that the enemy can be centred with player 1 still in view; smoothed) and the KeepsPlayer1 clamp (SplitAimLockKeepsPlayer1:
// beyond that the turn is held back; 0 = the enemy always centred, the f1.47 behaviour). Before f1.206 each method had its
// own ~70-line copy. (Why: the author - lock-on "follows based on the center of the screen. Since we block half the screen
// with windows, it doesn't follow the center of what is visible to P1"; f1.50 "sloppy and doesn't center well"; thirds "The
// enemy stays centered, but Cloud doesn't stay in frame".) What differs is spelled out by `lens`:
//  - KeepM(t) = how far player 1 may sit off the centre of their part and stay in view (t = tan of half the camera's FOV):
//    lens - 60% of atan(share * t) (the part is symmetric around the lens axis); turn - 60% of the narrower side of the part
//    as an angle from its centre (player 1's part spans screen x -1 .. -1 + 2 * share, its centre at -off - f1.83);
//  - the wider FOV: lens - worked out directly; turn - found by a search (14 halvings);
//  - the log texts (and when the clamp line is written).
// Each method keeps its own state (a SplitAim each - before: two sets of statics). Returns the yaw to put in the middle of
// player 1's part; th = tan(half FOV) of the camera as it is now (the lock zoom may have widened it - its FOV is written).
struct SplitAim { float lw, lockYaw, zoom; bool lastLock, big; int lockLogs, zoomLogs, keepLogs; };
static SplitAim g_SplitAimLens = {}, g_SplitAimTurn = {};
static float SplitAimYaw(SplitAim& s, bool lens, unsigned char* pcm, void* p1, const float* loc, float gameYaw, float fov, float off,
                         bool split, float sw, float dt, float& th)
{
    const float R = RAD_TO_DEG, D = DEG_TO_RAD;
    float share = 1.f - off;
    float base = gameYaw;
    float plc[3];
    if (p1 && ActorLoc(p1, plc))
    {
        float dx = plc[0] - loc[0], dy = plc[1] - loc[1];
        if (dx * dx + dy * dy > 400.f) base = atan2f(dy, dx) * R;
    }
    // the lock flag is the game's own (battle manager +0x388)
    void* lt = (g_Cfg.camSplitLock && split && P1LockedOn()) ? P1TargetChar(p1) : nullptr;
    float tl[3]; bool lock = lt && ActorLoc(lt, tl);
    s.lw += ((lock ? 1.f : 0.f) - s.lw) * (1.f - expf(-6.f * dt));
    if (lock)
    {
        float dx = tl[0] - loc[0], dy = tl[1] - loc[1];
        if (dx * dx + dy * dy > 400.f) s.lockYaw = atan2f(dy, dx) * R;
    }
    if (lock != s.lastLock)
    {
        s.lastLock = lock;
        if (s.lockLogs < 20)
        {
            ++s.lockLogs; char nm[96] = "-"; if (lt) NameOf(lt, nm, sizeof(nm));
            if (lens) CoopLog("CAMERA: player 1 %s%s - split-screen lens view %s", lock ? "locked on " : "not locked on", lock ? nm : "", lock ? "centres that enemy in player 1's part (widening the view if player 1 wouldn't fit)" : "centres player 1 in their part");
            else CoopLog("CAMERA: player 1 %s%s%s - split-screen aim %s", lock ? "locked on " : "not locked on", lock ? nm : "", "", lock ? "keeps that enemy in the middle of player 1's part (as far as player 1 stays in view)" : "keeps player 1 in the middle of their part");
        }
    }
    auto KeepM = [&](float t)
    {
        if (lens) return 0.6f * atanf(share * t) * R;
        float outer = (atanf(t) - atanf(off * t)) * R, inner = (atanf(off * t) - atanf((off - share) * t)) * R;
        return 0.6f * (outer < inner ? outer : inner);
    };
    float d = s.lw > 0.001f ? AngleDiff(s.lockYaw, base) : 0.f;
    float wantExtra = 0.f, ad = fabsf(d);
    if (g_Cfg.camSplitLockZoom && lock && ad > KeepM(th))
    {
        float hiF = fov * 1.6f; if (hiF > 110.f) hiF = 110.f; if (hiF < fov) hiF = fov;
        if (lens)
        {
            float need = ad / 0.6f; if (need > 80.f) need = 80.f;
            float fNeed = 2.f * atanf(tanf(need * D) / share) * R;
            if (fNeed > hiF) fNeed = hiF;
            wantExtra = fNeed > fov ? fNeed - fov : 0.f;
        }
        else
        {
            float lo = fov, hi = hiF;
            if (KeepM(tanf(hi * 0.5f * D)) > ad)
                for (int it = 0; it < 14; ++it) { float mid = 0.5f * (lo + hi); if (KeepM(tanf(mid * 0.5f * D)) >= ad) hi = mid; else lo = mid; }
            wantExtra = hi - fov;
        }
    }
    s.zoom += (wantExtra - s.zoom) * (1.f - expf(-3.f * dt));
    if (s.zoom < 0.05f && wantExtra == 0.f) s.zoom = 0.f;
    if (s.zoom > 0.f)
    {
        float fovUse = fov + s.zoom * sw;
        *(float*)(pcm + 0x418) = fovUse;   // the game's camera this frame (rebuilt by the game every frame)
        th = tanf(fovUse * 0.5f * D);
        if (s.zoom > 5.f && !s.big && s.zoomLogs < 10) { ++s.zoomLogs; s.big = true; CoopLog("CAMERA: player 1 locked on - view widened to %.0f deg (game %.0f) so the enemy is centred and player 1 stays in view (SplitAimLockZoom)", fov + wantExtra, fov); }
        if (s.zoom < 1.f) s.big = false;
    }
    if (s.lw > 0.001f)
    {
        if (g_Cfg.camSplitLockKeep)
        {
            float m = KeepM(th);
            if (d > m || d < -m)
            {
                if (lens)
                {
                    if (s.keepLogs < 5 && (!g_Cfg.camSplitLockZoom || wantExtra >= (fov * 1.6f > 110.f ? 110.f : fov * 1.6f) - fov - 0.5f))
                    { ++s.keepLogs; CoopLog("CAMERA: player 1 locked on - the enemy is %.0f deg from player 1 even with the widest view, the view turns only %.0f deg so player 1 stays in their part (SplitAimLockKeepsPlayer1)", d, m); }
                }
                else if (s.keepLogs < 5 && (!g_Cfg.camSplitLockZoom || s.zoom > (fov * 0.6f > 110.f - fov ? 110.f - fov : fov * 0.6f) - 1.f))
                { ++s.keepLogs; CoopLog("CAMERA: player 1 locked on - the enemy is %.0f deg from player 1, the view turns only %.0f deg so player 1 stays in their part (SplitAimLockKeepsPlayer1)", d, m); }
                d = d > m ? m : -m;
            }
        }
        base += d * s.lw;
    }
    return base;
}
static void Hook_UpdateCamera_Impl(void* pcmv, float dt)
{
    Orig_UpdateCamera(pcmv, dt);
    unsigned char* pcm = (unsigned char*)pcmv;
    if (pcm && Readable(pcm + 0x400, 0x18)) { memcpy(g_GameCamLoc, pcm + 0x400, 12); memcpy(g_GameCamRot, pcm + 0x40C, 12); }
    g_LastPcm = pcm; g_LastPcmTick = GetTickCount64();   // for the off-screen arrow
    MonitorSpanPov(pcm);   // f1.285 multi-monitor span test (pip_hud/30c; only with MonitorSpanTest=1)
    MonitorSwitchTickMaybe();   // f1.306: the switching also while the main tick stands still (some cutscenes)
    if (!pcm || !GetPartyMember) return;
    if (!(dt > 0.f && dt < 0.5f)) dt = 0.016f;

    void* vt = *(void**)(pcm + 0x1070);          // ViewTarget.Target
    void* pcOwner = *(void**)(pcm + 0x378);       // PCOwner
    void* p1 = LeaderChar();   // (was GetPartyMember(0) = first in the party order: after player 1 switched to Tifa the
                               // split-screen aim / framing kept turning to Cloud - "the camera stays on Cloud")
    static void* lastVt = (void*)1;
    static bool lastVtIsGameplay = false;
    if (vt != lastVt)
    {
        lastVt = vt;
        char nm[96] = "null"; if (vt) NameOf(vt, nm, sizeof(nm));
        char cls[96] = "?"; void* c = vt ? *(void**)((unsigned char*)vt + 0x10) : nullptr; if (c) NameOf(c, cls, sizeof(cls));
        lastVtIsGameplay = vt && (vt == p1 || vt == pcOwner || strncmp(nm, "EndCameraActor", 14) == 0);
        CoopLog("CAMERA: view target is now %s (class %s)%s", nm, cls, lastVtIsGameplay ? " - gameplay camera, co-op camera applies" : " - not the gameplay camera, left alone");
    }
    bool ours = vt && lastVtIsGameplay;
    bool cine = false;
    // f1.279 (the author, long play 2026-11-02 20:09:50, video 29:20: the camera swung to a story shot while P2's window stayed
    // up): the last enemy died 20:09:53 but the game kept the battle (command menu ui 8) for 25 s - a scripted end-of-battle
    // scene - until it took the field controls at 20:10:18. Inside a battle the story-shot checks (narrowed camera, camera far
    // from player 1) were off, so the windows stayed. Now a battle with NO enemy left for 2 s gets the same checks as walking
    // around. Battles with enemies (grabs, limit breaks, special attacks) keep the windows as before.
    bool emptyBattle = false;
    {
        static ULONGLONG noFoesSince = 0, lastChk = 0; static bool foes = true;
        ULONGLONG nowE = GetTickCount64();
        if (!InBattleNow()) { noFoesSince = 0; foes = true; }
        else
        {
            if (nowE - lastChk >= 250) { lastChk = nowE; void* e[4]; foes = CollectEnemies(e, 4) > 0; }
            if (foes) noFoesSince = 0; else if (!noFoesSince) noFoesSince = nowE;
            emptyBattle = noFoesSince && nowE - noFoesSince >= 2000;
        }
        static bool lastEmpty = false; static ULONGLONG nextInfo = 0; static int el = 0;
        if (emptyBattle != lastEmpty)
        {
            lastEmpty = emptyBattle;
            if (el < 80) { ++el; CoopLog("CAMERA: %s", emptyBattle ? "battle with no enemy left (2 s) - story-shot checks on (narrowed / far-away game camera hides the windows)" : "battle with enemies again / battle over - story-shot checks back to battle rules"); }
            nextInfo = 0;
        }
        if (emptyBattle && nowE >= nextInfo && el < 80 && p1)
        {   // log only: what the game's camera does in that stretch (the next log confirms the fix)
            nextInfo = nowE + 2000; ++el;
            float pp[3]; float d = -1.f;
            if (ActorLoc(p1, pp)) { const float* gl = (const float*)(pcm + 0x400); float dx = gl[0] - pp[0], dy = gl[1] - pp[1], dz = gl[2] - pp[2]; d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.f; }
            CoopLog("CAMERA: no enemy left - the game's camera fov %.0f, %.1f m from player 1", *(float*)(pcm + 0x418), d);
        }
    }
    {   // (the author, f1.120 run ~21:27:00 / 21:28:15: "camera should have changed, but it stayed on coop cam ... too
        // far zoomed out, it focuses on the center between the characters. Should be just normal cam at this point"): those
        // shots keep the field controls ON (TALKSCENE only flipped at 21:27:05 / 21:28:25), but the game's own camera
        // narrows: fov 52 / 50 at 21:27:00 - 21:27:03 and 21:28:09 - 21:28:15, 33 / 30 at 21:28:18 - 21:28:21; in the
        // f1.118 - f1.120 runs every other "framing" line (409) says 60. So: outside battle, the game's fov more than 4 deg
        // under its normal one (60; or a value it has held for 20 s outside battle and scenes) = a cinematic shot -> the
        // same step-aside as a talking scene. [Camera] TalkScenes=0 turns both off.
        // f1.281 (the author 2026-11-04: "I don't know if the proposed fix is robust enough?" -> "Yes on F1.281"): the normal
        // field of view is FIXED at 60 - nothing is learned any more. f1.120 - f1.280 learned a wider value held 20 s in the field;
        // 2026-11-04 22:09:55 an area of Wall Market walked at 63 - 65 deg -> "normal 65", and afterwards plain 60 - 61 deg walking
        // counted as a cinematic shot (22:10:51, 22:11:02, 22:11:16, 22:18:44: co-op camera off ~10 s each). Walking in 3 h of logs:
        // 60 - 65 deg; story shots 30 - 54 deg (the 50 deg 2026-11-04 21:47 was the forced story walk). Cinematic = under 56 deg.
        const float normalFov = 60.f;
        float gf = *(float*)(pcm + 0x418);
        bool field = ours && (!InBattleNow() || emptyBattle) && !g_InCutscene && !g_TalkScene && gf > 10.f && gf < 170.f;   // (f1.279: + a battle with no enemy left)
        cine = g_Cfg.camTalkScenes && field && gf < normalFov - 4.f;
        static bool lastCine = false;
        if (cine != lastCine)
        {
            lastCine = cine;
            static int cl = 0; if (cl < 200) { ++cl; CoopLog("CINEMATIC: %s", cine ? "the game's camera narrowed (cinematic shot) - game camera, split screen hidden" : "the game's camera is back to normal - co-op camera / split screen back (if they were on)"); }
        }
        static float lastF = 0.f; static int fl = 0;
        if (cine && fabsf(lastF - gf) > 2.f && fl < 60) { lastF = gf; ++fl; CoopLog("CINEMATIC: field of view %.0f deg (normal %.0f)", gf, normalFov); }
        if (!cine) lastF = 0.f;
        {   // f1.281 safety net (log only): a "cinematic shot" that lasts 60 s of play while player 1 has the field controls is
            // either a forced story walk (right) or an area the game normally walks at under 56 deg (then the co-op camera / split
            // screen would stay off there - the fixed rule would need a change). Paused / menu time doesn't count.
            static ULONGLONG cineMs = 0, lastT = 0; static float fMin = 0.f, fMax = 0.f; static bool told = false; static int tl = 0;
            ULONGLONG nowC = GetTickCount64(), step = lastT ? nowC - lastT : 0; if (step > 100) step = 100; lastT = nowC;
            if (!cine) { cineMs = 0; told = false; }
            else if (!emptyBattle && !g_GamePaused && !g_MainMenuUp)
            {
                if (cineMs == 0) { fMin = fMax = gf; }
                if (gf < fMin) { fMin = gf; }
                if (gf > fMax) { fMax = gf; }
                cineMs += step;
                if (cineMs >= 60000 && !told && tl < 30)
                {
                    told = true; ++tl;
                    CoopLog("CINEMATIC: LONG - a narrowed camera (%.0f - %.0f deg) for 60 s of play while player 1 walks - a forced story walk, or an area that walks narrower than 56 deg (then tell Claude where)", fMin, fMax);
                }
            }
        }
    }
    {   // (the author: "When a cutscene begins it should flip back to the normal game camera, then when it ends it can
        // go back to split screen or coop cam if one of those were previously selected"): the game's own cutscene signal
        // (g_InCutscene) already did that; the co-op camera also stood down for a cutscene camera / a camera far from player
        // 1 - now the split-screen windows do too (g_ScriptedShot, held 0.4 s so a load-time camera jump doesn't flicker).
        bool off = !ours || g_TalkScene || cine || g_StoryShot;   // + talking scenes (core/27a); + cinematic shots; + f1.253 story shots
        float pl[3];
        // (the author, f1.164 Air Buster fight ~20:45: "Cloud was caught in a grapple ... This caused the camera to exit
        // split screen and focus on him. Not ideal."): in a battle a far-away game camera (a grab / special attack shot) no
        // longer hides the windows - only the game's cutscene signal, a non-gameplay camera, talking scenes or cinematic
        // shots do. (The co-op camera still steps aside for it below, so player 1's view shows the game's shot.)
        if (!off && p1 && ((!InBattleNow() && !g_BalBattle) || emptyBattle) && ActorLoc(p1, pl))   // (g_BalBattle: the party-size battle latch, settings/03; f1.279: + a battle with no enemy left)
        {
            const float* gl = (const float*)(pcm + 0x400);
            float dx = gl[0] - pl[0], dy = gl[1] - pl[1], dz = gl[2] - pl[2];
            off = (dx * dx + dy * dy + dz * dz) > g_Cfg.camCutsceneDist * g_Cfg.camCutsceneDist;
        }
        static ULONGLONG since = 0; ULONGLONG now = GetTickCount64();
        if (!off) since = 0; else if (!since) since = now;
        bool shot = off && (g_StoryShot || now - since >= 400);   // (f1.255: a story shot hides at once - no 0.4 s of the swap showing)
        if (shot != g_ScriptedShot)
        {
            g_ScriptedShot = shot;
            CoopLog("CUTSCENE: %s", shot ? (!ours ? "the game switched to a cutscene / scripted camera - split screen hidden until it's back"
                                                   : g_TalkScene ? "talking scene (field controls off) - split screen hidden until it's over"
                                                   : cine ? "cinematic shot (the game's camera narrowed) - split screen hidden until it's over"
                                                   : "the game's camera is far from player 1 (scripted shot) - split screen hidden until it's back")
                                         : "the game's camera is back on player 1 - split screen back (if it was on)");
        }
    }
    if (!ours) { g_CamW = 0.f; g_CamHaveState = false; LensClear(pcm); return; }   // cutscene / ability camera: hands off
    {   // f1.278 (the author, long play 2026-11-02 21:12:00: "the upgrade menu was broken ... it wasn't loading up the graphics
        // properly"): the game's own menus use the gameplay camera (fov 90 in the main menu). 21:12:02 - 21:12:27 and 21:12:45 -
        // 21:13:09, with the main menu open and the game paused, the co-op camera was framing Cloud + Tifa ("framing 2 things ...
        // fov 90") - it pulled the menu's camera away from what the menu shows. Now: while the game is paused or the main menu /
        // save screen is up, the game's camera is left alone (the co-op camera keeps its state and carries on when play resumes).
        bool menuUp = g_GamePaused || g_MainMenuUp;
        static bool lastMenuUp = false;
        if (menuUp != lastMenuUp)
        {
            lastMenuUp = menuUp;
            static int ml = 0; if (ml < 60) { ++ml; CoopLog("CAMERA: %s", menuUp ? "game paused / main menu - the game's camera is left alone (co-op camera / split-screen aim hold)" : "play resumed - co-op camera / split-screen aim carry on"); }
        }
        if (menuUp) { LensClear(pcm); return; }
    }
    // cutscenes also use the gameplay camera actor - stand down while one is running, and
    // whenever the game's camera is far away from player 1 (scripted shots, area loads).
    {
        float pl[3], gl[3] = {((float*)(pcm + 0x400))[0], ((float*)(pcm + 0x400))[1], ((float*)(pcm + 0x400))[2]};
        float fov0 = *(float*)(pcm + 0x418), aspect0 = *(float*)(pcm + 0x428);   // (the grab-shot lens below)
        bool farAway = false;
        if (p1 && ActorLoc(p1, pl))
        {
            float dx = gl[0] - pl[0], dy = gl[1] - pl[1], dz = gl[2] - pl[2];
            farAway = (dx * dx + dy * dy + dz * dz) > g_Cfg.camCutsceneDist * g_Cfg.camCutsceneDist;
        }
        static bool wasStoodDown = false;
        bool standDown = g_InCutscene || farAway || g_TalkScene || cine || g_StoryShot;   // (no mini-game stand-down; + talking scenes; + cinematic shots; + f1.253 story shots)
        if (standDown != wasStoodDown)
        {
            wasStoodDown = standDown;
            CoopLog("CAMERA: %s", standDown ? (g_InCutscene ? "cutscene - normal camera" : g_TalkScene ? "talking scene - normal camera" : cine ? "cinematic shot - normal camera" : g_StoryShot ? "story shot - normal camera" : "game camera is far from player 1 - normal camera") : "co-op camera resumes");
        }
        if (standDown)
        {
            g_CamW = 0.f; g_CamHaveState = false; g_OrientActive = g_OrientReturning = false;
            // (the author, f1.208 Airbuster fight ~19:23:11: "when Cloud got grabbed ... the camera put him in the center of
            // the screen so he was half cut off by the other windows"): in a battle a far-away game camera (a grab / special
            // attack shot) keeps the split screen (f1.165), but the lens was cleared here, so the game's shot was centred on
            // the whole screen - behind the windows. Now, with the windows showing and only "far away" as the reason, the
            // game's shot keeps its own aim but is moved into player 1's part: the off-centre lens (side by side) or the
            // small look-down / look-up (windows at the bottom / top), as in normal split-screen play. No turning toward
            // player 1 (the game's shot frames what it wants).
            if (farAway && !g_InCutscene && !g_TalkScene && !cine && !g_StoryShot && AnyWindowShowing() && GrabShotIntoP1Part(pcm, fov0, aspect0)) return;
            LensClear(pcm);
            return;
        }
    }

    float* loc = (float*)(pcm + 0x400);
    float* rot = (float*)(pcm + 0x40C);   // Pitch, Yaw, Roll (degrees)
    float fov = *(float*)(pcm + 0x418);
    float aspect = *(float*)(pcm + 0x428);
    if (!(fov > 10.f && fov < 170.f)) fov = 90.f;
    if (!(aspect > 0.5f && aspect < 4.f)) aspect = 16.f / 9.f;

    // split screen (picture-in-picture at Half). Each player has their own view, so the co-op
    // framing stands down and player 1's normal camera is turned (side by side: through an off-centre
    // lens) so player 1 and the way they're heading sit in the middle of their part of the screen.
    // (The old Slide way - moving the camera sideways - was removed in f1.200, the author's call.)
    {
        static float sw = 0.f;
        bool split = AnyWindowShowing();   // (always split screen) (player 3's window alone too - the author: player 1 "Needs to have the same adjustments that he has when P2 is split screen")
        bool lensUsed = false;   // the off-centre lens was set this frame
        struct LensEnd { bool& used; unsigned char* pcm; ~LensEnd() { if (used) g_LensSet = true; else LensClear(pcm); } } lensEnd{lensUsed, pcm};
        float kk = 1.f - expf(-4.f * dt);
        sw += ((split ? 1.f : 0.f) - sw) * kk;
        if (split || sw > 0.01f)
        {
            int side = SplitSide();                 // thirds = windows on the right, player 1 in the left third
            float off = 1.f - SplitP1Frac();        // player 1's centre, off the screen centre (screen half-widths): 0.5 halves, 2/3 thirds
            {   // With the camera turned by atan(0.5 * tan(half FOV)), the direction
                // player 1 runs in (straight ahead for the game) lands in the middle of their half, and so does
                // player 1 - the vanishing point is there too, so running no longer drifts toward the screen centre.
                const float R = RAD_TO_DEG, D = DEG_TO_RAD;
                float th = tanf(fov * 0.5f * D);
                bool lensOk = side < 2 && g_Cfg.camSplitLens && LensResolve() && Readable(pcm + 0x400 + g_LensOff, 8);
                if (side >= 2)
                {
                    float dv = atanf(0.5f * th / aspect) * R;
                    float sgn = side == 3 ? -1.f : 1.f;   // P2/P3 at the bottom -> look down a little (player 1 in the top half)
                    float want = rot[0] + sgn * dv;
                    // f1.271 (the author: "P1 using lock on when on the horizontal camera split doesn't keep the target
                    // centered vertically - but only when near a wall?" -> "Keep the top/bottom layouts centered with locked
                    // on yes"): the top / bottom layouts only tilted the game's pitch by a fixed amount, so the locked-on
                    // enemy's height in player 1's (half-height) part was the game camera's - near a wall the game pulls in
                    // and changes its angle. Now, locked on, the pitch puts the enemy in the middle of player 1's part
                    // (eased in / out like SplitAimFollowsLockOn), and with SplitAimLockKeepsPlayer1 the part's centre stays
                    // within 60% of its half-height of player 1, so player 1 stays in view.
                    static float vlw = 0.f; static int vlogs = 0;
                    void* lt = (g_Cfg.camSplitLock && split && P1LockedOn()) ? P1TargetChar(p1) : nullptr;
                    float tl[3], pl[3];
                    bool vlock = lt && ActorLoc(lt, tl);
                    vlw += ((vlock ? 1.f : 0.f) - vlw) * (1.f - expf(-6.f * dt));
                    static float lastCentre = 0.f;
                    if (vlock)
                    {
                        float hx = tl[0] - loc[0], hy = tl[1] - loc[1];
                        float centre = atan2f(tl[2] - loc[2], sqrtf(hx * hx + hy * hy)) * R;   // the enemy's elevation from the camera
                        if (g_Cfg.camSplitLockKeep && p1 && ActorLoc(p1, pl))
                        {
                            float px = pl[0] - loc[0], py = pl[1] - loc[1];
                            float pe = atan2f(pl[2] - loc[2], sqrtf(px * px + py * py)) * R;
                            float keep = 0.6f * dv;   // the part's half-height is about dv
                            if (centre > pe + keep) centre = pe + keep;
                            if (centre < pe - keep) centre = pe - keep;
                        }
                        lastCentre = centre;
                        if (vlogs < 5 && sw > 0.99f) { ++vlogs; CoopLog("CAMERA: player 1 locked on (windows %s) - the view tilts so the enemy is in the middle of player 1's part, up and down (f1.271)", side == 3 ? "at the bottom" : "at the top"); }
                    }
                    if (vlw > 0.001f) want += ((lastCentre + sgn * dv) - want) * vlw;
                    rot[0] += (want - rot[0]) * sw;
                }
                else if (lensOk)
                {   // the off-centre lens (see LensResolve). The picture's centre moves to the middle of player 1's part;
                    // the camera turns so player 1 (or, locked on, the enemy - SplitAimFollowsLockOn) is straight ahead. Player
                    // 1's part then spans atan(share * tan(half FOV)) each side of its centre - symmetric, no edge squeeze.
                    float axis = SplitP1Axis();   // the middle of player 1's part, screen x in NDC (-1 left .. 1 right) (f1.290: from the monitors layout too)
                    float* lensXY = (float*)(pcm + 0x400 + g_LensOff);
                    lensXY[0] = -axis * g_LensSign * sw; lensXY[1] = 0.f; lensUsed = true;
                    if (sw > 0.99f && g_LensChecks < 3) { static ULONGLONG nextChk = 0; ULONGLONG nc = GetTickCount64(); if (nc >= nextChk) { nextChk = nc + 1500; g_LensChkArm = true; g_LensChkAxis = axis; } }
                    float base = SplitAimYaw(g_SplitAimLens, true, pcm, p1, loc, rot[1], fov, off, split, sw, dt, th);
                    rot[1] += AngleDiff(base, rot[1]) * sw;
                }
                else
                {   // measured from the direction to player 1 (the game's camera keeps player 1 a little off
                    // centre, over the shoulder - that made player 1 sit off-centre on one side). Player 1 lands in
                    // the middle of their half and, while running, so does the way they're heading.
                    float base = SplitAimYaw(g_SplitAimTurn, false, pcm, p1, loc, rot[1], fov, off, split, sw, dt, th);
                    float dh = atanf(off * th) * R;   // player 1's centre (with the wider lens when the lock zoom widened it)
                    float want = base + (side == 1 ? dh : -dh);   // P2/P3 on the right -> look right of player 1
                    rot[1] += AngleDiff(want, rot[1]) * sw;
                }
            }
            if (split) { g_CamW = 0.f; g_CamHaveState = false; g_OrientActive = g_OrientReturning = false; return; }
        }
    }

    void* p2 = g_Player->joined ? P2Char() : nullptr;
    float a[3], b[3];
    bool active = g_Cfg.camEnabled && g_CamOn && p1 && p2 && p1 != p2 && ActorLoc(p1, a) && ActorLoc(p2, b);

    float k = 1.f - expf(-g_Cfg.camSmoothing * dt);
    g_CamW += ((active ? 1.f : 0.f) - g_CamW) * k;
    if (!active && g_CamW < 0.01f) { g_CamW = 0.f; g_CamHaveState = false; return; }

    float gameLoc[3] = {loc[0], loc[1], loc[2]};
    float gamePitch = rot[0], yaw = rot[1];
    if (active)
    {
        // 1. what to frame
        void* en[64]; int ne = 0;
        for (int i = 0; i < g_EnemyN && ne < 64; ++i) { void* e = ResolveWeak(g_EnemyIdx[i], g_EnemySer[i]); if (e) en[ne++] = e; }
        float pts[72][3]; int np = 0;
        memcpy(pts[np++], a, 12); memcpy(pts[np++], b, 12);
        void* t1 = P1Target(p1, en, ne);
        void* t2 = g_Player->drawValid ? ResolveWeak(g_Player->tgtIdx, g_Player->tgtSerial) : nullptr;
        if (t1 && ActorLoc(t1, pts[np])) np++;
        if (t2 && t2 != t1 && ActorLoc(t2, pts[np])) np++;
        int required = np;
        float extra[64][3]; int nx = 0;
        for (int i = 0; i < ne && nx < 64; ++i)
        {
            if (en[i] == t1 || en[i] == t2) continue;
            float e[3]; if (!ActorLoc(en[i], e)) continue;
            float dx = e[0] - a[0], dy = e[1] - a[1];
            if (dx * dx + dy * dy <= g_Cfg.camEnemyRadius * g_Cfg.camEnemyRadius) memcpy(extra[nx++], e, 12);
        }

        // the closest the co-op camera gets to player 1 = BaseZoomOut, measured from player 1 (the author, tooltip
        // review row 71 - before f1.200 it was the game's own distance + BaseZoomOut; the default 8 m = the game's usual
        // ~5 m (the f1.169 - f1.197 log: median 4.95 m, 10% / 90% 4.5 / 6.4 m) + the old 3 m: "set the default to where it currently is")
        float gd[3] = {gameLoc[0] - a[0], gameLoc[1] - a[1], gameLoc[2] - a[2]};
        float gameDist = sqrtf(gd[0] * gd[0] + gd[1] * gd[1] + gd[2] * gd[2]);   // the game's own distance (start state, log)
        CamSolveIn in;
        in.yaw = yaw;
        in.tH = tanf(fov * 0.5f * DEG_TO_RAD) * g_Cfg.camMargin;
        in.tV = in.tH / aspect;
        in.minDist = g_Cfg.camBaseZoom;

        auto solve = [&](int count, float outAim[3], float* outPitch, float* outDist)
        {
            float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
            for (int i = 0; i < count; ++i) for (int j = 0; j < 3; ++j) { if (pts[i][j] < mn[j]) mn[j] = pts[i][j]; if (pts[i][j] > mx[j]) mx[j] = pts[i][j]; }
            for (int j = 0; j < 3; ++j) outAim[j] = a[j] + ((mn[j] + mx[j]) * 0.5f - a[j]) * g_Cfg.camBias;
            float spread = 0.f;
            for (int i = 0; i < count; ++i)
            {
                float dx = pts[i][0] - outAim[0], dy = pts[i][1] - outAim[1];
                float d = sqrtf(dx * dx + dy * dy); if (d > spread) spread = d;
            }
            float t = (spread - g_Cfg.camTiltStart) / (g_Cfg.camTiltFull - g_Cfg.camTiltStart);
            if (t < 0.f) t = 0.f;
            if (t > 1.f) t = 1.f;
            float target = -g_Cfg.camMaxPitch;
            // (the author: "slight more height"): CAM_EXTRA_TILT degrees steeper than the game's own pitch, always
            float gp = gamePitch - CAM_EXTRA_TILT; if (gp < target) gp = gamePitch < target ? gamePitch : target;
            *outPitch = (gp < target) ? gp : gp + (target - gp) * t; // only ever steeper
            in.pitch = *outPitch;
            *outDist = RequiredDist(outAim, pts, count, in);
        };
        float aim[3], pitch, dist;
        solve(required, aim, &pitch, &dist);
        // option C: add the other nearby enemies only if it's cheap
        if (nx > 0)
        {
            int base = np;
            for (int i = 0; i < nx && np < 72; ++i) memcpy(pts[np++], extra[i], 12);
            float aim2[3], pitch2, dist2;
            solve(np, aim2, &pitch2, &dist2);
            if (dist2 <= dist * (1.f + g_Cfg.camEnemySlack)) { memcpy(aim, aim2, 12); pitch = pitch2; dist = dist2; }
            else np = base;
        }
        bool beyond = dist > g_Cfg.camMaxDist;
        if (dist > g_Cfg.camMaxDist) dist = g_Cfg.camMaxDist;
        CameraAutoOrient((unsigned char*)pcOwner, beyond, dist < g_Cfg.camMaxDist * 0.8f, a, b, dt);

        // 5. smooth
        if (!g_CamHaveState)
        {
            // start from where the game's camera is, so joining eases in
            memcpy(g_CamAim, a, 12); g_CamPitch = gamePitch; g_CamDist = gameDist; g_CamHaveState = true;
        }
        for (int j = 0; j < 3; ++j) g_CamAim[j] += (aim[j] - g_CamAim[j]) * k;
        g_CamPitch += (pitch - g_CamPitch) * k;
        g_CamDist += (dist - g_CamDist) * k;

        static ULONGLONG lastLog = 0;
        ULONGLONG now = GetTickCount64();
        if (now - lastLog > 3000)
        {
            lastLog = now;
            CoopLog("CAMERA: framing %d things (%d required%s), distance %.0f (game %.0f), tilt %.0f (game %.0f), fov %.0f",
                np, required, t1 ? ", incl. P1 target" : "", g_CamDist, gameDist, -g_CamPitch, -gamePitch, fov);
        }
    }
    if (!g_CamHaveState) return;

    // build our camera: game yaw, our pitch, looking at the aim point from our distance
    float cp = cosf(g_CamPitch * DEG_TO_RAD), sp = sinf(g_CamPitch * DEG_TO_RAD);
    float cy = cosf(yaw * DEG_TO_RAD), sy = sinf(yaw * DEG_TO_RAD);
    float fwd[3] = {cp * cy, cp * sy, sp};
    float ours3[3];
    // wall check - pull in front of anything solid between the aim point and the camera
    float dist = g_CamDist;
    if (g_Cfg.camWallCheck)
    {
        float want3[3];
        for (int j = 0; j < 3; ++j) want3[j] = g_CamAim[j] - fwd[j] * g_CamDist;
        float frac;
        char stop[64] = "", skip[200] = "";
        {   // only walls / scenery stop the co-op camera - on the Camera channel (the game's camera walls) and on
            // trace type 3 (doors / background meshes); characters, enemies, props are looked past
            // (f1.200, the author: always on - the WallsOnly setting is gone)
            char st2[64], sk2[200];
            frac = CameraWallsOnly(pcmv, g_CamAim, want3, p1, p2, 1, stop, sizeof(stop), skip, sizeof(skip));
            float f2 = CameraWallsOnly(pcmv, g_CamAim, want3, p1, p2, 2, st2, sizeof(st2), sk2, sizeof(sk2));
            if (f2 < frac) { frac = f2; memcpy(stop, st2, sizeof(stop)); }
            if (!skip[0]) memcpy(skip, sk2, sizeof(skip));
        }
        float safe = frac * g_CamDist - 35.f;
        if (safe < 60.f) safe = 60.f;
        if (frac < 1.f && safe < g_CollDist) g_CollDist = safe;          // snap in at once
        else g_CollDist += ((frac < 1.f ? safe : g_CamDist + 200.f) - g_CollDist) * k; // ease back out
        if (g_CollDist < dist) dist = g_CollDist;
        static ULONGLONG lastWallLog = 0;
        if (frac < 1.f && GetTickCount64() - lastWallLog > 3000) { lastWallLog = GetTickCount64(); CoopLog("CAMERA: wall in the way - pulled in to %.0f (wanted %.0f)%s%s%s%s", dist, g_CamDist, stop[0] ? " - " : "", stop, skip[0] ? " | looked past: " : "", skip); }
        else if (frac >= 1.f && skip[0])
        {   // what the walls-only check looked past (before f1.123 these pulled the camera in)
            static ULONGLONG lastSkipLog = 0; static int skipLogs = 0;
            if (GetTickCount64() - lastSkipLog > 5000 && skipLogs < 100) { lastSkipLog = GetTickCount64(); ++skipLogs; CoopLog("CAMERA: wall check looked past %s (not walls)", skip); }
        }
    }
    for (int j = 0; j < 3; ++j) ours3[j] = g_CamAim[j] - fwd[j] * dist;
    // blend with the game's camera
    float w = g_CamW;
    for (int j = 0; j < 3; ++j) loc[j] = gameLoc[j] + (ours3[j] - gameLoc[j]) * w;
    rot[0] = gamePitch + WrapDeg(g_CamPitch - gamePitch) * w;
}

static void InstallCameraHook()
{
    uintptr_t* slot = (uintptr_t*)(g_Base + RVA_ENDPCM_VTABLE + PCM_UPDATECAMERA_SLOT * sizeof(uintptr_t));
    if (*slot != g_Base + RVA_ENDPCM_UPDATECAMERA)
    {
        CoopLog("HOOK FAILED camera: UpdateCamera slot holds %p, expected %p - co-op camera disabled", (void*)*slot, (void*)(g_Base + RVA_ENDPCM_UPDATECAMERA));
        ++g_HooksFailed;
        return;
    }
    Orig_UpdateCamera = (UpdateCamera_t)*slot;
    DWORD old;
    VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old);
    *slot = (uintptr_t)&Hook_UpdateCamera;
    VirtualProtect(slot, sizeof(uintptr_t), old, &old);
    CoopLog("Hooked camera manager UpdateCamera (vtable slot %d)", PCM_UPDATECAMERA_SLOT);
}
// f1.244 (core/06 CutsceneCheckGone): the game's camera this frame is on player 1 again - within 15 m of
// their character (world position) with a normal field of view (a cutscene shot is often far away or narrow: 15 deg at
// Reno & Rude 19:54:58)
static bool CutsceneCamBackOnP1()
{
    unsigned char* pcm = g_LastPcm; void* p1 = LeaderChar(); float pl[3];
    if (!pcm || GetTickCount64() - g_LastPcmTick > 500 || !Readable(pcm + 0x400, 0x20) || !p1 || !CharLoc(p1, pl)) return false;
    const float* c = (const float*)(pcm + 0x400); float fov = *(const float*)(pcm + 0x418);
    float dx = c[0] - pl[0], dy = c[1] - pl[1], dz = c[2] - pl[2];
    return dx * dx + dy * dy + dz * dz < 1500.f * 1500.f && fov >= 45.f && fov < 120.f;
}
