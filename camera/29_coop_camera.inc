// =================================================================================
// SHARED CO-OP CAMERA
// =================================================================================
// The game's camera manager (AEndPlayerCameraManager, native vtable 0x4D150B8) overrides the engine's
// UpdateCamera(float DeltaTime) at vtable slot 198 (0x1687090). We run it, then adjust the result
// in CameraCache.POV (camera manager +0x3F0 entry; POV at +0x400: Location +0x400, Rotation +0x40C,
// FOV +0x418) - that is what the renderer uses for the frame.
//  - aim point moves from player 1 toward player 2 by Bias (0.5 = halfway): the whole camera is
//    shifted by Bias * (P2 - P1), so player 1's camera angle/rotation is kept exactly;
//  - then it is pulled back along its own view direction: BaseZoomOut always, plus
//    (separation - ZoomStartDistance) * ZoomPerDistance, capped at MaxExtraZoom;
//  - both ease in/out with Smoothing, so joining/leaving/separating never snaps.
// Only applied while the camera's view target is player 1's character (or the controller), so
// cutscene cameras and ability camera shots are left completely alone. View target changes are logged.
// Not done yet: wall collision for the extra zoom (it can poke through walls in tight spaces).
static constexpr uintptr_t RVA_ENDPCM_VTABLE = 0x4D150B8;
static constexpr int       PCM_UPDATECAMERA_SLOT = 198;
static constexpr uintptr_t RVA_ENDPCM_UPDATECAMERA = 0x1687090;
typedef void (*UpdateCamera_t)(void* pcm, float dt);
static UpdateCamera_t Orig_UpdateCamera = nullptr;
static bool  g_CamOn = true;

static void PollCameraToggleKey()
{
    static bool wasDown = false;
    bool down = HotkeyDown(g_Cfg.camVk, g_Cfg.camCtrl, g_Cfg.camShift, g_Cfg.camAlt);
    if (down && !wasDown && !HotkeysAllowed()) NoteHotkeyIgnored("camera key");
    else if (down && !wasDown && !g_Cfg.camEnabled) { CoopLog("Camera toggle key - the co-op camera is switched off in the settings ([Camera] Enabled=0), nothing to toggle"); HudNotice(L"Co-op camera is switched off in the settings"); }   // (code review A5)
    else if (down && !wasDown)
    {   // f1.280 (the author: "'Co-op camera on / off' - yes I want this note when the button is pressed")
        g_CamOn = !g_CamOn;
        CoopLog("Camera toggle key - co-op camera %s", g_CamOn ? "ON" : "OFF (normal camera)");
        HudNotice(g_CamOn ? L"Co-op camera on" : L"Co-op camera off");
    }
    wasDown = down;
}


