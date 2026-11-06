// ---- v5.6: picture-in-picture view of player 2 ----
// A SceneCapture2D actor (the engine's "second camera that renders into a texture") is spawned once
// through UGameplayStatics::BeginDeferredActorSpawnFromClass/FinishSpawningActor, given a render
// target from UKismetRenderingLibrary::CreateRenderTarget2D, moved behind player 2 each frame, and
// the texture is drawn on the HUD with UCanvas::K2_DrawTexture. All of it through reflection.
// While the window isn't shown, the capture component's tick is turned off so it costs nothing.
// Level changes destroy the actor; it's re-made (weak refs, at most every 2 s, gives up after 5 fails).
// (efficiency review A20 / C13 / C14 / F17): FindObj walks the whole object list (~400k objects, one name lookup each:
// 5-13 ms). Every object the mod looks up by name is listed here; the first FindObj walks ONCE for all of them, later calls
// get the remembered object (checked still alive and still that name). Before, GameplayStatics was walked for 3x,
// KismetRenderingLibrary / UserWidget / PostProcessVolume 2x each. Not found = the next call walks again (as before).
struct FindObjE { const char* name; const char* cls; int idx, ser; bool found; };
static FindObjE g_FindObjs[] = {
    {"Default__GameplayStatics", "GameplayStatics"}, {"Default__KismetRenderingLibrary", "KismetRenderingLibrary"},
    {"Default__KismetSystemLibrary", "KismetSystemLibrary"}, {"Default__SlateBlueprintLibrary", "SlateBlueprintLibrary"},
    {"Default__EndMenuAPI", "EndMenuAPI"}, {"Default__EndBattleAPI", "EndBattleAPI"},
    {"Default__EndBattleAIController", "EndBattleAIController"},
    {"SceneCapture2D", "Class"}, {"PostProcessVolume", "Class"}, {"TextureRenderTarget2D", "Class"}, {"UserWidget", "Class"},
    {"MinimalViewInfo", "ScriptStruct"}, {"RobotoTiny", "Font"}, {"EndMessageLogs", "Class"},   // the message log widget class
};
static void* FindObjWalk(const char* name, const char* clsName)   // the plain walk (for a pair that isn't in the list)
{
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    for (int i = 0; items && i < num; ++i)
    {
        void* o = *(void**)(items + (size_t)i * 24);
        if (!o) continue;
        if (!NameIs(o, name)) continue;
        void* c = *(void**)((unsigned char*)o + 0x10);
        if (c && NameIs(c, clsName)) return o;
    }
    return nullptr;
}
static void* FindObj(const char* name, const char* clsName)
{
    const int N = (int)(sizeof(g_FindObjs) / sizeof(g_FindObjs[0]));
    int me = -1;
    for (int k = 0; k < N; ++k) if (!strcmp(g_FindObjs[k].name, name) && !strcmp(g_FindObjs[k].cls, clsName)) { me = k; break; }
    if (me < 0) return FindObjWalk(name, clsName);
    FindObjE& e = g_FindObjs[me];
    if (e.found)
    {
        void* o = ResolveWeak(e.idx, e.ser);
        if (o && NameIs(o, name)) return o;
        e.found = false;   // gone - walk again
    }
    // one walk for every listed object not known yet (first match in list order, like the plain walk)
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    int open = 0; for (int k = 0; k < N; ++k) if (!g_FindObjs[k].found) ++open;
    for (int i = 0; items && i < num && open > 0; ++i)
    {
        void* o = *(void**)(items + (size_t)i * 24);
        if (!o) continue;
        char nm[128]; NameOf(o, nm, sizeof(nm));
        for (int k = 0; k < N; ++k)
        {
            FindObjE& f = g_FindObjs[k];
            if (f.found || strcmp(nm, f.name)) continue;
            void* c = *(void**)((unsigned char*)o + 0x10);
            if (c && NameIs(c, f.cls) && MakeWeak(o, &f.idx, &f.ser)) { f.found = true; --open; }
        }
    }
    return e.found ? ResolveWeak(e.idx, e.ser) : nullptr;
}
static int PropOff(void* cls, const char* name)   // a property's offset (Offset_Internal +0x44), -1 = not found
{
    void* f = FindField(cls, name);
    return f ? *(int*)((unsigned char*)f + 0x44) : -1;
}
struct HcTArray { void* data; int num; int max; };   // (moved up)
// ---- f1.242 (the author: a grabbed player's window camera is "too close ... viewing inside the house model"; "make P2/P3
// cameras operate like the base game cameras"). f1.241 GRABCAM, Ghoul 16:01:15: while it held Tifa her position read ~900 m
// away - ActorLoc reads the root's RelativeLocation, which is relative to the parent once the game ATTACHES the grabbed
// character to the grabber; the game played no special shot (no battle-camera sequence), its normal camera kept
// following her real position. CharAttachParent = the root's AttachParent (reflection); ActorWorldLoc =
// K2_GetActorLocation (the world position, attached or not). Used by the window cameras (below) and GRABCAM.
static void* CharAttachParent(void* actor)
{
    static int off = -2;
    unsigned char* a = (unsigned char*)actor;
    unsigned char* root = (a && Readable(a + OFF_ROOT_COMPONENT, 8)) ? *(unsigned char**)(a + OFF_ROOT_COMPONENT) : nullptr;
    if (!root) return nullptr;
    if (off == -2)
    {
        void* cls = Readable(root + 0x10, 8) ? *(void**)(root + 0x10) : nullptr;
        off = cls ? PropOff(cls, "AttachParent") : -1;
        CoopLog("WINCAM: SceneComponent AttachParent at +0x%X%s", off, off > 0 ? "" : " - NOT found (a grabbed player's window camera can't follow them)");
    }
    if (off <= 0 || !Readable(root + off, 8)) return nullptr;
    return *(void**)(root + off);
}
static bool ActorWorldLoc(void* actor, float out[3])
{
    static void* fn = nullptr; static int ret = -1, state = 0;
    if (!actor || !Readable((unsigned char*)actor + 0x10, 8)) return false;
    if (state == 0)
    {
        state = -1;
        fn = FindFunc(actor, "K2_GetActorLocation");
        ret = fn ? ParamOff(fn, "ReturnValue") : -1;
        if (fn && ret >= 0 && ret + 12 <= 64) state = 1;
        CoopLog("WINCAM: K2_GetActorLocation %p, ReturnValue +%d%s", fn, ret, state == 1 ? "" : " - NOT usable");
    }
    if (state != 1) return false;
    alignas(16) unsigned char b[64] = {};
    Orig_ProcessEvent(actor, fn, b);
    memcpy(out, b + ret, 12);
    return out[0] == out[0] && out[1] == out[1] && out[2] == out[2];   // (not NaN)
}
// a character's position: the root's (relative = world) one, or the world one while it is attached to something (a grab)
static bool CharLoc(void* actor, float out[3])
{
    if (CharAttachParent(actor) && ActorWorldLoc(actor, out)) return true;
    return ActorLoc(actor, out);
}
struct PipState
{
    int state = 0;   // 0 = not resolved, 1 = ready, -1 = unavailable
    void* gs = nullptr, *krl = nullptr, *capCls = nullptr;
    void* fBegin = nullptr, *fFinish = nullptr, *fRT = nullptr;
    int bW = -1, bCls = -1, bXf = -1, bColl = -1, bOwner = -1, bRet = -1;
    int fA = -1, fXf = -1;
    int rW = -1, rWd = -1, rH = -1, rF = -1, rRet = -1;
    int offComp = -1;
    // per actor/component (resolved after the first spawn)
    void* fMove = nullptr; int mL = -1, mR = -1, mS = -1, mT = -1;
    void* fTick = nullptr; int tE = -1;
    int cTarget = -1, cSource = -1, cFov = -1, cLod = -1, cDist = -1;
    // drawing
    void* fDraw = nullptr; int dT = -1, dP = -1, dS = -1, dCP = -1, dCS = -1, dC = -1, dB = -1, dR = -1, dPv = -1;
    int drawState = 0;
};
static PipState g_Pip;
// dynamic window quality (pip_hud/30b) - the values the windows use right now (-1 / false = the settings' own)
static float g_DynDetail = -1.f, g_DynDist = -1.f; static int g_DynSharp = -1; static bool g_DynHalf = false;
static float PipDetailNow() { return g_DynDetail > 0.f ? g_DynDetail : g_Cfg.pipDetail; }
static float PipViewDistNow() { return g_DynDist > 0.f ? g_DynDist : g_Cfg.pipViewDist; }
static int PipSharpNow() { return g_DynSharp > 0 ? g_DynSharp : g_Cfg.pipFullPct; }
static void DynApplyInterval(void* comp);   // pip_hud/30b
static int g_PipOn = -1;               // -1 = take from the settings on the first tick
static int g_PipModeRt = -1;           // 0 = always, 2 = battle only (F7 cycles)
static bool PipShowing() { return g_Player->pipVisible; }
// the yaw player 2's stick is relative to - the picture-in-picture view while it shows, else the main camera.
static float P2ControlYaw(unsigned char* pc)
{
    float base = *(float*)(pc + OFF_CONTROL_ROTATION + 4);
    return ((g_Player->num == 2 || g_Player->num == 3) && g_Player->pipVisible) ? g_Player->pipYaw : base;
}
static bool g_P3WinOn = false;   // player 3 is in and has a window this frame
static bool g_P2WinOn = true;    // player 2 is in and has a window this frame (true until the first update - as before)
// f1.290 MULTI-MONITOR layout (pip_hud/31b sets these while the game window spans the monitors): each player's region as
// fractions of the drawing area (x, y, w, h; w 0 = none), player 1's centre x (fraction). Used by PipRect, SplitSide,
// SplitP1Frac, SplitP1Axis instead of the halves / thirds.
static bool g_MonLayoutOn = false;
static float g_MonRegF[3][4] = {};
static void P3WinUpdate()
{
    bool on = false; if (g_P3Enabled) { WithP3 w; on = (g_Player->joined || g_Cfg.pipForAI) && GetPartyMember && P2Char() != nullptr; }   // only with a character to play (or WindowsForAI)
    g_P3WinOn = on && g_PipOn > 0;   // horizontal split too (P2 left, P3 right)
    // (the author: "if P3 leaves it goes to split screen. If P2 leaves, it leaves P3 as a quarter screen. It should put
    // P3 as a half screen in that situation"): player 3's window only shares the half with player 2's while player 2 is in
    bool on2; { WithP2 b; on2 = (g_Player->joined || g_Cfg.pipForAI) && GetPartyMember && P2Char() != nullptr; }
    static bool last2 = true;
    if (on2 != last2 && g_P3WinOn) CoopLog("PIP: player 2 %s - player 3's window %s", on2 ? "is back" : "is out", on2 ? "shares the half with player 2's again" : "takes the whole half");
    last2 = on2; g_P2WinOn = on2;
}
// (the author, f1.130 run: "on the 2 player split screen, the right side had the health page showing over another
// health page"): player 1's things (the game's party panel, the red edge, the command menu) were only placed into player
// 1's part while PLAYER 2's window showed - with player 2 out and player 3's window alone (22:56:26 "the game's party
// panel back to the bottom right of the screen") they went back over player 3's window. Any split-screen window counts.
static bool AnyWindowShowing()
{
    return g_Player->pipVisible || g_OtherPlayer->pipVisible;   // (either way round: one of the two is player 2's, the other player 3's)
}
// (the author: "a camera configuration for 3 player, where the screen is split into 1/3rds vertically ... mainly for
// people with widescreen monitors"): [PictureInPicture] Side=Thirds (pipSide 4) = three columns, player 1 | player 2 |
// player 3. Only while player 3 has a window; until then it is the side-by-side halves with the windows on the right.
// (the author: "I'd put a toggle for it in experimental. Off by default."): only with [PictureInPicture] ThirdsLayout=1;
// otherwise Side=Thirds acts as Right and the layout key skips it.
static bool MonOneScreen();   // pip_hud/31b (f1.311)
static bool SplitThirds()
{
    if (g_MonLayoutOn) return false;   // f1.290: the monitors layout instead
    // f1.313 (the author: "If one wide screen layout is selected. Does it make sure that the other forms of splitting are
    // disabled and it only has the vertical splits?"): one wide screen = side by side only - both windows = always thirds
    if (MonOneScreen()) return g_P3WinOn && g_P2WinOn;
    return g_Cfg.pipSide == 4 && g_Cfg.pipThirdsOn && g_P3WinOn && g_P2WinOn;   // thirds only with both windows
}
// The layout as the half-screen code sees it: 0 left, 1 right, 2 top, 3 bottom. Thirds counts as "windows on the right"
// (player 1 is on the left either way); code that cares about the narrower column asks SplitThirds().
static int SplitSide()
{
    if (g_MonLayoutOn) return g_MonRegF[0][0] > 0.01f && g_MonRegF[0][0] + g_MonRegF[0][2] > 0.99f ? 0 : 1;   // f1.290: player 1's monitor on the right = windows left; left / middle = right
    if (MonOneScreen()) return g_Cfg.pipSide == 0 ? 0 : 1;   // f1.313: one wide screen - left / right only (top / bottom / thirds = right)
    return g_Cfg.pipSide == 4 ? 1 : g_Cfg.pipSide;
}
// Player 1's share of the screen width in a side-by-side split (1/2, or 1/3 in thirds; f1.290: their monitor's share).
static float SplitP1Frac() { return g_MonLayoutOn ? (g_MonRegF[0][2] > 0.05f ? g_MonRegF[0][2] : 0.5f) : SplitThirds() ? (1.f / 3.f) : 0.5f; }
// f1.290: the middle of player 1's part as screen x in NDC (-1 left .. 1 right) - the off-centre lens axis. Halves / thirds
// as before (from the side); the monitors layout from player 1's region (with three monitors player 1 can be in the middle).
static float SplitP1Axis()
{
    if (g_MonLayoutOn) return (g_MonRegF[0][0] + g_MonRegF[0][2] * 0.5f) * 2.f - 1.f;
    float off = 1.f - SplitP1Frac();
    return SplitSide() == 1 ? -off : off;
}

