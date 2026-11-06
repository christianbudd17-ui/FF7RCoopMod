// players 2/3's reticle = the GAME'S OWN target reticle (BattleTarget_New_C), in their color ([General] GameReticle=1).
// The author (2026-10-25, after f1.175's enemy bars passed: "Okay, works great..."): "Can we use the original games reticle,
// but recolor it?"
// Exe RE: the target icon manager = a 0x60-byte FGCObject (vtable 0x4BF1240, made in 0x13B1A70 at 0x13B204E - 0x13B20A4,
// menu system +0x90); +0x08 a TSet of icons (0x60 bytes: +0x00 weak icon, +0x08 its world position), +0x58 the current icon.
//  - 0x13E6D60 Show(manager, UWorld, FVector*, EEndMenuBattleTargetState) (= EndMenuAPI ShowBattleTargetIcon): the current
//    icon plays its Out animation and joins the set, a new one is made (In animation), added to the set, returned. States:
//    1 Enabled (soft target), 3 LockedEnabled (lock-on) - the game picks 3 / 1 from the battle manager +0x388 (0x9B98E3).
//  - 0x13E7D20 Tick(manager) (menu system tick): every icon in the set placed from its stored world position (0x13E7BE0),
//    animations run, faded-out ones dropped.
//  - Hide (EndMenuAPI HideBattleTargetIcon): icon->RemoveFromParent (vfunc +0x250), +0x58 = it, removed from the set
//    (0x13F6950(set, &weak key)).
//  - SetBattleTargetIconPosition (0x1396240) only looks in the GAME'S manager - so the mod writes its icon's stored position
//    in its own manager's set instead.
// Players 2 and 3 each get a second manager (made like the game's, as the enemy bars - pip_hud/36f). DrawP2Marker
// (marker/19) calls TiDraw for the player's target while their window shows: a new icon when the target / lock-on changes
// (the game's own In / Out animations), the stored position = the point on player 1's camera ray that lands where their
// window shows the target (EsMapWorld), the whole widget tinted with the player's color (UUserWidget ColorAndOpacity -
// multiplies the reticle's own colors), hidden outside their window. The mod's drawn reticle (GameReticle) is skipped
// then; the P2 / P3 label and the off-screen pointer stay. GameReticle=0 = the drawn reticle as before.
static unsigned char* g_TiClone[4] = {};
static unsigned char* g_TiReal = nullptr;   // the game's manager (seen in its tick)
struct TiSt { void* tgt = nullptr; int state = -1, iIdx = -1, iSer = 0, miss = 0, detach = 0, visBad = 0; bool att = false; ULONGLONG used = 0, made = 0; unsigned char vis0 = 4; bool hidden = false; unsigned char accent[42] = {}; };
static TiSt g_Ti[4];
typedef void (*TiTick_t)(void* mgr);
static TiTick_t Orig_TiTick = nullptr;
static unsigned char* TiMakeClone()
{
    typedef void* (*Malloc_t)(size_t, uint32_t);
    typedef void (*BitInit_t)(void*, uint64_t, uint64_t);
    unsigned char* c = (unsigned char*)((Malloc_t)(g_Base + 0x1C32C90))(0x60, 0);
    if (!c) return nullptr;
    memset(c, 0, 0x60);
    ((void (*)(void*))(g_Base + 0xD82980))(c);            // FGCObject base (the garbage collector)
    *(void**)c = (void*)(g_Base + 0x4BF1240);             // the target icon manager's vtable (as 0x13B207B)
    ((BitInit_t)(g_Base + 0x63F170))(c + 0x18, 0, 0);     // the icon set's allocation bits (as 0x13B208E)
    *(int*)(c + 0x38) = -1; *(int*)(c + 0x3C) = 0;
    return c;
}
static bool TiIsOurs(void* w)   // menus/35b: player 1's reticle search skips ours
{
    if (!w) return false;
    for (int pn = 2; pn <= 3; ++pn)
    {
        unsigned char* c = g_TiClone[pn]; if (!c) continue;
        if (*(void**)(c + 0x58) == w) return true;
        unsigned char* data = *(unsigned char**)(c + 0x08); int num = *(int*)(c + 0x10);
        unsigned* bits = *(unsigned**)(c + 0x28); if (!bits) bits = (unsigned*)(c + 0x18);
        for (int i = 0; data && i < num && i < 64; ++i)
        {
            if (!Readable(bits + (i >> 5), 4) || !(bits[i >> 5] >> (i & 31) & 1)) continue;
            unsigned char* e = data + (size_t)i * 0x60;
            if (Readable(e, 8) && ResolveWeak(*(int*)e, *(int*)(e + 4)) == w) return true;
        }
    }
    return false;
}
// the icon's entry in its manager's set (0x60 bytes: +0x00 weak icon, +0x08 world position, +0x1C lock-on flag)
static unsigned char* TiEntry(unsigned char* c, void* icon)
{
    int ki, ks; if (!MakeWeak(icon, &ki, &ks)) return nullptr;
    unsigned char* data = *(unsigned char**)(c + 0x08); int num = *(int*)(c + 0x10);
    unsigned* bits = *(unsigned**)(c + 0x28); if (!bits) bits = (unsigned*)(c + 0x18);
    for (int i = 0; data && i < num && i < 64; ++i)
    {
        if (!Readable(bits + (i >> 5), 4) || !(bits[i >> 5] >> (i & 31) & 1)) continue;
        unsigned char* e = data + (size_t)i * 0x60;
        if (Readable(e, 0x60) && *(int*)e == ki && *(int*)(e + 4) == ks) return e;
    }
    return nullptr;
}
static void TiHide(int pn)
{
    unsigned char* c = g_TiClone[pn]; TiSt& s = g_Ti[pn];
    void* icon = ResolveWeak(s.iIdx, s.iSer);
    if (c && icon && Readable(icon, 8))
    {
        void** vt = *(void***)icon;
        if (vt && Readable(vt + 0x250 / 8, 8)) ((void (*)(void*))vt[0x250 / 8])(icon);   // RemoveFromParent (as the game's Hide)
        *(void**)(c + 0x58) = icon;
        int key[2]; if (MakeWeak(icon, &key[0], &key[1])) ((void (*)(void*, int*))(g_Base + 0x13F6950))(c + 0x08, key);
    }
    s = TiSt();
}
static void Hook_TiTick(void* mgr)
{
    if (GetCurrentThreadId() != g_GameThreadId) { Orig_TiTick(mgr); return; }
    g_TiReal = (unsigned char*)mgr;
    Orig_TiTick(mgr);
    ULONGLONG now = GetTickCount64();
    for (int pn = 2; pn <= 3; ++pn)
    {
        unsigned char* c = g_TiClone[pn]; if (!c) continue;
        TiSt& s = g_Ti[pn];
        if (s.iIdx >= 0 && now - s.used > 150)
        {
            static LogRate logs; if (LogOk(logs, 8)) { CoopLog("GAMERETICLE: player %d's reticle put away (no target in their window)", pn); }
            TiHide(pn);
        }
        Orig_TiTick(c);   // the game places and animates its icons (and drops faded ones)
    }
}
// (the author on f1.176's whole-widget tint: "clearly the white gets the most of the color"): the reticle's BLUE
// images (EndImageSet ColorAndOpacity 0 0.6 1) get the player's color, the white ones stay white - like player 1's, with
// blue swapped for their color. Set again whenever the game (an animation) puts the blue back.
static void* g_IsColFn = nullptr; static int g_IsColOff = -1;
static void TiAccents(TiSt& s, void* icon)
{
    for (int k = 0; k < 42; ++k)
    {
        if (!s.accent[k]) continue;
        unsigned char* is = Readable((unsigned char*)icon + 0x3E0 + k * 8, 8) ? *(unsigned char**)((unsigned char*)icon + 0x3E0 + k * 8) : nullptr;
        if (!is || !Readable(is + 0x108, 16)) continue;
        if (!g_IsColFn)
        {
            g_IsColFn = FindFunc(is, "SetColorAndOpacity");
            g_IsColOff = g_IsColFn ? ParamOff(g_IsColFn, "InColorAndOpacity") : -1;
            CoopLog("GAMERETICLE: EndImageSet SetColorAndOpacity %s (+%d)", g_IsColFn && g_IsColOff >= 0 ? "found - the reticle's blue parts take the player's color" : "NOT found", g_IsColOff);
            if (!g_IsColFn || g_IsColOff < 0 || g_IsColOff + 16 > 64 || ParmsSize(g_IsColFn) > 64) { g_IsColFn = (void*)1; g_IsColOff = -1; }
        }
        if (g_IsColOff < 0) return;
        float* cc = (float*)(is + 0x108);
        if (cc[0] == g_Player->color[0] && cc[1] == g_Player->color[1] && cc[2] == g_Player->color[2]) continue;
        float col[4] = {g_Player->color[0], g_Player->color[1], g_Player->color[2], cc[3]};
        alignas(16) unsigned char b[64] = {};
        memcpy(b + g_IsColOff, col, 16); Orig_ProcessEvent(is, g_IsColFn, b);
    }
}
// (the author, f1.180 test: "After P2 went down, his reticle went away and did not return ... I couldn't see his
// reticle with split screen on or off"): the f1.178 log couldn't tell why (every GAMERETICLE line had used up its count by
// then). Now: (a) is the icon still attached (UserWidget IsInViewport, or a parent widget)? once it was and then isn't for
// 3 frames = made again; (b) its Visibility Hidden / Collapsed while we want it shown for 3 frames = shown again;
// (c) one "GAMERETICLE STATE" line per player whenever its state changes (at most one per 2 s each).
static void* g_TiVpFn = nullptr; static int g_TiVpRet = -1;
static void* g_TiParFn = nullptr; static int g_TiParRet = -1;
static int g_TiOpOff = -2;
static int TiAttached(void* icon)   // 1 attached, 0 not, -1 can't tell
{
    if (!icon || !Readable(icon, 0x30)) return -1;
    static int state = 0;
    if (state == 0)
    {
        state = -1;
        g_TiVpFn = FindFunc(icon, "IsInViewport"); g_TiVpRet = g_TiVpFn ? ParamOff(g_TiVpFn, "ReturnValue") : -1;
        g_TiParFn = FindFunc(icon, "GetParent"); g_TiParRet = g_TiParFn ? ParamOff(g_TiParFn, "ReturnValue") : -1;
        if ((g_TiVpFn && g_TiVpRet >= 0 && ParmsSize(g_TiVpFn) <= 32) || (g_TiParFn && g_TiParRet >= 0 && ParmsSize(g_TiParFn) <= 32)) state = 1;
        CoopLog("GAMERETICLE: attach check %s (IsInViewport %s, GetParent %s)", state == 1 ? "ready" : "NOT available", g_TiVpFn ? "found" : "-", g_TiParFn ? "found" : "-");
    }
    if (state != 1) return -1;
    if (g_TiVpFn && g_TiVpRet >= 0)
    {
        alignas(16) unsigned char b[32] = {}; Orig_ProcessEvent(icon, g_TiVpFn, b);
        if (b[g_TiVpRet]) return 1;
    }
    if (g_TiParFn && g_TiParRet >= 0)
    {
        alignas(16) unsigned char b[32] = {}; Orig_ProcessEvent(icon, g_TiParFn, b);
        void* par = nullptr; memcpy(&par, b + g_TiParRet, 8);
        if (par) return 1;
    }
    return 0;
}
static void TiStateLog(int pn, const TiSt& s, void* icon, bool in, bool entry, int att, bool inPip)
{
    if (!g_DetailedLog) return;
    int vis = (icon && g_EsVisOff > 0 && Readable((unsigned char*)icon + g_EsVisOff, 1)) ? ((unsigned char*)icon)[g_EsVisOff] : -1;
    if (g_TiOpOff == -2 && icon) g_TiOpOff = PropOff(*(void**)((unsigned char*)icon + 0x10), "RenderOpacity");
    float op = (icon && g_TiOpOff > 0 && Readable((unsigned char*)icon + g_TiOpOff, 4)) ? *(float*)((unsigned char*)icon + g_TiOpOff) : -1.f;
    float ca = (icon && Readable((unsigned char*)icon + 0x100, 16)) ? ((float*)((unsigned char*)icon + 0x100))[3] : -1.f;
    int key = (icon ? 1 : 0) | (in ? 2 : 0) | (entry ? 4 : 0) | ((att + 1) << 3) | (s.hidden ? 32 : 0) | (inPip ? 64 : 0) | ((vis & 7) << 7) | ((op > 0.5f ? 1 : 0) << 10) | ((ca > 0.5f ? 1 : 0) << 11);
    static int lastKey[4] = {-1, -1, -1, -1}; static ULONGLONG lastT[4] = {};
    ULONGLONG now = GetTickCount64();
    if (key == lastKey[pn] || now - lastT[pn] < 2000) return;
    lastKey[pn] = key; lastT[pn] = now;
    char tn[96] = "-"; if (s.tgt) NameOf(s.tgt, tn, sizeof(tn));
    CoopLog("GAMERETICLE STATE: player %d on %s - icon %s, %s, entry %s, attached %s, visibility %d (%s by us), opacity %.2f, color alpha %.2f",
            pn, tn, icon ? "yes" : "NO", inPip ? (in ? "in their window" : "outside their window") : "shared screen", entry ? "yes" : "no",
            att < 0 ? "?" : att ? "yes" : "NO", vis, s.hidden ? "hidden" : "shown", op, ca);
}
// players 2 / 3's reticle offset on the shared screen (split screen off), in screen pixels: 2 up, 3 down
static float TiSharedNudge(int pn)
{
    float H = g_PipCanvasH > 100.f ? g_PipCanvasH : 1080.f;
    float px = 8.f * H / 1080.f;
    return pn == 2 ? -px : pn == 3 ? px : 0.f;
}
// DrawP2Marker (marker/19): true = the game's reticle shows this player's target in their window (skip the drawn one)
static bool TiDraw(int pn, void* tgt, bool locked, const float* mid, bool inPip)
{
    if (pn < 2 || pn > 3 || !tgt || !g_TiReal || !g_P1Pc || GetCurrentThreadId() != g_GameThreadId) return false;
    unsigned char*& c = g_TiClone[pn];
    if (!c)
    {
        static bool failed[4] = {};
        if (failed[pn]) return false;
        c = TiMakeClone();
        CoopLog("GAMERETICLE: player %d's target icon manager %s (%p)", pn, c ? "made - their reticle is the game's own" : "could NOT be made", c);
        if (!c) { failed[pn] = true; return false; }
    }
    TiSt& s = g_Ti[pn];
    void* icon = ResolveWeak(s.iIdx, s.iSer);
    // (the author's screenshot 23:35: "Lock on is not right though"): as the game's own reticle code (0x9B9170) - ONE
    // icon, made once (Show) and kept: a new target only moves it (it slides over, like player 1's); every frame the game
    // writes the lock-on flag into the icon's set entry (+0x1C = battle manager +0x388) and the icon's tick shows the
    // lock-on look from that. f1.176 / f1.177 made a new icon per target / lock change and never set the flag.
    if (!icon)
    {
        void** vt = *(void***)g_P1Pc;
        void* world = (vt && Readable(vt + 0x140 / 8, 8)) ? ((void* (*)(void*))vt[0x140 / 8])(g_P1Pc) : nullptr;   // UObject::GetWorld
        if (!world) return false;
        float p[3] = {mid[0], mid[1], mid[2]};
        int state = locked ? 3 : 1;
        icon = ((void* (*)(void*, void*, float*, uint64_t))(g_Base + 0x13E6D60))(c, world, p, (uint64_t)state);
        static LogRate logs;
        if (LogOk(logs, 12)) { char tn[96] = "-"; NameOf(tgt, tn, sizeof(tn)); CoopLog("GAMERETICLE: player %d's reticle %s on %s (%s)", pn, icon ? "made" : "NOT made", tn, locked ? "locked on" : "soft target"); }
        if (!icon) return false;
        MakeWeak(icon, &s.iIdx, &s.iSer); s.hidden = false; s.made = GetTickCount64(); s.miss = 0;
        s.vis0 = (g_EsVisOff > 0 && Readable((unsigned char*)icon + g_EsVisOff, 1)) ? ((unsigned char*)icon)[g_EsVisOff] : 4;
        // which of its images carry the game's blue (f1.176 log: Ims_* are white 1 1 1 or blue 0 0.6 1)
        for (int k = 0; k < 42; ++k)
        {
            unsigned char* is = Readable((unsigned char*)icon + 0x3E0 + k * 8, 8) ? *(unsigned char**)((unsigned char*)icon + 0x3E0 + k * 8) : nullptr;
            float* cc = (is && Readable(is + 0x108, 16)) ? (float*)(is + 0x108) : nullptr;
            s.accent[k] = cc && cc[2] > 0.8f && cc[0] < 0.3f;   // blue, not white
        }
    }
    if (tgt != s.tgt || (int)locked != s.state)
    {
        static LogRate logs;
        if (LogOk(logs, 16)) { char tn[96] = "-"; NameOf(tgt, tn, sizeof(tn)); CoopLog("GAMERETICLE: player %d's reticle on %s (%s)", pn, tn, locked ? "locked on" : "soft target"); }
        s.tgt = tgt; s.state = locked;
    }
    s.used = GetTickCount64();
    // where: in their window the point on player 1's camera ray that lands where their window shows the target; without
    // windows (split screen off) the target itself (player 1's view = the whole screen)
    float w2[3];
    bool in = inPip ? EsMapWorld(pn, mid, w2) : true;
    if (!inPip)
    {   // (the author: "p2 a few pixels up, p3 a few pixels down when not in split screen so they don't just stack"):
        // on the shared screen the point is moved TiSharedNudge() pixels up / down on player 1's screen - projected with
        // player 1's camera (0x1348940), moved, and turned back into a world point at the same distance (DeprojectScreenToWorld)
        w2[0] = mid[0]; w2[1] = mid[1]; w2[2] = mid[2];
        float sc[2], o[3], d[3];
        if (g_P1Pc && ((bool (*)(void*, const float*, float*))(g_Base + 0x1348940))(g_P1Pc, mid, sc))
        {
            sc[1] += TiSharedNudge(pn);
            if (PopDeproject(g_P1Pc, sc, o, d))
            {
                float dx = mid[0] - o[0], dy = mid[1] - o[1], dz = mid[2] - o[2];
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                w2[0] = o[0] + d[0] * dist; w2[1] = o[1] + d[1] * dist; w2[2] = o[2] + d[2] * dist;
            }
        }
    }
    unsigned char* e = in ? TiEntry(c, icon) : nullptr;
    // (the author: "briefly I felt like I lost my reticle as P3"): if the game's tick dropped the icon's entry (the
    // icon object is still there, so it was never made again) the reticle froze / vanished. Entry gone for 3 frames (and
    // the icon older than 0.3 s) = put it away; the next frame makes a new one.
    if (in && !e && s.used - s.made > 300) ++s.miss; else s.miss = 0;
    // (the f1.181 log showed "attached NO" for every icon - the game's icons sit in neither the viewport nor a parent
    //  UMG widget; after 300 frames with no "yes" the check stops, so it costs nothing)
    static int attYes = 0, attCalls = 0;
    int att = (attYes || attCalls < 300) ? TiAttached(icon) : -1;
    if (att == 1) ++attYes; else if (att == 0) ++attCalls;
    if (att == 1) { s.att = true; s.detach = 0; } else if (att == 0 && s.att) ++s.detach;
    TiStateLog(pn, s, icon, in, e != nullptr, att, inPip);
    if (s.miss >= 3 || s.detach >= 3)
    {
        static LogRate logs; if (LogOk(logs, 20)) { CoopLog("GAMERETICLE: player %d's reticle %s - made again", pn, s.miss >= 3 ? "lost its place in the game's list" : "was taken off the screen"); }
        void** vt = Readable(icon, 8) ? *(void***)icon : nullptr;
        if (vt && Readable(vt + 0x250 / 8, 8)) ((void (*)(void*))vt[0x250 / 8])(icon);   // RemoveFromParent
        s = TiSt();
        return true;
    }
    if (e)
    {
        memcpy(e + 8, w2, 12);
        e[0x1C] = locked ? 1 : 0;   // the lock-on flag (as 0x9B99C2)
    }
    TiAccents(s, icon);   // the game's blue parts in the player's color, white stays white (the author 2026-10-26: no other choice)
    if (!in) { if (!s.hidden) { EsCollapse(icon); s.hidden = true; } }
    else if (s.hidden) { EsSetVis(icon, s.vis0 == 1 || s.vis0 == 2 ? 4 : s.vis0); s.hidden = false; }
    else if (g_EsVisOff > 0 && Readable((unsigned char*)icon + g_EsVisOff, 1))
    {   // we want it shown, but something hid it (Hidden / Collapsed) - show it again after 3 frames
        unsigned char v = ((unsigned char*)icon)[g_EsVisOff];
        if (v == 1 || v == 2) { if (++s.visBad >= 3) { s.visBad = 0; EsSetVis(icon, s.vis0 == 1 || s.vis0 == 2 ? 4 : s.vis0);
            static LogRate logs; if (LogOk(logs, 20)) { CoopLog("GAMERETICLE: player %d's reticle was hidden by something else - shown again", pn); } } }
        else s.visBad = 0;
    }
    // research (once): the game reticle's own colors - is the tint enough, or does each image need its color?
    static bool dumped = true;   // done (f1.176 log)
    if (!dumped)
    {
        dumped = true;
        unsigned char* r = (unsigned char*)icon; int n = 0;
        char line[900] = ""; int pos = 0;
        for (int off = 0x3E0; off <= 0x528 && n < 12; off += 0x10)
        {
            unsigned char* is = Readable(r + off, 8) ? *(unsigned char**)(r + off) : nullptr;
            if (!is || !Readable(is + 0x108, 16)) continue;
            float* cc = (float*)(is + 0x108); char nm[64]; NameOf(is, nm, sizeof(nm)); ++n;
            if (pos < 820) pos += snprintf(line + pos, sizeof(line) - pos, "%s%s %.2f %.2f %.2f %.2f", pos ? ", " : "", nm, cc[0], cc[1], cc[2], cc[3]);
        }
        CoopLog("GAMERETICLE: the reticle's images: %s", line);
    }
    return true;
}
static void InstallGameReticleHook()
{
    static const unsigned char ex[15] = {0x40,0x55,0x53,0x48,0x8D,0x6C,0x24,0x98,0x48,0x81,0xEC,0x68,0x01,0x00,0x00};
    void* t = MakeInlineHook(g_Base + 0x13E7D20, (void*)Hook_TiTick, 15, ex, "target icon tick (GameReticle)");
    if (t) Orig_TiTick = (TiTick_t)t;
    CoopLog("GAMERETICLE: hook %s", t ? "installed" : "NOT installed - players 2/3 keep the drawn reticle");
}