// ---- v5.0: framing camera ----
// Each frame, after the game's own camera update, while the game's gameplay camera is in use:
//  1. Gather what to frame: player 1, player 2, player 1's current target, player 2's target
//     (always), plus other enemies within EnemyRadius of player 1 if adding them pulls the camera
//     back by at most EnemyExtraAllowance (option C).
//  2. Aim point = player 1 moved Bias of the way toward the centre of everything framed.
//  3. Tilt: the more spread out it all is (largest distance from the aim point), the further the
//     pitch goes from the game's own angle toward MaxTilt.
//  4. Distance: the smallest distance at which every framed point fits inside ScreenMargin of the
//     view (using the camera's real FOV and aspect), never closer than BaseZoomOut (from player 1),
//     never farther than MaxDistance.
//  5. Yaw stays the game's (player 1 turns the camera). Aim point / tilt / distance are smoothed,
//     and the whole thing blends in and out (joining, leaving, cutscenes).
//  6. If even MaxDistance can't fit everyone, auto-orient (optional) turns the real camera yaw
//     (controller ControlRotation) to look from behind player 1 toward player 2, and back again.
static bool  g_OrientActive = false, g_OrientReturning = false;
static float g_OrientSavedYaw = 0.f, g_OrientLastSetYaw = 0.f;
static ULONGLONG g_OrientPauseUntil = 0;
static float WrapDeg(float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; }
static void CameraAutoOrient(unsigned char* pc, bool beyond, bool together, const float a[3], const float b[3], float dt)
{
    if (!pc || !g_Cfg.camAutoOrient) return;
    float* yawp = (float*)(pc + 0x3A8 + 4);
    float yaw = *yawp;
    ULONGLONG now = GetTickCount64();
    if ((g_OrientActive || g_OrientReturning) && fabsf(WrapDeg(yaw - g_OrientLastSetYaw)) > 3.f)
    {
        g_OrientActive = g_OrientReturning = false;
        g_OrientPauseUntil = now + 3000;
        CoopLog("CAMERA: player 1 moved the camera - auto-orient stops");
        return;
    }
    if (now < g_OrientPauseUntil) return;
    float step = g_Cfg.camOrientSpeed * (dt > 0.f && dt < 0.5f ? dt : 0.016f);
    if (beyond)
    {
        if (!g_OrientActive) { g_OrientActive = true; if (!g_OrientReturning) g_OrientSavedYaw = yaw; g_OrientReturning = false; CoopLog("CAMERA: can't fit everyone - turning to look from behind player 1 toward player 2"); }
        float target = atan2f(b[1] - a[1], b[0] - a[0]) * RAD_TO_DEG;
        float d = WrapDeg(target - yaw);
        if (fabsf(d) > 0.5f) { yaw = WrapDeg(yaw + (d > 0 ? 1.f : -1.f) * (fabsf(d) < step ? fabsf(d) : step)); *yawp = yaw; }
        g_OrientLastSetYaw = yaw;
    }
    else if (g_OrientActive && together)
    {
        g_OrientActive = false; g_OrientReturning = true;
        CoopLog("CAMERA: everyone fits again - turning back");
    }
    if (g_OrientReturning)
    {
        float d = WrapDeg(g_OrientSavedYaw - yaw);
        if (fabsf(d) <= step) { yaw = g_OrientSavedYaw; g_OrientReturning = false; CoopLog("CAMERA: back to the original angle"); }
        else yaw = WrapDeg(yaw + (d > 0 ? 1.f : -1.f) * step);
        *yawp = yaw;
        g_OrientLastSetYaw = yaw;
    }
    else if (g_OrientActive) g_OrientLastSetYaw = *yawp;
}

// Player 1's current battle target: the leader's battle unit target (unit+0x38 shared pointer,
// virtual +0x20 returns the target character - same as GetTargetBase). Only accepted if it is one of
// the current enemies, so nothing odd can sneak in.
static void* P1Target(void* p1, void** enemies, int n)
{
    if (!p1 || n <= 0) return nullptr;
    unsigned char* unit = *(unsigned char**)((unsigned char*)p1 + OFF_BATTLE_UNIT);
    if (!unit || !Readable(unit + 0x38, 0x10)) return nullptr;
    void* obj = *(void**)(unit + 0x38);
    void* rc = *(void**)(unit + 0x40);
    if (!obj || !rc || !Readable(obj, 8)) return nullptr;
    void** vt = *(void***)obj;
    if (!Readable(vt + 4, 8) || Rva((uint64_t)vt[4]) < 0) return nullptr;
    void* t = ((void* (*)(void*))vt[4])(obj);
    for (int i = 0; i < n; ++i) if (enemies[i] == t) return t;
    static int warned = 0;
    if (t && warned < 3) { warned++; char nm[96] = "?"; NameOf(t, nm, sizeof(nm)); CoopLog("CAMERA: player 1's target object %s isn't in the enemy list - not framed", nm); }
    return nullptr;
}