static void PipResolve()
{
    g_Pip.state = -1;
    g_Pip.gs = FindObj("Default__GameplayStatics", "GameplayStatics");
    g_Pip.krl = FindObj("Default__KismetRenderingLibrary", "KismetRenderingLibrary");
    g_Pip.capCls = FindObj("SceneCapture2D", "Class");
    if (g_Pip.gs) { g_Pip.fBegin = FindFunc(g_Pip.gs, "BeginDeferredActorSpawnFromClass"); g_Pip.fFinish = FindFunc(g_Pip.gs, "FinishSpawningActor"); }
    if (g_Pip.krl) g_Pip.fRT = FindFunc(g_Pip.krl, "CreateRenderTarget2D");
    if (g_Pip.fBegin)
    {
        void* f = g_Pip.fBegin;
        g_Pip.bW = ParamOff(f, "WorldContextObject"); g_Pip.bCls = ParamOff(f, "ActorClass"); g_Pip.bXf = ParamOff(f, "SpawnTransform");
        g_Pip.bColl = ParamOff(f, "CollisionHandlingOverride"); g_Pip.bOwner = ParamOff(f, "Owner"); g_Pip.bRet = ParamOff(f, "ReturnValue");
    }
    if (g_Pip.fFinish) { g_Pip.fA = ParamOff(g_Pip.fFinish, "Actor"); g_Pip.fXf = ParamOff(g_Pip.fFinish, "SpawnTransform"); }
    if (g_Pip.fRT)
    {
        void* f = g_Pip.fRT;
        g_Pip.rW = ParamOff(f, "WorldContextObject"); g_Pip.rWd = ParamOff(f, "Width"); g_Pip.rH = ParamOff(f, "Height");
        g_Pip.rF = ParamOff(f, "Format"); g_Pip.rRet = ParamOff(f, "ReturnValue");
    }
    if (g_Pip.capCls) g_Pip.offComp = PropOff(g_Pip.capCls, "CaptureComponent2D");
    CoopLog("PIP: statics %p krl %p SceneCapture2D %p | Begin %p (W%d C%d X%d coll%d own%d R%d size%d) Finish %p (A%d X%d size%d) CreateRT %p (W%d w%d h%d f%d R%d size%d) | comp off %d",
        g_Pip.gs, g_Pip.krl, g_Pip.capCls,
        g_Pip.fBegin, g_Pip.bW, g_Pip.bCls, g_Pip.bXf, g_Pip.bColl, g_Pip.bOwner, g_Pip.bRet, g_Pip.fBegin ? ParmsSize(g_Pip.fBegin) : 0,
        g_Pip.fFinish, g_Pip.fA, g_Pip.fXf, g_Pip.fFinish ? ParmsSize(g_Pip.fFinish) : 0,
        g_Pip.fRT, g_Pip.rW, g_Pip.rWd, g_Pip.rH, g_Pip.rF, g_Pip.rRet, g_Pip.fRT ? ParmsSize(g_Pip.fRT) : 0, g_Pip.offComp);
    bool ok = g_Pip.fBegin && g_Pip.fFinish && g_Pip.fRT && g_Pip.capCls && g_Pip.offComp > 0 &&
        g_Pip.bW >= 0 && g_Pip.bCls >= 0 && g_Pip.bXf >= 0 && g_Pip.bRet >= 0 && ParmsSize(g_Pip.fBegin) <= 512 &&
        g_Pip.fA >= 0 && g_Pip.fXf >= 0 && ParmsSize(g_Pip.fFinish) <= 512 &&
        g_Pip.rW >= 0 && g_Pip.rWd >= 0 && g_Pip.rH >= 0 && g_Pip.rRet >= 0 && ParmsSize(g_Pip.fRT) <= 512;
    if (ok) g_Pip.state = 1;
    else CoopLog("PIP: unavailable - couldn't find what it needs in this game (details above)");
}

