// =================================================================================
// f1.241 GRABCAM - LOG ONLY (the author, boss rush 2026-10-31: "sometimes when a player is grabbed, the camera is a little
// strange" - "The 'strange' camera stuff is happening within the grabbed players window" - "I think the camera is too close
// - when Aerith got pulled into the hell house it was viewing inside the house model"; "I think we should make P2/P3
// cameras operate like the base game cameras" - "Yes logging, no stop gap. Let's just figure out what the game does and
// then focus on recreating that effect"). Nothing is changed here. Measured:
//  - every PlayBattleCameraSequence (0x9FD5B0 - the native under EndBattleAPI / EndBattleAIController
//    PlayBattleCameraSequence: (FName* sequence, owner chara, resource chara) -> sequence UID): its ID, owner, the
//    owner's target then (whose grab), the result, the caller. The f1.240 Hell House log: the game's grab shot came only
//    for its suck-in on player 1's Cloud (4x), never for the same move on Aerith (3x) - does its script ask for the
//    sequence then (and the game drops it), or not at all?
//  - while a shot runs (a sequence call < 15 s ago, or the game camera far from player 1 in a battle, or a boss's
//    grab-like move - BOSSACT name with Absorb / Grab / Catch / Bind / Hold / Capture): 4 times a second the game camera
//    (place, angles, FOV) in the frame of the attacker and of each party character (forward / right / up from them, by
//    their facing), each character's distance to the attacker (a grabbed one is pulled in / moved), and players 2 / 3's
//    window cameras (distance to their character). At a shot's start: every live EndBattleCameraActor with its settings
//    (owner, target, aim / eye targets and offsets, "move up when it hits a wall").
//  - pip_hud/30 (WINCAM): a window camera pulled in close by its wall check - by what, how close (once a second per player).
// Remove once the grab camera for players 2 / 3 is built (removal checklist).
// =================================================================================
static ULONGLONG g_GcSeqAt = 0, g_GcMoveUntil = 0;   // a sequence call / a grab-like boss move (enemies/22g) - sample until
static void* g_GcOwner = nullptr;                   // the attacker of the last sequence call / grab-like move (position reads only -
static char g_GcOwnerName[96] = "-";               //   Readable-guarded; its name is read when it is set, f1.239's lesson)
static char g_GcWhy[80] = "";
static void GcFName(const void* fname, char* out, size_t n)   // an FName's text (as NameOf, without the object)
{
    out[0] = 0;
    if (!fname || !Readable(fname, 8)) { snprintf(out, n, "?"); return; }
    FStringRaw s{};
    ((FNameToString_t)(g_Base + RVA_FNAME_TOSTRING))((void*)fname, &s);
    if (s.data)
    {
        WideCharToMultiByte(CP_UTF8, 0, s.data, -1, out, (int)n, nullptr, nullptr);
        typedef void (*Free_t)(void*);
        ((Free_t)(g_Base + 0x16C0370))(s.data);
    }
}
// enemies/22g BossActEvent: a boss move began / was prepared - a grab-like name starts the sampling (its target = the grabbed one)
static void GrabCamMoveNote(void* enemy, const char* move)
{
    static const char* const kGrab[] = {"Absorb", "Grab", "Catch", "Bind", "Hold", "Capture", "Suck", "Throw"};
    bool grab = false;
    for (const char* k : kGrab) if (strstr(move, k)) { grab = true; break; }
    if (!grab) return;
    g_GcMoveUntil = GetTickCount64() + 16000; g_GcOwner = enemy; NameOf(enemy, g_GcOwnerName, sizeof(g_GcOwnerName));
    snprintf(g_GcWhy, sizeof(g_GcWhy), "boss move %s", move);
}
// f1.253 (the author, f1.252 Failed Experiment: "It still shows the split screen too early though"; earlier: "the cutscene
// the first time doesnt disable split screen"): the fight's catwalk shots (the game's "Tifa, go to the gate" shot) are battle
// camera sequences, not cutscenes - in a battle the split screen stays on for those (f1.165, grabs). And since the quiet
// lead hand-off (f1.250+) can make Tifa the leader right at the shot's start (log 22:43:07.63: she stood 1.5 m from the zone),
// the game's camera wasn't "far from player 1" any more and the mod's split-screen camera took over the game's shot
// ("co-op camera resumes" 22:43:07.634). Now an EB0010 CatWalk* shot is a story shot: windows + their command HUD hidden,
// the mod's camera stands down (camera/38), until the shot's EndBattleLevelSequenceActor gets OnStop (log: 6.8 s after the
// start), a cutscene begins, the battle ends, or 20 s.
static ULONGLONG g_StoryShotAt = 0;
static bool g_StoryHeld = false;   // f1.255: held by the mod (seats/24b gate hand-off) - only StoryShotRelease / 90 s / the battle's end end it
static void StoryShotEnd(const char* why)
{
    if (!g_StoryShot) return;
    g_StoryHeld = false;
    g_StoryShot = false;
    CoopLog("STORYSHOT: over after %.1f s (%s) - split screen back (if it was on)", (GetTickCount64() - g_StoryShotAt) / 1000.0, why);
}
static void StoryShotPE(void* obj, void* fn)
{
    if (GetCurrentThreadId() != g_GameThreadId || !obj || !fn) return;
    ULONGLONG now = GetTickCount64();
    if (g_StoryHeld) return;   // f1.255: the mod lets go of it itself
    if (now - g_StoryShotAt > 20000) { StoryShotEnd("20 s"); return; }
    if (g_InCutscene) { StoryShotEnd("a cutscene began"); return; }
    static void* stopFn = nullptr;
    if (stopFn) { if (fn == stopFn) StoryShotEnd("its sequence stopped"); return; }
    static void* lastCls = nullptr; static bool lastSeq = false;
    void* cls = Readable((unsigned char*)obj + 0x10, 8) ? *(void**)((unsigned char*)obj + 0x10) : nullptr;
    if (cls != lastCls) { lastCls = cls; char cn[96] = ""; if (cls) ClassNameOf(cls, cn, sizeof(cn)); lastSeq = strstr(cn, "EndBattleLevelSequenceActor") != nullptr; }
    if (!lastSeq) return;
    char fnm[64] = ""; NameOf(fn, fnm, sizeof(fnm));
    if (!strcmp(fnm, "OnStop")) { stopFn = fn; StoryShotEnd("its sequence stopped"); }
}
static void StoryShotNote(const char* seqName, const char* ownerName)
{
    if (strncmp(ownerName, "EB0010_", 7) || !strstr(seqName, "CatWalk")) return;
    g_StoryShotAt = GetTickCount64();
    if (!g_StoryShot) CoopLog("STORYSHOT: %s (%s) - a story shot: split screen hidden, the game's camera until it's over", seqName, ownerName);
    g_StoryShot = true;
}
static void StoryShotTick()
{
    if (!g_StoryShot) return;
    ULONGLONG age = GetTickCount64() - g_StoryShotAt;
    if (!(InBattleNow() || BattleWorldOn())) StoryShotEnd("the battle is over");
    else if (age > (g_StoryHeld ? 90000ull : 20000ull)) StoryShotEnd(g_StoryHeld ? "held 90 s" : "20 s");
}
// f1.255 (the author, f1.254: "That switch is just JARRING... Maybe the thing to do is when she gets there, switch the split
// screen off ... then when the cutscene ends, turn it back on"; "Okay, yes do it your way"): the gate hand-off runs under a
// held story shot - full screen, the game's camera - from the moment it starts until 0.5 s after the hand-back
static void StoryShotHold(const char* why)
{
    g_StoryShotAt = GetTickCount64();
    if (!g_StoryShot || !g_StoryHeld) CoopLog("STORYSHOT: held by the mod (%s) - split screen hidden, the game's camera", why);
    g_StoryShot = true; g_StoryHeld = true;
}
static void StoryShotRelease(const char* why) { if (g_StoryShot && g_StoryHeld) StoryShotEnd(why); }
typedef int (*GcPlayBcs_t)(void* seq, void* owner, void* res);
static GcPlayBcs_t Orig_GcPlayBcs = nullptr;
static int Hook_GcPlayBcs(void* seq, void* owner, void* res)
{
    uintptr_t from = (uintptr_t)__builtin_return_address(0) - g_Base;
    if (GetCurrentThreadId() == g_GameThreadId && (g_QZ.pending || g_QL.active))
    {   // f1.257 (the author, f1.255: "if you don't get to the catwalk fast enough, it plays the cutscene again and the camera
        // starts flipping out. Can we disable the reminder cutscene switch?"): while the Failed Experiment's ask for player 2's /
        // 3's Tifa is held (seats/24b), its reminder shot (EB0010_00_CatWalk02, every ~20 - 25 s until the leader reaches the
        // gate) isn't played - the game gets -1 (what it gets when a shot can't play). The first CatWalk shot still plays.
        char sn0[64]; GcFName(seq, sn0, sizeof(sn0));
        char on0[96] = ""; if (owner) NameOf(owner, on0, sizeof(on0));
        if (!strcmp(sn0, "EB0010_00_CatWalk02") && !strncmp(on0, "EB0010_", 7))
        {
            static int n = 0; ++n;
            strncpy(g_LastShotOwner, on0, sizeof(g_LastShotOwner) - 1); g_LastShotOwner[sizeof(g_LastShotOwner) - 1] = 0; g_LastShotAt = GetTickCount64();   // f1.258
            QuietLeadShotNote();
            static LogRate rr; if (LogOk(rr, 20)) CoopLog("REMINDER: the Failed Experiment's reminder shot %s skipped (#%d) - player %d's Tifa is on the way to the gate (the game gets -1)", sn0, n, g_QZ.seat + 1);
            return -1;
        }
    }
    int r = Orig_GcPlayBcs(seq, owner, res);
    if (GetCurrentThreadId() != g_GameThreadId) return r;
    QuietLeadShotNote();   // f1.249 (seats/24b): a quiet lead hand-off waits while the game plays its shots
    static LogRate rate;
    char sn[64], on[96] = "null", rn[96] = "-", tn[24] = "nobody";
    GcFName(seq, sn, sizeof(sn));
    if (owner) NameOf(owner, on, sizeof(on));
    StoryShotNote(sn, on);   // f1.253
    strncpy(g_LastShotOwner, on, sizeof(g_LastShotOwner) - 1); g_LastShotOwner[sizeof(g_LastShotOwner) - 1] = 0; g_LastShotAt = GetTickCount64();   // f1.251 (seats/24b)
    if (res && res != owner) NameOf(res, rn, sizeof(rn));
    void* tgt = nullptr;
    unsigned char* u = owner && Readable((unsigned char*)owner + OFF_BATTLE_UNIT, 8) ? *(unsigned char**)((unsigned char*)owner + OFF_BATTLE_UNIT) : nullptr;
    if (u && Readable(u + 0x38, 8)) tgt = TsCharOfHandle(*(void**)(u + 0x38));
    if (tgt) TlShort(tgt, tn, sizeof(tn));
    if (LogOk(rate, 60)) CoopLog("GRABCAM: PlayBattleCameraSequence %s - owner %s (resource %s), its target %s (%s) -> sequence %d%s (called from RVA 0x%llX)",
        sn, on, rn, tn, kTlWho[TlWho(tgt, g_AttnHuman)], r, r < 0 ? " = NOT played" : "", (unsigned long long)from);
    g_GcSeqAt = GetTickCount64(); g_GcOwner = owner; snprintf(g_GcOwnerName, sizeof(g_GcOwnerName), "%s", on);
    snprintf(g_GcWhy, sizeof(g_GcWhy), "sequence %s", sn);
    return r;
}
static void InstallGrabCamLog()
{
    static const unsigned char ex[14] = {0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
    if (void* t = MakeInlineHook(g_Base + 0x9FD5B0, (void*)Hook_GcPlayBcs, 14, ex, "PlayBattleCameraSequence (GRABCAM, log only)")) Orig_GcPlayBcs = (GcPlayBcs_t)t;
}
// the game camera in an actor's frame: forward / right / up (cm) from it by its facing
static void GcRel(void* a, const float cam[3], char* out, size_t n, bool party = false)   // party: a live party member (world position if attached)
{
    float p[3];
    if (!a || !(party ? CharLoc(a, p) : ActorLoc(a, p))) { snprintf(out, n, "-"); return; }
    float yaw = ActorYaw(a) * DEG_TO_RAD, dx = cam[0] - p[0], dy = cam[1] - p[1], dz = cam[2] - p[2];
    float fwd = dx * cosf(yaw) + dy * sinf(yaw), right = -dx * sinf(yaw) + dy * cosf(yaw);
    snprintf(out, n, "%.1f m (fwd %.1f, right %.1f, up %.1f)", sqrtf(dx * dx + dy * dy + dz * dz) / 100.f, fwd / 100.f, right / 100.f, dz / 100.f);
}
static void GcDumpBattleCameras()
{   // every live EndBattleCameraActor (its class or a class made from it)
    static void* cls = nullptr; static bool looked = false;
    if (!looked) { looked = true; cls = FindObjWalk("EndBattleCameraActor", "Class"); CoopLog("GRABCAM: EndBattleCameraActor class %p", cls); }
    if (!cls) return;
    static int offs[16] = {-2};
    static const char* const kP[16] = {"OwnerActor", "TargetActor", "AimTarget0_Actor", "AimTarget1_Actor", "EyeTargetActor", "SequenceActor",
        "AimTarget0_Type", "AimTarget1_Type", "EyeTargetType", "AimTarget0_Offset", "EyeTargetOffset", "EyeTargetRelativeLocation",
        "AimTarget0_RelativeLocation", "bMoveUpEyeWhenHitWall", "AimLeapTime", "EyeLeapTime"};
    if (offs[0] == -2) for (int i = 0; i < 16; ++i) offs[i] = PropOff(cls, kP[i]);
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM), found = 0;
    for (int i = 0; items && i < num && found < 4; ++i)
    {
        unsigned char* o = *(unsigned char**)(items + (size_t)i * 24);
        if (!o) continue;
        void* c = *(void**)(o + 0x10); bool is = false;
        for (int d = 0; c && d < 8; ++d) { if (c == cls) { is = true; break; } c = Readable((unsigned char*)c + 0x30, 8) ? *(void**)((unsigned char*)c + 0x30) : nullptr; }
        if (!is) continue;
        char nm[96]; NameOf(o, nm, sizeof(nm));
        if (!strncmp(nm, "Default__", 9)) continue;
        ++found;
        char an[6][40];
        for (int k = 0; k < 6; ++k) { snprintf(an[k], 40, "-"); if (offs[k] > 0 && Readable(o + offs[k], 8)) if (void* x = *(void**)(o + offs[k])) NameOf(x, an[k], 40); }
        auto b = [&](int k) { return offs[k] > 0 && Readable(o + offs[k], 1) ? (int)o[offs[k]] : -1; };
        auto v = [&](int k, char* s, size_t n) { if (offs[k] > 0 && Readable(o + offs[k], 12)) { float* f = (float*)(o + offs[k]); snprintf(s, n, "(%.0f %.0f %.0f)", f[0], f[1], f[2]); } else snprintf(s, n, "?"); };
        auto f1 = [&](int k) { return offs[k] > 0 && Readable(o + offs[k], 4) ? *(float*)(o + offs[k]) : -1.f; };
        char v0[48], v1[48], v2[48], v3[48]; v(9, v0, 48); v(10, v1, 48); v(11, v2, 48); v(12, v3, 48);
        float loc[3]; char rel[96] = "-"; if (ActorLoc(o, loc) && g_GcOwner) GcRel(g_GcOwner, loc, rel, sizeof(rel));
        CoopLog("GRABCAM: battle camera %s - owner %s, target %s, aim0 %s, aim1 %s, eye %s, sequence %s | types aim0 %d aim1 %d eye %d | aim0 offset %s rel %s, eye offset %s rel %s | move up at walls %d | leap aim %.2f eye %.2f | from the attacker %s",
            nm, an[0], an[1], an[2], an[3], an[4], an[5], b(6), b(7), b(8), v0, v3, v1, v2, b(13), f1(14), f1(15), rel);
    }
    if (!found) CoopLog("GRABCAM: no EndBattleCameraActor alive");
}
// camera/38 Hook_UpdateCamera (the game's final camera this frame)
static void GrabCamTick(unsigned char* pcm)
{
    if (!g_Cfg.targetLog || !pcm || !Readable(pcm + 0x400, 0x1C) || GetCurrentThreadId() != g_GameThreadId) return;
    ULONGLONG now = GetTickCount64();
    static ULONGLONG last = 0, shotAt = 0;
    if (now - last < 250) return;
    last = now;
    const float* cam = (const float*)(pcm + 0x400);
    const float* rot = (const float*)(pcm + 0x40C);
    void* p1 = LeaderChar();
    float pl[3]; bool isFar = false;
    if (InBattleNow() && p1 && CharLoc(p1, pl))
    {
        float dx = cam[0] - pl[0], dy = cam[1] - pl[1], dz = cam[2] - pl[2];
        isFar = dx * dx + dy * dy + dz * dz > g_Cfg.camCutsceneDist * g_Cfg.camCutsceneDist;
    }
    bool on = isFar || now - g_GcSeqAt < 15000 || now < g_GcMoveUntil;
    if (!on || !InBattleNow()) { if (shotAt) { CoopLog("GRABCAM: shot / grab watch over after %.1f s", (now - shotAt) / 1000.0); shotAt = 0; } g_GcOwner = nullptr; g_GcSeqAt = g_GcMoveUntil = 0; return; }
    if (!shotAt)
    {
        shotAt = now;
        CoopLog("GRABCAM: watch starts - %s%s%s", isFar ? "the game camera is far from player 1" : g_GcWhy, isFar && g_GcWhy[0] ? " | last: " : "", isFar && g_GcWhy[0] ? g_GcWhy : "");
        GcDumpBattleCameras();
    }
    static LogRate rate;
    if (!LogOk(rate, 240)) return;
    const char* an = g_GcOwner ? g_GcOwnerName : "-"; char ar[96] = "-";
    if (g_GcOwner) GcRel(g_GcOwner, cam, ar, sizeof(ar));
    char ps[3][200];
    for (int i = 0; i < 3; ++i)
    {
        snprintf(ps[i], sizeof(ps[i]), "-");
        void* ch = GetPartyMember ? GetPartyMember(i) : nullptr;
        if (!ch) continue;
        char cn[24]; TlShort(ch, cn, sizeof(cn));
        char cr[96]; GcRel(ch, cam, cr, sizeof(cr), true);
        float cp[3], op[3]; char ad[24] = "?";
        if (g_GcOwner && CharLoc(ch, cp) && ActorLoc(g_GcOwner, op)) snprintf(ad, sizeof(ad), "%.1f m", sqrtf((cp[0]-op[0])*(cp[0]-op[0]) + (cp[1]-op[1])*(cp[1]-op[1]) + (cp[2]-op[2])*(cp[2]-op[2])) / 100.f);
        char wc[48] = "";
        PlayerSlot* sl = ch == g_AttnHuman[0] ? &g_Player2Slot : ch == g_AttnHuman[1] ? &g_Player3Slot : nullptr;
        if (sl && sl->pipCamOk && sl->pipVisible && CharLoc(ch, cp))
        {
            float dx = sl->pipCamPos[0] - cp[0], dy = sl->pipCamPos[1] - cp[1], dz = sl->pipCamPos[2] - cp[2];
            snprintf(wc, sizeof(wc), ", window camera %.1f m away", sqrtf(dx * dx + dy * dy + dz * dz) / 100.f);
        }
        char at[64] = ""; if (void* par = CharAttachParent(ch)) { char pn[48]; NameOf(par, pn, sizeof(pn)); snprintf(at, sizeof(at), ", ATTACHED to %s", pn); }
        snprintf(ps[i], sizeof(ps[i]), "%s (%s): camera %s, %s from the attacker%s%s", cn, kTlWho[TlWho(ch, g_AttnHuman)], cr, ad, wc, at);
    }
    CoopLog("GRABCAM: +%.2f s | game camera pitch %.0f yaw %.0f fov %.0f%s | from the attacker %s: %s | %s | %s | %s",
        (now - shotAt) / 1000.0, rot[0], rot[1], *(const float*)(pcm + 0x418), isFar ? " (far from player 1)" : "", an, ar, ps[0], ps[1], ps[2]);
}