struct CamSolveIn { float yaw, pitch, tH, tV, minDist; };
// Smallest distance from aim point C at which every point fits (see 4. above).
static float RequiredDist(const float C[3], const float pts[][3], int n, const CamSolveIn& in)
{
    float cp = cosf(in.pitch * DEG_TO_RAD), sp = sinf(in.pitch * DEG_TO_RAD);
    float cy = cosf(in.yaw * DEG_TO_RAD), sy = sinf(in.yaw * DEG_TO_RAD);
    float fwd[3] = {cp * cy, cp * sy, sp};
    float right[3] = {-sy, cy, 0.f};
    float up[3] = {-sp * cy, -sp * sy, cp};
    float D = in.minDist;
    for (int i = 0; i < n; ++i)
    {
        float q[3] = {pts[i][0] - C[0], pts[i][1] - C[1], pts[i][2] - C[2]};
        float f = q[0] * fwd[0] + q[1] * fwd[1] + q[2] * fwd[2];
        float r = q[0] * right[0] + q[1] * right[1] + q[2] * right[2];
        float u = q[0] * up[0] + q[1] * up[1] + q[2] * up[2];
        float d1 = fabsf(r) / in.tH - f;
        float d2 = fabsf(u) / in.tV - f;
        if (d1 > D) D = d1;
        if (d2 > D) D = d2;
    }
    return D;
}


// ---- v5.1: camera wall check ----
// UKismetSystemLibrary::LineTraceSingle (called through ProcessEvent on the library's default object,
// parameters looked up by name) from the aim point to where the camera wants to be, on the Camera
// trace channel, ignoring the two players. If something solid is in the way the camera moves in front
// of it immediately (no lag, so the view never gets blocked); it eases back out when clear.
struct TraceFns { int state = 0; void* cdo = nullptr; void* fn = nullptr; int size = 0;
    int pWorld = -1, pStart = -1, pEnd = -1, pChan = -1, pComplex = -1, pIgnore = -1, pDebug = -1, pHit = -1, pSelf = -1, pRet = -1; };
static TraceFns g_TF;
static float g_CollDist = 1e9f;
static void ResolveTrace()
{
    g_TF.state = -1;
    g_TF.cdo = FindObj("Default__KismetSystemLibrary", "KismetSystemLibrary");   // was its own object walk
    if (!g_TF.cdo) { CoopLog("CAMERA: wall check unavailable (KismetSystemLibrary not found)"); return; }
    g_TF.fn = FindFunc(g_TF.cdo, "LineTraceSingle");
    if (!g_TF.fn) { CoopLog("CAMERA: wall check unavailable (LineTraceSingle not found)"); return; }
    void* f = g_TF.fn;
    g_TF.size = ParmsSize(f);
    g_TF.pWorld = ParamOff(f, "WorldContextObject");
    g_TF.pStart = ParamOff(f, "Start"); if (g_TF.pStart < 0) g_TF.pStart = ParamOff(f, "START");
    g_TF.pEnd = ParamOff(f, "End");
    g_TF.pChan = ParamOff(f, "TraceChannel");
    g_TF.pComplex = ParamOff(f, "bTraceComplex");
    g_TF.pIgnore = ParamOff(f, "ActorsToIgnore");
    g_TF.pDebug = ParamOff(f, "DrawDebugType");
    g_TF.pHit = ParamOff(f, "OutHit");
    g_TF.pSelf = ParamOff(f, "bIgnoreSelf");
    g_TF.pRet = ParamOff(f, "ReturnValue");
    CoopLog("CAMERA: wall check LineTraceSingle=%p size %d world %d start %d end %d chan %d ignore %d hit %d ret %d",
        f, g_TF.size, g_TF.pWorld, g_TF.pStart, g_TF.pEnd, g_TF.pChan, g_TF.pIgnore, g_TF.pHit, g_TF.pRet);
    if (g_TF.size > 0 && g_TF.size <= 1024 && g_TF.pWorld >= 0 && g_TF.pStart >= 0 && g_TF.pEnd >= 0 && g_TF.pChan >= 0 &&
        g_TF.pIgnore >= 0 && g_TF.pHit >= 0 && g_TF.pRet >= 0)
        g_TF.state = 1;
    else CoopLog("CAMERA: wall check unavailable (parameters didn't resolve)");
}
// Returns the fraction (0..1) of the way from a to b where something blocks the camera, or 1 if clear.
static float CameraTrace(void* world, const float a[3], const float b[3], void* ign1, void* ign2)
{
    if (g_TF.state == 0) ResolveTrace();
    if (g_TF.state < 0) return 1.f;
    alignas(16) unsigned char buf[1024] = {};
    void* ignore[2] = {ign1, ign2};
    struct { void** data; int num; int max; } arr = {ignore, (ign1 ? 1 : 0) + (ign2 ? 1 : 0), 2};
    if (!ign1) ignore[0] = ign2;
    memcpy(buf + g_TF.pWorld, &world, 8);
    memcpy(buf + g_TF.pStart, a, 12);
    memcpy(buf + g_TF.pEnd, b, 12);
    buf[g_TF.pChan] = 1;                      // TraceTypeQuery2 = the Camera channel
    if (g_TF.pComplex >= 0) buf[g_TF.pComplex] = 0;
    memcpy(buf + g_TF.pIgnore, &arr, sizeof(arr));
    if (g_TF.pDebug >= 0) buf[g_TF.pDebug] = 0; // no debug drawing
    if (g_TF.pSelf >= 0) buf[g_TF.pSelf] = 0;
    Orig_ProcessEvent(g_TF.cdo, g_TF.fn, buf);
    if (!buf[g_TF.pRet]) return 1.f;
    float t = *(float*)(buf + g_TF.pHit + 4);   // FHitResult.Time
    if (!(t >= 0.f && t <= 1.f)) t = 1.f;
    return t;
}