static void* PipComponent(void* actor)
{
    unsigned char* a = (unsigned char*)actor;
    if (!a || !Readable(a + g_Pip.offComp, 8)) return nullptr;
    void* comp = *(void**)(a + g_Pip.offComp);
    return (comp && Readable(comp, 0x40)) ? comp : nullptr;
}
static void PipSetTicking(void* actor, bool on)
{
    if (g_Player->pipTicking == on) return;
    void* comp = PipComponent(actor);
    if (!comp || !g_Pip.fTick) return;
    alignas(16) unsigned char b[64] = {};
    b[g_Pip.tE] = on ? 1 : 0;
    Orig_ProcessEvent(comp, g_Pip.fTick, b);
    g_Player->pipTicking = on;
    CoopLog("PIP: capture %s", on ? "running" : "paused (window hidden)");
}

// where the window goes and how big it is (canvas pixels).
static float g_PipCanvasW = 1920.f, g_PipCanvasH = 1080.f;   // last canvas size seen (for the render target's shape)
static float g_GameClientAspect = 0.f;   // f1.286: the game window's drawing area width / height (pip_hud/30c MonitorTick, every 5 s)
// f1.288 (multi-monitor span test, f1.287 09:46: P2's window on monitor 2 = horizontal streaks with the 3840x1080
// screen-shaped picture): while the window spans the monitors each window gets a WINDOW-shaped picture with a centred lens
// (its own size only - also half the GPU work of a 3840-wide picture), drawn whole into the window.
// f1.293: OFF again - P2 / P3's picture contains the game's HUD laid out for the picture's size (f1.292 screenshot: a full
// normal-size HUD incl. player 1's menu in P2's window); the screen-shaped picture keeps it the same as the main view.
// (The f1.286 "streaks" were the main view - the game's render size, fixed by r.SetRes in f1.289.)
static bool PipWindowShaped() { return false; }
static void PipRect(float clipX, float clipY, float* x, float* y, float* w, float* h)
{   // split screen, no margin (the v6.8 small sizes Third / Quarter / a number + Corner were removed)
    if (g_MonLayoutOn)
    {   // f1.290: the monitors layout - this player's region (player 2 / 3)
        const float* r = g_MonRegF[g_Player->num == 3 ? 2 : 1];
        if (r[2] > 0.01f && r[3] > 0.01f) { *x = r[0] * clipX; *y = r[1] * clipY; *w = r[2] * clipX; *h = r[3] * clipY; return; }
    }
    if (SplitThirds())
    {   // three columns - player 2 in the middle third, player 3 in the right third
        *w = clipX / 3.f; *h = clipY; *y = 0.f; *x = g_Player->num == 3 ? clipX * (2.f / 3.f) : clipX / 3.f;
        return;
    }
    int side = SplitSide();
    if (side >= 2)
    {   // horizontal; with player 3, the half is split - player 2 left, player 3 right
        *w = clipX; *h = clipY * 0.5f; *x = 0.f; *y = side == 3 ? clipY * 0.5f : 0.f;
        if (g_P3WinOn && g_P2WinOn) { *w = clipX * 0.5f; if (g_Player->num == 3) *x = clipX * 0.5f; }   // P3 alone = the whole half
        return;
    }
    *w = clipX * 0.5f; *h = clipY; *y = 0.f; *x = side == 1 ? clipX - *w : 0.f;
    if (g_P3WinOn && g_P2WinOn) { *h = clipY * 0.5f; if (g_Player->num == 3) *y = clipY * 0.5f; }   // player 2 top, player 3 bottom (P3 alone = the whole half)
}
// (the author: thirds "have an almost fish eye lense"; "All tall windows"): the window FOV is measured across the
// window's width. In a window narrower than 16:9 (thirds 640x1080, side-by-side halves 960x1080) that stretched the
// top-to-bottom angle (FOV 80 -> 110 deg in thirds). [PictureInPicture] TallWindowFov=1 (default): such a window keeps the
// top-to-bottom angle a 16:9 window would have with that FOV, and narrows across instead. 16:9 or wider: unchanged.
// Returns tan(half the horizontal angle across the window).
static float PipTanHalf(float rw, float rh)
{
    float t = tanf(g_Player->cfg->pipFov * 0.5f * DEG_TO_RAD); if (t < 0.05f) t = 0.05f;
    if (g_Cfg.pipTallFov && rh > 1.f && rw > 1.f)
    {
        float asp = rw / rh, ref = 16.f / 9.f;
        if (asp < ref) t *= asp / ref;
        if (t < 0.05f) t = 0.05f;
    }
    return t;
}
static float PipFovNow()   // the horizontal FOV across this player's window, in degrees (the capture's FOVAngle)
{
    float x, y, w, h; PipRect(g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f, &x, &y, &w, &h);
    return 2.f * atanf(PipTanHalf(w, h)) * RAD_TO_DEG;
}
// off-centre lens for a screen-sized picture. The window's own rectangle shows exactly what the
// normal window camera would (same horizontal FOV across the window), the rest of the picture is unused.
// UE reversed-Z perspective, row vectors: ndc.x = x*a/z + ox, ndc.y = y*b/z + oy.