// =================================================================================
// f1.259 AUTOSTEP (the author: "When would B take over?" - at the gate hand-off, until the jump cutscene, 2 s at most - "Okay,
// go with B"). f1.258 log 00:09:40.8 - 00:09:45.5: after the hand-off the game camera swung round to the new leader and
// player 2's Tifa stood still ~3 s (3 of 81 stick reads moved) before the jump fired; in the f1.255 run she kept moving
// and it fired 0.9 s after. Now the leader's left stick (game slot 0, input/42) points at the jump spot - the jump cutscene
// began with Tifa at (101.5 .. 101.7, 12.9 .. 13.0) m in all five runs - relative to the game camera's yaw, no buttons,
// until a cutscene begins, 2 s, or the hand-off ends. Log "AUTOSTEP: ...".
// =================================================================================
static float g_AsX = 0.f, g_AsY = 0.f; static ULONGLONG g_AsStart = 0;
static void AutoStepAim()
{
    void* c = LeaderChar(); float p[3];
    if (!c || !CharLoc(c, p)) return;
    float dx = g_AsX - p[0], dy = g_AsY - p[1];
    if (dx * dx + dy * dy < 30.f * 30.f) return;   // within 0.3 m - keep the last direction (it fires on the way through)
    float want = atan2f(dy, dx) * RAD_TO_DEG, rel = (want - g_GameCamRot[1]) * DEG_TO_RAD;
    g_AutoStepLX = (SHORT)(sinf(rel) * 32000.f); g_AutoStepLY = (SHORT)(cosf(rel) * 32000.f);
}
static void AutoStepStart(float tx, float ty)
{
    g_AsX = tx; g_AsY = ty; g_AsStart = GetTickCount64();
    AutoStepAim();
    g_AutoStepUntil = g_AsStart + 3000; g_AutoStepOn = true;   // f1.282: was 2 s (f1.283: f1.282 had the "on" switch inside this comment - no steering)
    void* c = LeaderChar(); float p[3] = {0, 0, 0}; if (c) CharLoc(c, p);
    CoopLog("AUTOSTEP: the leader is steered to the jump spot (%.1f, %.1f) m from (%.1f, %.1f) m - stick %d / %d (camera yaw %.0f), 3 s at most",
        tx / 100.f, ty / 100.f, p[0] / 100.f, p[1] / 100.f, (int)g_AutoStepLX, (int)g_AutoStepLY, g_GameCamRot[1]);
}
static void AutoStepStop(const char* why)
{
    if (!g_AutoStepOn) return;
    g_AutoStepOn = false;
    void* c = LeaderChar(); float p[3] = {0, 0, 0}; if (c) CharLoc(c, p);
    CoopLog("AUTOSTEP: over after %.2f s (%s) - leader at (%.1f, %.1f) m", (GetTickCount64() - g_AsStart) / 1000.0, why, p[0] / 100.f, p[1] / 100.f);
}
static void AutoStepTick()
{
    if (!g_AutoStepOn) return;
    if (g_InCutscene || g_CutSince > g_AsStart) { AutoStepStop("the jump cutscene began"); return; }
    if (GetTickCount64() >= g_AutoStepUntil) { AutoStepStop("3 s - no cutscene yet, the player has the stick again"); return; }
    AutoStepAim();
}