// DIAG (CAMWALL, the author 2026-10-22: "Barret's camera goes through this door, while Cloud's zooms in extremely close
// when he approaches the same door"). f1.99: trace types 1 (Visibility) / 2 (Camera) pass the door, types 3 - 8 (not set up
// in this game, all the same) stop at it. f1.100 added a world-object line (WorldStatic + WorldDynamic) - it never hit
// anything in the f1.100 test (the door still let the camera through) - removed again.
// CAMWALL with names: the level has its own camera walls for the game's camera - "Collision_GameCameraWall_..." on
// types 1 / 2 - next to "Collision_Wall_..." / the door mesh "BG0004_00_5" / physics props / Barret's sunglasses on type 3.
// So the game's camera only stops at its GameCameraWall pieces, and the game camera (a ball, not a thin line) touches them
// where our line slips past. f1.101: the window cameras sweep a ball (SphereTraceSingle on the Camera channel,
// [PictureInPicture] WindowWallRadius cm, default 20; 0 = the old thin line). A touch right at the start (< 2 %) is
// ignored so the camera doesn't slam in. CAMWALL logs the line (type 2), the ball and type 3, with names, one more test.
struct HitStructOffs { int state = 0; int hTime = 4, hActor = -1; };
static HitStructOffs g_HS;
static void ResolveHitStruct()
{
    g_HS.state = 1;
    if (void* hs = FindObj("HitResult", "ScriptStruct"))
    {
        int t = ParamOff(hs, "Time"); if (t >= 0) g_HS.hTime = t;
        g_HS.hActor = ParamOff(hs, "Actor");
    }
    CoopLog("CAMERA: HitResult Time +%d, Actor +%d", g_HS.hTime, g_HS.hActor);
}
static void HitActorName(const unsigned char* hit, char* nm, size_t n)
{
    snprintf(nm, n, "?");
    if (g_HS.hActor < 0) return;
    int idx = *(const int*)(hit + g_HS.hActor), ser = *(const int*)(hit + g_HS.hActor + 4);
    if (void* act = (idx > 0) ? ResolveWeak(idx, ser) : nullptr) NameOf(act, nm, n);
}
struct SweepFns { int state = 0; void* fn = nullptr; int size = 0;
    int pWorld = -1, pStart = -1, pEnd = -1, pRad = -1, pChan = -1, pComplex = -1, pIgnore = -1, pDebug = -1, pHit = -1, pSelf = -1, pRet = -1; };