static void PipFullProjection(unsigned char* comp, void* compCls)
{
    static int oUse = -2, oMat = -2;
    if (oUse == -2) { oUse = PropOff(compCls, "bUseCustomProjectionMatrix"); oMat = PropOff(compCls, "CustomProjectionMatrix"); }
    if (oUse < 0 || oMat < 0) { static bool said = false; if (!said) { said = true; CoopLog("PIP: FullSizeHud unavailable - no custom lens on the capture camera"); } return; }
    float W = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f, x, y, w, h; PipRect(W, H, &x, &y, &w, &h);   // always (FullSizeHud)
    float t = PipTanHalf(w, h);   // tall windows keep the 16:9 top-to-bottom angle
    if (PipWindowShaped()) { W = w; H = h; x = 0.f; y = 0.f; }   // f1.288: the picture is the window itself - centred lens
    float a = (w / W) / t, b = a * W / H;
    float ox = ((x + w * 0.5f) / W) * 2.f - 1.f, oy = 1.f - ((y + h * 0.5f) / H) * 2.f;
    float m[16] = {a, 0, 0, 0,   0, b, 0, 0,   ox, oy, 0, 1,   0, 0, 10.f, 0};
    memcpy(comp + oMat, m, sizeof(m));
    comp[oUse] = 1;
    static int logs = 0;
    if (logs < 16) { ++logs; CoopLog("PIP: FullSizeHud lens for player %d's window (%.0f,%.0f %.0fx%.0f of %.0fx%.0f): a %.3f b %.3f centre %.3f,%.3f - FOV %.0f across, %.0f top to bottom (setting %.0f%s)",
        g_Player->num, x, y, w, h, W, H, a, b, ox, oy, 2.f * atanf(t) * RAD_TO_DEG, 2.f * atanf(t * h / w) * RAD_TO_DEG, g_Player->cfg->pipFov, g_Cfg.pipTallFov && w / h < 16.f / 9.f - 0.01f ? ", tall window" : ""); }
}
// (the author: "each consecutive screen gets darker. Left is bright, right is dark, middle is in between"; the f1.80
// screenshot: P1 33, P2 19, P3 10 average, P3 fading 21 -> 7 left to right). Each window shows a SLICE of a screen-sized
// picture, so the picture's vignette (darker edges, centred on the whole picture) darkens the outer windows most. The
// capture component's own FPostProcessSettings (blend weight 1) overrides: VignetteIntensity 0 ([PictureInPicture]
// WindowVignette=0, default) and, if WindowBrightness != 0, AutoExposureBias = the world's bias + that many EV.
// Everything by reflection: UStructProperty::Struct at +0x70; UBoolProperty FieldSize/ByteOffset/ByteMask/FieldMask
// at +0x70..+0x73 (as the removed v7.4 SetBoolProp). The world's post-process volume values are logged next to the capture's.
struct PpOff { int f = -1; int b = -1; unsigned char m = 0; };   // float offset; its bOverride_ byte offset + mask
static struct { int state = 0; int pp = -1; PpOff vig, bias, minB, maxB; int method = -1, methodB = -1; unsigned char methodM = 0; int ppvSet = -1; void* ppvCls = nullptr; } g_PpS;
static bool PpBool(void* st, const char* name, int* off, unsigned char* mask)
{
    unsigned char* p = (unsigned char*)FindField(st, name);
    if (!p || !Readable(p, 0x74) || p[0x70] != 1) return false;
    *off = *(int*)(p + 0x44) + p[0x71]; *mask = p[0x73] ? p[0x73] : 0xFF;
    return true;
}
static void PpResolve(void* compCls)
{
    g_PpS.state = -1;
    unsigned char* f = (unsigned char*)FindField(compCls, "PostProcessSettings");
    if (!f || !Readable(f, 0x78)) { CoopLog("PIP PP: no PostProcessSettings on the capture camera - vignette / brightness unchanged"); return; }
    void* st = *(void**)(f + 0x70);
    if (!st || !Readable(st, 0x40) || !NameIs(st, "PostProcessSettings")) { CoopLog("PIP PP: PostProcessSettings struct not found - vignette / brightness unchanged"); return; }
    g_PpS.pp = *(int*)(f + 0x44);
    auto F = [&](const char* fn, const char* bn, PpOff& o) { o.f = PropOff(st, fn); if (!PpBool(st, bn, &o.b, &o.m)) o.b = -1; };
    F("VignetteIntensity", "bOverride_VignetteIntensity", g_PpS.vig);
    F("AutoExposureBias", "bOverride_AutoExposureBias", g_PpS.bias);
    F("AutoExposureMinBrightness", "bOverride_AutoExposureMinBrightness", g_PpS.minB);
    F("AutoExposureMaxBrightness", "bOverride_AutoExposureMaxBrightness", g_PpS.maxB);
    g_PpS.method = PropOff(st, "AutoExposureMethod");
    if (!PpBool(st, "bOverride_AutoExposureMethod", &g_PpS.methodB, &g_PpS.methodM)) g_PpS.methodB = -1;
    g_PpS.ppvCls = FindObj("PostProcessVolume", "Class");
    if (g_PpS.ppvCls) g_PpS.ppvSet = PropOff(g_PpS.ppvCls, "Settings");
    CoopLog("PIP PP: settings +%d | vignette %d/%d bias %d/%d min %d/%d max %d/%d method %d/%d | volume Settings +%d",
        g_PpS.pp, g_PpS.vig.f, g_PpS.vig.b, g_PpS.bias.f, g_PpS.bias.b, g_PpS.minB.f, g_PpS.minB.b, g_PpS.maxB.f, g_PpS.maxB.b, g_PpS.method, g_PpS.methodB, g_PpS.ppvSet);
    if (g_PpS.pp >= 0 && g_PpS.vig.f >= 0 && g_PpS.vig.b >= 0) g_PpS.state = 1;
    else CoopLog("PIP PP: the vignette fields weren't found - vignette / brightness unchanged");
}
static void PpDescribe(const char* who, unsigned char* s, char* out, size_t n)   // one FPostProcessSettings, for the log
{
    auto B = [&](const PpOff& o) { return o.b >= 0 && (s[o.b] & o.m) ? 1 : 0; };
    auto V = [&](const PpOff& o) { return o.f >= 0 ? *(float*)(s + o.f) : -999.f; };
    snprintf(out, n, "%s: vignette %.2f (override %d), exposure bias %.2f (%d), min %.2f (%d), max %.2f (%d), method %d (%d)", who,
        V(g_PpS.vig), B(g_PpS.vig), V(g_PpS.bias), B(g_PpS.bias), V(g_PpS.minB), B(g_PpS.minB), V(g_PpS.maxB), B(g_PpS.maxB),
        g_PpS.method >= 0 ? (int)s[g_PpS.method] : -1, g_PpS.methodB >= 0 && (s[g_PpS.methodB] & g_PpS.methodM) ? 1 : 0);
}
static void* PpWorldVolume()   // the level's (unbound) post-process volume - the game's own picture settings
{
    if (!g_PpS.ppvCls || g_PpS.ppvSet < 0) return nullptr;
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    for (int i = 0; items && i < num; ++i)
    {
        unsigned char* ob = *(unsigned char**)(items + (size_t)i * 24);
        if (!ob || (*(DWORD*)(ob + 8) & 0x30)) continue;   // (no class default objects)
        if (*(void**)(ob + 0x10) == g_PpS.ppvCls && Readable(ob + g_PpS.ppvSet, 0x40)) return ob;
    }
    return nullptr;
}
static void PipPostProcess(unsigned char* c, void* compCls)
{
    if (g_PpS.state == 0) PpResolve(compCls);
    if (g_PpS.state != 1 || !Readable(c + g_PpS.pp, 0x40)) return;
    unsigned char* s = c + g_PpS.pp;
    static int logs = 0; char t1[260], t2[260];
    // (review F17): the level's volume is found by walking every object (~2 ms) - only when WindowBrightness needs its
    // exposure bias, or for the first log lines (before: on every window spawn)
    void* vol = (g_Cfg.pipBright != 0.f || logs < 6) ? PpWorldVolume() : nullptr;
    unsigned char* vs = vol ? (unsigned char*)vol + g_PpS.ppvSet : nullptr;
    if (logs < 6) { PpDescribe("capture before", s, t1, sizeof(t1)); if (vs) PpDescribe("level volume", vs, t2, sizeof(t2)); else snprintf(t2, sizeof(t2), "level volume: none found"); }
    if (!g_Cfg.pipVignette)
    {
        *(float*)(s + g_PpS.vig.f) = 0.f; s[g_PpS.vig.b] |= g_PpS.vig.m;
    }
    else s[g_PpS.vig.b] &= (unsigned char)~g_PpS.vig.m;
    if (g_PpS.bias.f >= 0 && g_PpS.bias.b >= 0)
    {
        if (g_Cfg.pipBright != 0.f)
        {
            float base = (vs && (vs[g_PpS.bias.b] & g_PpS.bias.m)) ? *(float*)(vs + g_PpS.bias.f) : 0.f;
            *(float*)(s + g_PpS.bias.f) = base + g_Cfg.pipBright; s[g_PpS.bias.b] |= g_PpS.bias.m;
        }
        else s[g_PpS.bias.b] &= (unsigned char)~g_PpS.bias.m;
    }
    if (logs < 6)
    {
        ++logs; char t3[260]; PpDescribe("capture now", s, t3, sizeof(t3));
        CoopLog("PIP PP: player %d's window - %s | %s | %s (WindowVignette %d, WindowBrightness %+.1f EV)", g_Player->num, t1, t2, t3, (int)g_Cfg.pipVignette, g_Cfg.pipBright);
    }
}
static void* PipSpawn(void* pc, const float at[3])
{
    alignas(16) unsigned char b[512] = {};
    alignas(16) float xf[12] = {0.f, 0.f, 0.f, 1.f, at[0], at[1], at[2], 0.f, 1.f, 1.f, 1.f, 0.f};  // FTransform: quat, pos, scale
    // render target first
    // a screen-shaped picture (FullSizeHud - always since f1.56); the window shows its own part of it (PipFullProjection).
    //        FullSizeHudQuality = its size in % of the screen. (The v6.8 window-shaped picture with Resolution / AvoidGameHud is gone.)
    float rx, ry, rw, rh; PipRect(g_PipCanvasW, g_PipCanvasH, &rx, &ry, &rw, &rh);
    float sw = g_PipCanvasW > 100.f ? g_PipCanvasW : 1920.f, shh = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f;
    bool winShaped = PipWindowShaped();
    if (winShaped && rw > 100.f && rh > 100.f) { sw = rw; shh = rh; }   // f1.288: a window-shaped picture while spanning monitors
    int w = (int)(sw * PipSharpNow() / 100.f), h = (int)(shh * PipSharpNow() / 100.f);   // PipSharpNow
    w = w > 3840 ? 3840 : w; h = h > 2160 ? 2160 : h;
    g_Player->pipRtAspect = (rh > 1.f ? rw / rh : 1.f) * (winShaped ? -1.f : 1.f);   // negative = window-shaped (f1.288)
    memcpy(b + g_Pip.rW, &pc, 8); memcpy(b + g_Pip.rWd, &w, 4); memcpy(b + g_Pip.rH, &h, 4);
    if (g_Pip.rF >= 0) b[g_Pip.rF] = (unsigned char)g_Cfg.pipFormat;
    Orig_ProcessEvent(g_Pip.krl, g_Pip.fRT, b);
    void* rt = *(void**)(b + g_Pip.rRet);
    if (!rt) { CoopLog("PIP: couldn't create the render target"); return nullptr; }

    memset(b, 0, sizeof(b));
    memcpy(b + g_Pip.bW, &pc, 8); memcpy(b + g_Pip.bCls, &g_Pip.capCls, 8); memcpy(b + g_Pip.bXf, xf, 48);
    if (g_Pip.bColl >= 0) b[g_Pip.bColl] = 1;   // AlwaysSpawn
    Orig_ProcessEvent(g_Pip.gs, g_Pip.fBegin, b);
    void* actor = *(void**)(b + g_Pip.bRet);
    if (!actor) { CoopLog("PIP: couldn't spawn the capture camera"); return nullptr; }
    memset(b, 0, sizeof(b));
    memcpy(b + g_Pip.fA, &actor, 8); memcpy(b + g_Pip.fXf, xf, 48);
    Orig_ProcessEvent(g_Pip.gs, g_Pip.fFinish, b);

    void* comp = PipComponent(actor);
    if (!comp) { CoopLog("PIP: capture camera has no capture component"); return nullptr; }
    void* compCls = *(void**)((unsigned char*)comp + 0x10);
    if (!g_Pip.fMove)
    {
        g_Pip.fMove = FindFunc(actor, "K2_SetActorLocationAndRotation");
        if (g_Pip.fMove) { g_Pip.mL = ParamOff(g_Pip.fMove, "NewLocation"); g_Pip.mR = ParamOff(g_Pip.fMove, "NewRotation"); g_Pip.mS = ParamOff(g_Pip.fMove, "bSweep"); g_Pip.mT = ParamOff(g_Pip.fMove, "bTeleport"); }
        if (!g_Pip.fMove || g_Pip.mL < 0 || g_Pip.mR < 0 || ParmsSize(g_Pip.fMove) > 512) g_Pip.fMove = nullptr;
        g_Pip.fTick = FindFunc(comp, "SetComponentTickEnabled");
        if (g_Pip.fTick) { g_Pip.tE = ParamOff(g_Pip.fTick, "bEnabled"); if (g_Pip.tE < 0 || ParmsSize(g_Pip.fTick) > 64) g_Pip.fTick = nullptr; }
        g_Pip.cTarget = PropOff(compCls, "TextureTarget"); g_Pip.cSource = PropOff(compCls, "CaptureSource");
        g_Pip.cFov = PropOff(compCls, "FOVAngle"); g_Pip.cLod = PropOff(compCls, "LODDistanceFactor");
        g_Pip.cDist = PropOff(compCls, "MaxViewDistanceOverride");
        CoopLog("PIP: move %p (L%d R%d) tick %p (E%d) | component props target %d source %d fov %d lod %d dist %d",
            g_Pip.fMove, g_Pip.mL, g_Pip.mR, g_Pip.fTick, g_Pip.tE, g_Pip.cTarget, g_Pip.cSource, g_Pip.cFov, g_Pip.cLod, g_Pip.cDist);
    }
    if (!g_Pip.fMove || g_Pip.cTarget < 0) { CoopLog("PIP: unavailable - can't move or aim the capture camera"); g_Pip.state = -1; return nullptr; }
    unsigned char* c = (unsigned char*)comp;
    *(void**)(c + g_Pip.cTarget) = rt;
    if (g_Pip.cSource >= 0) c[g_Pip.cSource] = (unsigned char)g_Cfg.pipSource;
    if (g_Pip.cFov >= 0) *(float*)(c + g_Pip.cFov) = PipFovNow();
    if (g_Pip.cLod >= 0) *(float*)(c + g_Pip.cLod) = PipDetailNow();   // the dynamic quality's value if it has one
    if (g_Pip.cDist >= 0 && PipViewDistNow() > 0.f) *(float*)(c + g_Pip.cDist) = PipViewDistNow();
    DynApplyInterval(comp);   // half-rate windows (when the dynamic quality has them on)
    PipFullProjection(c, compCls);
    {   // without a remembered rendering state the capture gets no temporal AA (grainy, dotted picture)
        static int oPersist = -2, oEvery = -2;
        if (oPersist == -2) { oPersist = PropOff(compCls, "bAlwaysPersistRenderingState"); oEvery = PropOff(compCls, "bCaptureEveryFrame"); }
        if (oPersist >= 0) c[oPersist] = 1;   // temporal AA always on (TemporalAA is no longer a setting)
        {   // the capture camera's show flags had TemporalAA off (shipping build bit order: Bloom 0,
            // Tonemapper 1, AntiAliasing 2, TemporalAA 3, ...). The flags sit right after ShowFlagSettings.
            static int oSf = -2; if (oSf == -2) { oSf = PropOff(compCls, "ShowFlagSettings"); if (oSf >= 0) oSf += 0x10; }
            if (oSf >= 0)
            {
                unsigned char before = c[oSf];
                c[oSf] |= 0x0C;   // AntiAliasing + TemporalAA
                static int sl = 0; if (sl < 4) { ++sl; CoopLog("PIP: capture show flags byte +%d: %02X -> %02X (temporal AA on)", oSf, before, c[oSf]); }
            }
        }
        static int logs = 0;
        if (logs < 4) { ++logs; CoopLog("PIP: temporal AA ON (persist state +%d = %d, capture every frame +%d = %d)",
            oPersist, oPersist >= 0 ? c[oPersist] : -1, oEvery, oEvery >= 0 ? c[oEvery] : -1); }
    }
    PipPostProcess(c, compCls);   // vignette off, brightness, exposure diagnostics
    if (!MakeWeak(actor, &g_Player->pipActIdx, &g_Player->pipActSer) || !MakeWeak(rt, &g_Player->pipRtIdx, &g_Player->pipRtSer))
        CoopLog("PIP: WARNING - couldn't keep track of the capture camera / render target");
    g_Player->pipTicking = true;
    g_Player->pipFresh = true;
    char an[96] = "?"; NameOf(actor, an, sizeof(an));
    CoopLog("PIP: capture camera %s spawned (%dx%d render target %p)", an, w, h, rt);
    {   // diagnostics (once): where does the game's HUD inside the window picture come from?
        static bool done = false;
        if (!done)
        {
            done = true;
            unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
            int num = *(int*)(g_Base + RVA_GOBJ_NUM);
            void* rtCls = FindObj("TextureRenderTarget2D", "Class");
            void* ppvCls = FindObj("PostProcessVolume", "Class");
            int nrt = 0, nppv = 0;
            for (int i = 0; items && i < num; ++i)
            {
                unsigned char* ob = *(unsigned char**)(items + (size_t)i * 24);
                if (!ob || (*(DWORD*)(ob + 8) & 0x30)) continue;
                void* cls = *(void**)(ob + 0x10); bool isRt = false, isPpv = false;
                for (void* k = cls; k; k = *(void**)((unsigned char*)k + 0x30)) { if (k == rtCls) { isRt = true; break; } if (k == ppvCls) { isPpv = true; break; } }
                if (isRt && nrt < 80)
                {
                    ++nrt; char nm[160] = "?", cn[96] = "?", on[160] = "?"; NameOf(ob, nm, sizeof(nm)); NameOf(cls, cn, sizeof(cn));
                    void* outer = *(void**)(ob + 0x20); if (outer) NameOf(outer, on, sizeof(on));
                    CoopLog("HUDDIAG: render target %s (%s) %dx%d fmt %d, outer %s", nm, cn, *(int*)(ob + 0xD0), *(int*)(ob + 0xD4), ob[0xF0], on);
                }
                if (isPpv && nppv < 40)
                {
                    ++nppv; char nm[160] = "?"; NameOf(ob, nm, sizeof(nm));
                    HcTArray wb = *(HcTArray*)(ob + 0x3C0 + 0x5C0); HcTArray bl = *(HcTArray*)(ob + 0x3C0 + 0x5D0);
                    CoopLog("HUDDIAG: post-process volume %s: enabled %d unbound %d, weight %.2f, %d weighted blendable(s), %d blendable(s)", nm,
                        ob[0x9AC] & 1, (ob[0x9AC] >> 1) & 1, *(float*)(ob + 0x9A8), wb.num, bl.num);
                    for (int j = 0; wb.data && j < wb.num && j < 16; ++j)
                    {
                        unsigned char* e = (unsigned char*)wb.data + (size_t)j * 16; void* bo = *(void**)(e + 8);
                        char bn[200] = "null", bc[96] = "?"; if (bo) { NameOf(bo, bn, sizeof(bn)); NameOf(*(void**)((unsigned char*)bo + 0x10), bc, sizeof(bc)); }
                        CoopLog("HUDDIAG:    blendable %d: %s (%s) weight %.2f", j, bn, bc, *(float*)e);
                    }
                }
            }
            unsigned char* cc = (unsigned char*)comp;
            HcTArray cwb = *(HcTArray*)(cc + 0x360 + 0x5C0);
            CoopLog("HUDDIAG: %d render target(s), %d post-process volume(s); capture's own blendables %d, blend weight %.2f, primitive mode %d, show-flag settings %d",
                nrt, nppv, cwb.num, *(float*)(cc + 0x940), cc[0x2B0], ((HcTArray*)(cc + 0x308))->num);
            char hex[200]; int hp = 0; for (int j = 0; j < 0x30 && hp < 190; ++j) hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X", cc[0x318 + j]);
            CoopLog("HUDDIAG: capture component bytes +0x318: %s", hex);
        }
    }
    return actor;
}

static void PollPipToggleKey()
{
    static bool wasDown = false, wasLay = false;
    // each key needs exactly its own Ctrl / Shift / Alt, so F7 and Ctrl+F7 never both fire
    bool down = HotkeyDown(g_Cfg.pipVk, g_Cfg.pipCtrl, g_Cfg.pipShift, g_Cfg.pipAlt);
    bool lay = HotkeyDown(g_Cfg.layVk, g_Cfg.layCtrl, g_Cfg.layShift, g_Cfg.layAlt);
    if ((down && !wasDown) || (lay && !wasLay)) { if (!HotkeysAllowed()) { NoteHotkeyIgnored("split-screen key"); wasDown = down; wasLay = lay; return; } }
    if (lay && !wasLay)
    {   // right -> left -> bottom -> top -> right (Half split); saved so it sticks; -> thirds -> right
        static const int order[5] = {1, 0, 3, 2, 4};   // pipSide values: 0 left, 1 right, 2 top, 3 bottom, 4 thirds
        static const wchar_t* names[5] = {L"Left", L"Right", L"Top", L"Bottom", L"Thirds"};
        int cur = 0; for (int i = 0; i < 5; ++i) if (order[i] == g_Cfg.pipSide) cur = i;
        g_Cfg.pipSide = order[(cur + 1) % 5];
        if (g_Cfg.pipSide == 4 && !g_Cfg.pipThirdsOn) g_Cfg.pipSide = order[(cur + 2) % 5];   // thirds is experimental (off)
        if (MonOneScreen()) g_Cfg.pipSide = 1;   // f1.315: one wide screen - player 1 always on the left (halves / thirds by themselves)
        WritePrivateProfileStringW(L"PictureInPicture", L"Side", names[g_Cfg.pipSide], g_IniPath);
        PipForceRespawn(); if (g_P3Enabled) { WithP3 w; PipForceRespawn(); }
        if (!g_PipOn) { g_PipOn = 1; g_PipModeRt = 0; }
        wchar_t m[120];
        if (MonOneScreen()) swprintf(m, 120, L"Split screen (wide screen): player 1 on the left - halves, or thirds 1 2 3 with player 3 too");
        else if (g_Cfg.pipSide == 4) swprintf(m, 120, L"Split screen: thirds - player 1, 2, 3 from left to right%ls", g_P3Enabled ? L"" : L" (player 3 is off: halves)");
        else swprintf(m, 120, L"Split screen: players 2/3 on the %ls", g_Cfg.pipSide == 0 ? L"left" : g_Cfg.pipSide == 1 ? L"right" : g_Cfg.pipSide == 2 ? L"top" : L"bottom");
        HudNotice(m); CoopLog("PIP: layout key - %ls (saved)", m);
    }
    else if (down && !wasDown)
    {   // always -> battle only -> off -> always
        if (g_PipModeRt < 0) g_PipModeRt = g_Cfg.pipShow == 2 ? 2 : 0;
        if (!g_PipOn) { g_PipOn = 1; g_PipModeRt = 0; }
        else if (g_PipModeRt == 0) g_PipModeRt = 2;
        else { g_PipOn = 0; g_PipModeRt = 0; }
        g_Player->pipFails = 0;
        g_OtherPlayer->pipFails = 0;   // (BUG-4): player 3's window gets its 5 tries back too (runs with player 2's state loaded)
        const wchar_t* m = !g_PipOn ? L"Split screen: off" : g_PipModeRt == 2 ? L"Split screen: battle only" : L"Split screen: always";
        HudNotice(m);
        CoopLog("PIP: toggle key - %ls", m);
    }
    wasDown = down; wasLay = lay;
}