static SweepFns g_SW;
static void ResolveSweep()
{
    g_SW.state = -1;
    if (g_TF.state == 0) ResolveTrace();
    if (g_HS.state == 0) ResolveHitStruct();
    if (!g_TF.cdo) return;
    void* f = FindFunc(g_TF.cdo, "SphereTraceSingle");
    if (!f) { CoopLog("CAMERA: ball wall check unavailable (SphereTraceSingle not found) - thin line only"); return; }
    g_SW.fn = f; g_SW.size = ParmsSize(f);
    g_SW.pWorld = ParamOff(f, "WorldContextObject");
    g_SW.pStart = ParamOff(f, "Start"); if (g_SW.pStart < 0) g_SW.pStart = ParamOff(f, "START");
    g_SW.pEnd = ParamOff(f, "End");
    g_SW.pRad = ParamOff(f, "Radius");
    g_SW.pChan = ParamOff(f, "TraceChannel");
    g_SW.pComplex = ParamOff(f, "bTraceComplex");
    g_SW.pIgnore = ParamOff(f, "ActorsToIgnore");
    g_SW.pDebug = ParamOff(f, "DrawDebugType");
    g_SW.pHit = ParamOff(f, "OutHit");
    g_SW.pSelf = ParamOff(f, "bIgnoreSelf");
    g_SW.pRet = ParamOff(f, "ReturnValue");
    CoopLog("CAMERA: ball wall check SphereTraceSingle=%p size %d radius %d chan %d hit %d ret %d", f, g_SW.size, g_SW.pRad, g_SW.pChan, g_SW.pHit, g_SW.pRet);
    if (g_SW.size > 0 && g_SW.size <= 1024 && g_SW.pWorld >= 0 && g_SW.pStart >= 0 && g_SW.pEnd >= 0 && g_SW.pRad >= 0 && g_SW.pChan >= 0 &&
        g_SW.pIgnore >= 0 && g_SW.pHit >= 0 && g_SW.pRet >= 0)
        g_SW.state = 1;
    else CoopLog("CAMERA: ball wall check unavailable (parameters didn't resolve) - thin line only");
}
// The fraction (0..1) of the way from a to b where a ball of radius r touches something on trace type q + 1 (1 = Camera), or 1.
// Returns -1 if the sweep isn't available (the caller uses the line).
static float CameraSweep(void* world, const float a[3], const float b[3], void* ign1, void* ign2, float r, int q, char* nm, size_t n)
{
    if (nm) nm[0] = 0;
    if (g_SW.state == 0) ResolveSweep();
    if (g_SW.state < 0) return -1.f;
    alignas(16) unsigned char buf[1024] = {};
    void* ignore[2] = {ign1, ign2};
    struct { void** data; int num; int max; } arr = {ignore, (ign1 ? 1 : 0) + (ign2 ? 1 : 0), 2};
    if (!ign1) ignore[0] = ign2;
    memcpy(buf + g_SW.pWorld, &world, 8);
    memcpy(buf + g_SW.pStart, a, 12);
    memcpy(buf + g_SW.pEnd, b, 12);
    memcpy(buf + g_SW.pRad, &r, 4);
    buf[g_SW.pChan] = (unsigned char)q;
    if (g_SW.pComplex >= 0) buf[g_SW.pComplex] = 0;
    memcpy(buf + g_SW.pIgnore, &arr, sizeof(arr));
    if (g_SW.pDebug >= 0) buf[g_SW.pDebug] = 0;
    if (g_SW.pSelf >= 0) buf[g_SW.pSelf] = 0;
    Orig_ProcessEvent(g_TF.cdo, g_SW.fn, buf);
    if (!buf[g_SW.pRet]) return 1.f;
    float t = *(float*)(buf + g_SW.pHit + g_HS.hTime);
    if (!(t >= 0.02f && t <= 1.f)) return 1.f;   // touching something at the start - ignore
    if (nm) HitActorName(buf + g_SW.pHit, nm, n);
    return t;
}
// (the author: "Barrett's camera goes through this door, while Cloud's zooms in extremely close"; f1.101 CAMWALL:
// the 5th-storeroom door is the BG mesh "BG0004_00" - only trace type 3 hits it, the ball on the Camera channel slipped
// past it at 19:59:35): the window cameras also stop at solid scenery - a thin line on trace type 3 that only counts hits on
// "BG..." actors (the levels' background meshes: doors, walls, pipes). Other type-3 hits (the invisible "Collision_Wall_"
// player walls, characters, their attachments, physics props) are skipped by tracing again with them ignored (up to 4
// times). [PictureInPicture] WindowWallSolidObjects=0 switches it off. Returns 0..1 (1 = clear), the hit actor's name in nm.
static float CameraSolidBG(void* world, const float a[3], const float b[3], void* ign1, void* ign2, char* nm, size_t n)
{
    if (nm && n) nm[0] = 0;
    if (g_TF.state == 0) ResolveTrace();
    if (g_TF.state < 0) return 1.f;
    if (g_HS.state == 0) ResolveHitStruct();
    if (g_HS.hActor < 0) return 1.f;   // can't tell what was hit
    void* ignore[8] = {}; int ni = 0;
    if (ign1) ignore[ni++] = ign1;
    if (ign2) ignore[ni++] = ign2;
    for (int pass = 0; pass < 4; ++pass)
    {
        alignas(16) unsigned char buf[1024] = {};
        struct { void** data; int num; int max; } arr = {ignore, ni, 8};
        memcpy(buf + g_TF.pWorld, &world, 8);
        memcpy(buf + g_TF.pStart, a, 12);
        memcpy(buf + g_TF.pEnd, b, 12);
        buf[g_TF.pChan] = 2;   // trace type 3
        if (g_TF.pComplex >= 0) buf[g_TF.pComplex] = 0;
        memcpy(buf + g_TF.pIgnore, &arr, sizeof(arr));
        if (g_TF.pDebug >= 0) buf[g_TF.pDebug] = 0;
        if (g_TF.pSelf >= 0) buf[g_TF.pSelf] = 0;
        Orig_ProcessEvent(g_TF.cdo, g_TF.fn, buf);
        if (!buf[g_TF.pRet]) return 1.f;
        float t = *(float*)(buf + g_TF.pHit + g_HS.hTime);
        if (!(t >= 0.f && t <= 1.f)) return 1.f;
        int idx = *(int*)(buf + g_TF.pHit + g_HS.hActor), ser = *(int*)(buf + g_TF.pHit + g_HS.hActor + 4);
        void* act = idx > 0 ? ResolveWeak(idx, ser) : nullptr;
        if (!act) return 1.f;
        char an[64]; NameOf(act, an, sizeof(an));
        if (an[0] == 'B' && an[1] == 'G' && an[2] >= '0' && an[2] <= '9')
        {
            if (t < 0.02f) return 1.f;   // touching at the start (as the ball check) - ignore
            if (nm && n) snprintf(nm, n, "%s", an);
            return t;
        }
        if (ni >= 8) return 1.f;
        ignore[ni++] = act;   // not scenery - look past it
    }
    return 1.f;
}
// (the author, f1.121 run: "Can coop cam only react to walls and not other objects? It gets caught up a little too
// much"): the co-op camera's wall check counted EVERYTHING on the Camera channel except players 1 / 2 (player 3, enemies,
// props ...). Now a thin line on trace type q (1 = Camera channel, 2 = type 3) that only stops at level geometry - names
// from the f1.118 - f1.120 CAMWALL lines: the levels' own camera walls "Collision_GameCameraWall_...", background meshes
// "BG<digit>..." (walls, doors, pipes), "Collision_Terrain_..." and hits with no actor (level brushes) count; characters
// (PC / EN / EB ...), their attachments (FA...), physics props and the invisible player walls "Collision_Wall_..." are
// looked past (traced again with them ignored, up to 6 times). Returns 0..1 (1 = clear); what it stopped at / skipped in
// stop / skip (may be null).
static bool CamIsWall(const char* an)
{
    if (!an[0]) return true;
    if (an[0] == 'B' && an[1] == 'G' && an[2] >= '0' && an[2] <= '9') return true;
    return !strncmp(an, "Collision_GameCameraWall", 24) || !strncmp(an, "Collision_Terrain", 17);
}
static float CameraWallsOnly(void* world, const float a[3], const float b[3], void* ign1, void* ign2, int q, char* stop, size_t ns, char* skip, size_t nk)
{
    if (stop && ns) stop[0] = 0;
    if (skip && nk) skip[0] = 0;
    if (g_TF.state == 0) ResolveTrace();
    if (g_TF.state < 0) return 1.f;
    if (g_HS.state == 0) ResolveHitStruct();
    if (g_HS.hActor < 0) return CameraTrace(world, a, b, ign1, ign2);   // can't tell what was hit - the old check
    void* ignore[10] = {}; int ni = 0;
    if (ign1) ignore[ni++] = ign1;
    if (ign2) ignore[ni++] = ign2;
    size_t ko = 0;
    for (int pass = 0; pass < 6; ++pass)
    {
        alignas(16) unsigned char buf[1024] = {};
        struct { void** data; int num; int max; } arr = {ignore, ni, 10};
        memcpy(buf + g_TF.pWorld, &world, 8);
        memcpy(buf + g_TF.pStart, a, 12);
        memcpy(buf + g_TF.pEnd, b, 12);
        buf[g_TF.pChan] = (unsigned char)q;
        if (g_TF.pComplex >= 0) buf[g_TF.pComplex] = 0;
        memcpy(buf + g_TF.pIgnore, &arr, sizeof(arr));
        if (g_TF.pDebug >= 0) buf[g_TF.pDebug] = 0;
        if (g_TF.pSelf >= 0) buf[g_TF.pSelf] = 0;
        Orig_ProcessEvent(g_TF.cdo, g_TF.fn, buf);
        if (!buf[g_TF.pRet]) return 1.f;
        float t = *(float*)(buf + g_TF.pHit + g_HS.hTime);
        if (!(t >= 0.f && t <= 1.f)) return 1.f;
        int idx = *(int*)(buf + g_TF.pHit + g_HS.hActor), ser = *(int*)(buf + g_TF.pHit + g_HS.hActor + 4);
        void* act = idx > 0 ? ResolveWeak(idx, ser) : nullptr;
        char an[64] = ""; if (act) NameOf(act, an, sizeof(an));
        if (!act || CamIsWall(an))
        {
            if (stop && ns) snprintf(stop, ns, "%s", act ? an : "(level geometry)");
            return t;
        }
        if (skip && nk && ko + 2 < nk) ko += snprintf(skip + ko, nk - ko, "%s%s", ko ? ", " : "", an);
        if (ko >= nk) ko = nk - 1;
        if (ni >= 10) return 1.f;
        ignore[ni++] = act;   // not a wall - look past it
    }
    return 1.f;
}

static float g_CamW = 0.f;                       // blend 0 = game camera, 1 = co-op camera
static float g_CamAim[3] = {0, 0, 0}, g_CamPitch = 0.f, g_CamDist = 0.f;
static bool  g_CamHaveState = false;