// drop the current player's window camera so it's made again with new settings
static void PipForceRespawn()
{
    void* actor = ResolveWeak(g_Player->pipActIdx, g_Player->pipActSer);
    if (actor)
    {
        PipSetTicking(actor, false);
        void* f = FindFunc(actor, "K2_DestroyActor");
        if (f) { alignas(16) unsigned char b[32] = {}; Orig_ProcessEvent(actor, f, b); }
    }
    g_Player->pipActIdx = -1; g_Player->pipActSer = 0; g_Player->pipRtIdx = -1; g_Player->pipRtSer = 0; g_Player->pipTicking = true; g_Player->pipFails = 0; g_Player->pipLastTry = 0; g_Player->pipFresh = true;
}
// (BUG-4 / BUG-5): the split-screen keys once a frame, from the main tick with player 2's state loaded (before, inside
// PipTick = twice a frame; a layout-key press caught in player 3's call re-made player 3's window twice, player 2's not at all)
static void PipKeysTick()
{
    if (g_PipOn < 0) g_PipOn = (g_Cfg.pipEnabled || g_Cfg.pipShow == 2) ? 1 : 0;   // (as PipTick)
    if (g_PipModeRt < 0) g_PipModeRt = g_Cfg.pipShow == 2 ? 2 : 0;
    PollPipToggleKey();
}
static void PipTick(void* pc)
{
    g_Player->camStick = false;   // set again below while the window shows
    if (g_PipOn < 0) g_PipOn = (g_Cfg.pipEnabled || g_Cfg.pipShow == 2) ? 1 : 0;   // Battle mode is on from the start
    if (g_PipModeRt < 0) g_PipModeRt = g_Cfg.pipShow == 2 ? 2 : 0;
    // (f1.87, BUG-5: the split-screen keys are read once a frame by PipKeysTick, not here - PipTick runs once per window)
    void* actor = g_Pip.state == 1 ? ResolveWeak(g_Player->pipActIdx, g_Player->pipActSer) : nullptr;
    // WindowsForAI (testing) - the window also while this player isn't in (the AI plays their character)
    void* p2 = (g_PipOn && (g_Player->joined || g_Cfg.pipForAI) && !g_InCutscene && !g_ScriptedShot && !g_QuietLeadHide && GetPartyMember) ? P2Char() : nullptr;   // + scripted shots (+ f1.248 a quiet lead hand-off)

    // when to show it
    ULONGLONG now = GetTickCount64();
    bool show = p2 != nullptr;   // (no mini-game stand-down; the dead v11.1 OffScreen block is gone)
    if (show && g_PipModeRt == 2)
    {   // battle only - on when a battle starts, off 2 s after it ends (no flicker between waves)
        static bool inB = false; static ULONGLONG endSince = 0;
        bool b = InBattleNow();
        if (b) { endSince = 0; if (!inB) { inB = true; CoopLog("PIP: battle - split screen on"); } }
        else if (inB) { if (!endSince) endSince = now; if (now - endSince > 2000) { inB = false; CoopLog("PIP: battle over - back to one camera"); } }
        show = inB;
    }
    if (!show) { if (actor) PipSetTicking(actor, false); g_Player->pipVisible = false; return; }

    if (g_Pip.state == 0) PipResolve();
    if (g_Pip.state < 0) { g_Player->pipVisible = false; return; }
    float loc[3]; if (!ActorLoc(p2, loc)) { g_Player->pipVisible = false; return; }
    {   // f1.242: attached (grabbed) - the window camera follows the character's world position, as the game's own camera does
        void* par = CharAttachParent(p2);
        float w[3]; bool useW = par && ActorWorldLoc(p2, w);
        static void* lastPar[4] = {};
        int pn = g_Player->num & 3;
        if (par != lastPar[pn])
        {
            lastPar[pn] = par;
            char pnm[96] = "-", own[96] = "-";
            if (par) { NameOf(par, pnm, sizeof(pnm)); void* o = Readable((unsigned char*)par + 0x20, 8) ? *(void**)((unsigned char*)par + 0x20) : nullptr; if (o) NameOf(o, own, sizeof(own)); }
            if (par) CoopLog("WINCAM: player %d's character is attached to %s (of %s) - %s (relative position %.1f m from the world one)", g_Player->num, pnm, own,
                useW ? "the window camera follows its world position" : "its world position can't be read - window camera as before",
                useW ? sqrtf((w[0]-loc[0])*(w[0]-loc[0]) + (w[1]-loc[1])*(w[1]-loc[1]) + (w[2]-loc[2])*(w[2]-loc[2])) / 100.f : 0.f);
            else CoopLog("WINCAM: player %d's character is no longer attached - window camera back to normal", g_Player->num);
        }
        if (useW) memcpy(loc, w, 12);
    }
    if (actor && g_Player->pipRtAspect != 0.f)
    {   // the window changed shape (player 3's window came or went) - make a new render target (f1.288: also when the
        // picture switches between screen-shaped and window-shaped - the sign)
        float rx, ry, rw, rh; PipRect(g_PipCanvasW, g_PipCanvasH, &rx, &ry, &rw, &rh);
        float a = (rh > 1.f ? rw / rh : 1.f) * (PipWindowShaped() ? -1.f : 1.f);
        if (fabsf(a - g_Player->pipRtAspect) > 0.1f * fabsf(g_Player->pipRtAspect))
        {
            CoopLog("PIP: player %d's window changed shape - new picture (old camera removed)", g_Player->num);
            PipSetTicking(actor, false);
            static void* fDestroy = nullptr; if (!fDestroy) fDestroy = FindFunc(actor, "K2_DestroyActor");   // no build-up of old cameras/pictures
            if (fDestroy) { alignas(16) unsigned char b[32] = {}; Orig_ProcessEvent(actor, fDestroy, b); }
            g_Player->pipActIdx = -1; g_Player->pipActSer = 0; g_Player->pipRtIdx = -1; g_Player->pipRtSer = 0; g_Player->pipTicking = true;
            actor = nullptr; g_Player->pipFails = 0; g_Player->pipLastTry = 0;
        }
    }
    if (!actor || !ResolveWeak(g_Player->pipRtIdx, g_Player->pipRtSer))
    {
        if (g_Player->pipFails >= 5 || now - g_Player->pipLastTry < 2000) { g_Player->pipVisible = false; return; }
        g_Player->pipLastTry = now;
        actor = PipSpawn(pc, loc);
        if (!actor) { if (++g_Player->pipFails >= 5) CoopLog("PIP: giving up after 5 tries (press the toggle key twice to try again)"); g_Player->pipVisible = false; return; }
        g_Player->pipFails = 0;
    }
    PipSetTicking(actor, true);

    // aim: behind this player (turning with them, smoothed)
    float& yaw = g_Player->pipYawS; ULONGLONG& lastT = g_Player->pipLastT;
    float dt = lastT ? (now - lastT) / 1000.f : 0.016f; if (dt > 0.2f) dt = 0.2f; lastT = now;
    float want = ActorYaw(p2);
    float lockTl[3] = {0.f, 0.f, 0.f};   // where the locked target is
    bool lockCam = false;   // locked on -> the window camera keeps the target in view (like the game's lock-on camera)
    if (g_Player->tgtLocked)
    {
        void* tg = ResolveWeak(g_Player->tgtIdx, g_Player->tgtSerial); float tl[3];
        if (tg && ActorLoc(tg, tl)) { float dx = tl[0] - loc[0], dy = tl[1] - loc[1]; if (dx * dx + dy * dy > 400.f) { want = atan2f(dy, dx) * RAD_TO_DEG; lockCam = true; memcpy(lockTl, tl, 12); } }
    }
    float& pitchAdj = g_Player->pipPitchAdj; ULONGLONG& lastManual = g_Player->pipLastManual;   // player 2's own camera moves (globals)
    if (g_Player->pipFresh) { yaw = want; g_Player->pipFresh = false; pitchAdj = 0.f; lastManual = 0; }
    g_Player->camStick = g_Player->cfg->pipCamControl && !g_Player->tgtLocked;
    if (g_Player->camStick && (g_Player->rStickX != 0.f || g_Player->rStickY != 0.f))
    {
        float spd = g_Player->cfg->pipCamSpeed;   // per player (WindowCameraSpeed only)
        yaw += g_Player->rStickX * spd * dt;
        pitchAdj -= (g_Player->cfg->camInvertY ? -g_Player->rStickY : g_Player->rStickY) * spd * 0.5f * dt;   // stick up = look up (InvertY: down)
        float total = g_Player->cfg->pipPitch + pitchAdj;
        if (total < -20.f) pitchAdj = -20.f - g_Player->cfg->pipPitch;
        if (total > 60.f)  pitchAdj = 60.f - g_Player->cfg->pipPitch;
        lastManual = now;
    }
    bool follow = lockCam || !lastManual || (g_Player->cfg->pipFollowDelay > 0.f && now - lastManual > (ULONGLONG)(g_Player->cfg->pipFollowDelay * 1000.f));
    // f1.262 (the author, Abzu & Shoats on f1.261: "Felt like P2 was having its controls affected by camera changes at
    // different times" - "go with B"): their stick moves relative to this camera, and it kept turning behind the
    // character's FACING - in a battle the facing jumps with every attack / dodge, so the camera swung and the same stick
    // pointed somewhere else. In a battle it now moves only by their right stick or a lock-on (like player 1's camera);
    // outside battle it follows as before (WindowTurnSpeed / WindowAutoFollowDelay).
    {
        static bool lastBattle[4] = {false, false, false, false};
        static ULONGLONG battleSeen[4] = {0, 0, 0, 0};
        int bn = g_Player->num & 3;
        if (InBattleNow()) battleSeen[bn] = now;
        bool inBattle = battleSeen[bn] && now - battleSeen[bn] < 2000;   // (no flicker on a short battle-state gap)
        if (inBattle != lastBattle[bn])
        {
            lastBattle[bn] = inBattle;
            CoopLog("WINCAM: player %d's window camera %s", g_Player->num, inBattle ? "in a battle - moves only by their right stick or a lock-on (f1.262)" : "out of battle - follows them again");
        }
        if (inBattle && !lockCam) follow = false;
    }
    float k = (lockCam ? (g_Player->cfg->pipTurn > 4.f ? g_Player->cfg->pipTurn : 4.f) : g_Player->cfg->pipTurn) * dt; if (k > 1.f) k = 1.f;
    if (lockCam) lastManual = 0;
    if (follow) yaw += AngleDiff(want, yaw) * k;
    while (yaw > 180.f) yaw -= 360.f;
    while (yaw < -180.f) yaw += 360.f;
    g_Player->pipYaw = yaw;
    static float lockPitch[4] = {0.f, 0.f, 0.f, 0.f};   // extra tilt toward a high / low target, per player
    {
        float& lp = lockPitch[g_Player->num & 3];
        float wantLp = 0.f;
        if (lockCam)
        {   // angle from the camera's aim point (above the player) up/down to the target
            float dx = lockTl[0] - loc[0], dy = lockTl[1] - loc[1], dz = lockTl[2] - (loc[2] + g_Player->cfg->pipHeight);
            float ang = atan2f(dz, sqrtf(dx * dx + dy * dy)) * RAD_TO_DEG;   // + = above
            wantLp = -ang * 0.85f;                                          // tilt up most of the way (player stays in view)
            float total = g_Player->cfg->pipPitch + pitchAdj + wantLp;
            if (total < -50.f) wantLp = -50.f - g_Player->cfg->pipPitch - pitchAdj;
            if (total > 60.f)  wantLp = 60.f - g_Player->cfg->pipPitch - pitchAdj;
        }
        float kp = (lockCam ? 4.f : 2.f) * dt; if (kp > 1.f) kp = 1.f;
        lp += (wantLp - lp) * kp;
    }
    float pitch = -(g_Player->cfg->pipPitch + pitchAdj + lockPitch[g_Player->num & 3]);
    const float D = DEG_TO_RAD;
    float fwd[3] = {cosf(pitch * D) * cosf(yaw * D), cosf(pitch * D) * sinf(yaw * D), sinf(pitch * D)};
    float tgt[3] = {loc[0], loc[1], loc[2] + g_Player->cfg->pipHeight};
    float pos[3], dist = g_Player->cfg->pipDist;
    for (int j = 0; j < 3; ++j) pos[j] = tgt[j] - fwd[j] * dist;
    if (g_Cfg.camWallCheck)
    {
        // a ball (WindowWallRadius) on the Camera channel like the game's camera, so its camera walls stop it;
        // the thin line if the ball sweep isn't available / WindowWallRadius=0
        char hitNm[64] = "", solidNm[64] = "";   // f1.241 WINCAM (log only): what pulled it in
        float frac = g_Cfg.pipWallRadius > 0.f ? CameraSweep(pc, tgt, pos, LeaderChar(), p2, g_Cfg.pipWallRadius, 1, hitNm, sizeof(hitNm)) : -1.f;
        const char* by = "the ball check";
        if (frac < 0.f) { frac = CameraTrace(pc, tgt, pos, LeaderChar(), p2); by = "the line check"; }   // (L4): was GetPartyMember(0)
        if (g_Cfg.pipWallSolid) { float s = CameraSolidBG(pc, tgt, pos, LeaderChar(), p2, solidNm, sizeof(solidNm)); if (s < frac) { frac = s; by = "the scenery check"; memcpy(hitNm, solidNm, sizeof(hitNm)); } }   // doors / scenery
        if (frac < 1.f) { dist = frac * dist - 25.f; if (dist < 40.f) dist = 40.f; for (int j = 0; j < 3; ++j) pos[j] = tgt[j] - fwd[j] * dist; }
        if (g_Cfg.targetLog && frac < 1.f && dist < 150.f && InBattleNow())
        {   // f1.241 WINCAM (the author: the grabbed player's window camera "too close" - inside Hell House): once a second per player
            static ULONGLONG at[4] = {}; static LogRate rate; ULONGLONG nw = GetTickCount64();
            int pn = g_Player->num & 3;
            if (nw - at[pn] >= 1000 && LogOk(rate, 60)) { at[pn] = nw; CoopLog("WINCAM: player %d's window camera pulled in to %.1f m (wants %.1f m) by %s - %s", g_Player->num, dist / 100.f, g_Player->cfg->pipDist / 100.f, hitNm[0] ? hitNm : "?", by); }
        }
    }
    memcpy(g_Player->pipCamPos, pos, 12); g_Player->pipCamRot[0] = pitch; g_Player->pipCamRot[1] = yaw; g_Player->pipCamOk = true;
    alignas(16) unsigned char b[512] = {};
    float rot[3] = {pitch, yaw, 0.f};
    memcpy(b + g_Pip.mL, pos, 12); memcpy(b + g_Pip.mR, rot, 12);
    if (g_Pip.mS >= 0) b[g_Pip.mS] = 0;
    if (g_Pip.mT >= 0) b[g_Pip.mT] = 1;
    Orig_ProcessEvent(actor, g_Pip.fMove, b);
    void* comp = PipComponent(actor);
    if (comp && g_Pip.cFov >= 0) *(float*)((unsigned char*)comp + g_Pip.cFov) = PipFovNow();
    g_Player->pipVisible = true;
}

// ---- f1.223 (the author: "Dodge doesn't seem to go directionally where the player is pointing"): the move stick asked for
// INSIDE players 2 / 3's command pass (their dodge / attacks / abilities run there - their own command system or, at a
// battle's start, player 1's) was answered with player 1's input - only their movement pass answered with theirs (passes/05
// Hook_GetAxisPair). Now their stick, turned the same way as for their movement (the game turns it with player 1's camera
// yaw; their window looks another way). Counted per player and logged every 30 s (STICK lines) - if their dodge still
// ignores the stick and the count is 0, the dodge takes its direction from somewhere else (then RE).
static int g_StickAsk[4] = {}, g_StickPushed[4] = {};
static bool ActionPassAxis(float* out)
{
    if (!g_InActionPass2 || !(g_Player->num == 2 || g_Player->num == 3) || GetCurrentThreadId() != g_GameThreadId) return false;
    float x = g_Player->stickX, y = g_Player->stickY;
    unsigned char* pc = (unsigned char*)g_P1Pc;
    if (pc && (x != 0.f || y != 0.f) && Readable(pc + OFF_CONTROL_ROTATION, 12))
    {
        float d = AngleDiff(P2ControlYaw(pc), *(float*)(pc + OFF_CONTROL_ROTATION + 4)) * DEG_TO_RAD;
        if (d != 0.f) { float a = atan2f(x, y), m = sqrtf(x * x + y * y); x = m * sinf(a + d); y = m * cosf(a + d); }
    }
    out[0] = x; out[1] = y;
    int pi = g_Player->num & 3; ++g_StickAsk[pi]; if (x != 0.f || y != 0.f) ++g_StickPushed[pi];
    static bool first[4] = {};
    if (!first[pi]) { first[pi] = true; CoopLog("STICK: player %d's command pass asked for the move stick - answered with their own (was player 1's)", g_Player->num); }
    return true;
}
static void StickAskLog()   // every 30 s (from the main tick)
{
    static ULONGLONG at = 0; ULONGLONG now = GetTickCount64();
    if (now - at < 30000) return;
    at = now;
    for (int p = 2; p <= 3; ++p)
        if (g_StickAsk[p]) CoopLog("STICK: last 30 s - player %d's command pass read the move stick %d time(s), %d with the stick pushed (their own stick)", p, g_StickAsk[p], g_StickPushed[p]);
    for (int& v : g_StickAsk) v = 0;
    for (int& v : g_StickPushed) v = 0;
}
