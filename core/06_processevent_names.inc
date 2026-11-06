// ---- UObject::ProcessEvent (RVA 0x1E9BA20): every UFunction call goes through it ----
static constexpr uintptr_t RVA_PROCESS_EVENT = 0x1E9BA20;
static constexpr uintptr_t RVA_FNAME_TOSTRING = 0x1CE89A0;
typedef void (*ProcessEvent_t)(void* obj, void* fn, void* params);
typedef void (*FNameToString_t)(void* fname, void* outFString);
static ProcessEvent_t Orig_ProcessEvent = nullptr;
static DWORD g_GameThreadId = 0;
static volatile const char* g_Crumb = "start";
static void NameOf(void* uobj, char* out, size_t outLen);

static int ClassifyFn(void* fn);
// f1.246 GATELOG (log only, seats/13b): a watch window after the game's script asks for a person's character as the leader
static volatile ULONGLONG g_GateUntil = 0;
static void GateLogPE(void* obj, void* fn);
static void GateLogLeaderAsk(int kind, uintptr_t site, int answerType);
static void GateWatchStart(void* who, uintptr_t site, bool inBattle);
static void GateCutsceneNote(void* obj);   // seats/13b (f1.249)
static void AttnEvent(int fc, void* fn, void* ctrl, unsigned char* prm);
static void BossActEvent(int fc, void* fn, void* ctrl, unsigned char* prm);   // enemies/22g (f1.226, log only)
static bool BossEventPre(int fc, void* fn, void* ctrl, unsigned char* prm);   // enemies/22f (f1.231 / f1.233): before a boss's damage event handler runs
static void BossEventPost();
static volatile bool g_InCutscene = false;
// f1.235 (the author, boss rush f1.234 21:48 - 21:59: "I've been loading saves to get to the bosses - and the mod quit
// working"): a security soldier's AI controller got OnBeginCutScene at 21:48:35 (the shot before a fight) and NO
// OnEndCutScene ever came - g_InCutscene stayed on for the next 11 minutes and three save loads: split screen hidden,
// players 2/3's command HUD hidden and their menus blocked, the marker / status circles off, hotkeys "ignored (cutscene)".
// (the game sends Begin to the AI controller that watches a unit, End to the unit's own controller - core RE f1.235
// 0x91ECA2 / 0x8EAFF2 - so a pair can go missing.) Now, besides the game's End, a cutscene is also over when:
//  - the controller it began on is gone (a save load / level change destroys it) - checked each HUD frame;
//  - player 1 opens their command menu (the game doesn't allow it in a cutscene) - menus/08;
//  - outside a battle, the field controls are on and the game's camera is back on player 1 for 2 s - core/27a.
// (the last two only from CUT_SAFETY_AFTER_MS after the Begin, so a menu / field-controls state left from just before
// it can't end it at once)
static int g_CutIdx = -1, g_CutSer = 0;           // weak reference to the object the cutscene began on
static ULONGLONG g_CutSince = 0;                  // when it began (GetTickCount64)
static constexpr ULONGLONG CUT_SAFETY_AFTER_MS = 2000;
static bool  MakeWeak(void* obj, int* outIndex, int* outSerial);   // core/28
static void* ResolveWeak(int idx, int serial);
static ULONGLONG g_CutGoneAt = 0, g_CutBackSince = 0;
// f1.251 (the author, Failed Experiment f1.250: "the cutscene the first time doesnt disable split screen ... maybe it sees the
// cutscene as over before it flips the camera back to P1"): the jump cutscene began on Barret's AI controller (22:07:38.256),
// a MINION's controller got OnEndCutScene at 22:07:57.695 and ended it for the mod - the party's own ends came 5 s later
// (22:08:02.828). Now a cutscene that began on a party member's controller (name "PC...") ignores an enemy's end and waits
// for a party member's (max 15 s after the first enemy end, then it ends).
static bool g_CutOnParty = false;
static ULONGLONG g_CutEnemyEndAt = 0;
static char g_LastShotOwner[64] = "";   // f1.251 (camera/38b): the owner of the game's last battle camera shot ...
static ULONGLONG g_LastShotAt = 0;      // ... and when (seats/24b: the Failed Experiment's ask comes right after its catwalk shot)
// f1.253 (camera/38b): a story shot is on - the Failed Experiment's catwalk shots (EB0010 ... CatWalk*), from its
// PlayBattleCameraSequence until its sequence actor's OnStop (max 20 s): split screen hidden, the mod's camera stands down
static volatile bool g_StoryShot = false;
static void StoryShotPE(void* obj, void* fn);   // camera/38b
static void StoryShotTick();                     // camera/38b
static void StoryShotHold(const char* why);      // camera/38b (f1.255): a story shot the mod holds (QUIETLEAD gate hand-off) ...
static void StoryShotRelease(const char* why);   // ... until it lets go
static void AutoStepStart(float tx, float ty);   // camera/38b (f1.259): steer the leader to (tx, ty) cm for up to 2 s
static void AutoStepTick();                      // camera/38b (f1.259)
static void AutoStepStop(const char* why);       // camera/38b (f1.259)   // f1.244: its object gone since / the game camera back on player 1 since
static bool CutsceneCamBackOnP1();   // camera/38 (f1.244)
static void CutsceneClear(const char* why)
{
    if (!g_InCutscene) return;
    g_InCutscene = false; g_CutIdx = -1; g_CutSer = 0; g_CutGoneAt = g_CutBackSince = 0; g_CutEnemyEndAt = 0;
    CoopLog("CUTSCENE: ends - %s (the game sent no end) - co-op camera back", why);
}
static bool CutsceneSafetyReady() { return g_InCutscene && GetTickCount64() - g_CutSince >= CUT_SAFETY_AFTER_MS; }
// f1.244 (the author, Reno & Rude 19:55:00 screenshot: "Cutscene didn't turn off screen 2 at this point"): the Rude-joins
// cutscene began on EB0003_12_HelicopterTurks_MoveControl (19:54:56); the helicopter object went away INSIDE the cutscene
// (19:54:59.9) and the f1.235 rule ended it at once - the split screen came back over the game's cutscene shot (fov 15, 38 m
// from player 1). Now "its object is gone" ends a cutscene only once the game's camera is back on player 1 (within 15 m, a
// normal field of view) for 1 s - after a save load that is at once - or 60 s after the object went.
static void CutsceneCheckGone()   // game thread, each HUD frame
{
    if (g_InCutscene && g_CutEnemyEndAt && GetTickCount64() - g_CutEnemyEndAt > 15000)
    { CutsceneClear("an enemy's end came 15 s ago and no party member's"); return; }   // f1.251
    if (!(g_InCutscene && g_CutSer && !ResolveWeak(g_CutIdx, g_CutSer))) return;
    ULONGLONG now = GetTickCount64();
    if (!g_CutGoneAt) { g_CutGoneAt = now; g_CutBackSince = 0; CoopLog("CUTSCENE: the object it began on is gone - it ends when the game's camera is back on player 1"); }
    if (!CutsceneCamBackOnP1()) g_CutBackSince = 0; else if (!g_CutBackSince) g_CutBackSince = now;
    if (g_CutBackSince && now - g_CutBackSince >= 1000) CutsceneClear("the object it began on is gone and the game's camera is back on player 1 (save load / level change / the scene is over)");
    else if (now - g_CutGoneAt > 60000) CutsceneClear("the object it began on has been gone for 60 s");
}
static volatile ULONGLONG g_MenuOpenTick = 0;   // (PERF): last time a command menu was seen open (passes/12 MenuSlowTick)
static volatile bool g_TalkScene = false;    // the game took the field controls away outside battle (talking scene / scripted moment) - core/27a
static volatile bool g_ScriptedShot = false; // the game's camera is a cutscene camera or far from player 1 (camera/38) - windows hide
static volatile bool g_QuietLeadHide = false; // f1.248 (seats/24b QUIETLEAD): players 2 / 3's windows + command HUD stay hidden during a quiet lead hand-off
static void DrawP2Marker(void* hud);
static void DrawCoopHud(void* hud);
static void PipTick(void* pc);
static void PipKeysTick();             // (pip_hud/30)
static void IconTick();                // (input/40c)
static void IconRefreshSoon();         // (input/40c): F6 - every text with button icons rebuilt in its player's style
static void ListenerTick(void* pc);
static void PipDraw(void* canvas);
static void PipDrawUnderHud(void* hud);
static void Hook_ProcessEvent(void* obj, void* fn, void* params)
{
    int fc = ClassifyFn(fn); // (cached per function)
    if (g_GateUntil) GateLogPE(obj, fn);   // f1.246 (log only)
    if (g_StoryShot) StoryShotPE(obj, fn);  // f1.253
    if (fc == 1) { CutsceneCheckGone(); PipDrawUnderHud(obj); }   // picture-in-picture first, the game's HUD on top
    bool bossEv = (fc == 5 || fc == 15 || fc == 16) && BossEventPre(fc, fn, obj, (unsigned char*)params);   // f1.231 / f1.233: players 2 / 3's hits count as the player's for a boss's script
    Orig_ProcessEvent(obj, fn, params);
    if (bossEv) BossEventPost();
    if (fc == 1) DrawCoopHud(obj); // (status circles + P2 marker)
    else if (fc == 2 && !g_InCutscene)
    {
        g_InCutscene = true; g_CutSince = GetTickCount64(); g_CutGoneAt = g_CutBackSince = 0; g_CutEnemyEndAt = 0;
        if (!MakeWeak(obj, &g_CutIdx, &g_CutSer)) { g_CutIdx = -1; g_CutSer = 0; }
        char on[96] = "?"; NameOf(obj, on, sizeof(on)); CoopLog("CUTSCENE: begins (%s) - co-op camera hands over to the game", on);
        g_CutOnParty = !strncmp(on, "PC", 2);   // f1.251
        GateCutsceneNote(obj);   // f1.249 (log only)
    }
    else if (fc == 3 && g_InCutscene)
    {
        char en[96] = "?"; if (g_CutOnParty) NameOf(obj, en, sizeof(en));
        if (g_CutOnParty && strncmp(en, "PC", 2))
        {   // f1.251: began on a party member - an enemy's end doesn't end it
            if (!g_CutEnemyEndAt) { g_CutEnemyEndAt = GetTickCount64(); CoopLog("CUTSCENE: %s got its end - the cutscene began on a party member, it goes on until theirs (max 15 s)", en); }
        }
        else { g_InCutscene = false; g_CutIdx = -1; g_CutSer = 0; g_CutEnemyEndAt = 0; CoopLog("CUTSCENE: ends - co-op camera back"); }
    }
    else if (fc >= 4 && fc <= 5) AttnEvent(fc, fn, obj, (unsigned char*)params);   // (AttnEvent ignored 6 / 7 already)
    else if (fc >= 6 && fc <= 14) BossActEvent(fc, fn, obj, (unsigned char*)params);   // f1.226: a boss's moves (log only)
}

struct FStringRaw { wchar_t* data; int num; int max; };
static void NameOf(void* uobj, char* out, size_t outLen)
{
    out[0] = 0;
    if (!uobj) { strcpy_s(out, outLen, "null"); return; }
    FStringRaw s{};
    ((FNameToString_t)(g_Base + RVA_FNAME_TOSTRING))((unsigned char*)uobj + 0x18, &s);
    if (s.data)
    {
        WideCharToMultiByte(CP_UTF8, 0, s.data, -1, out, (int)outLen, nullptr, nullptr);
        typedef void (*Free_t)(void*);
        ((Free_t)(g_Base + 0x16C0370))(s.data); // FMemory::Free - NameOf used to leak this
    }
}
// (efficiency review B1 / C16 / F1 / F3): a CLASS's name, cached. The widget code asked for the same few class names
// dozens of times a frame (each NameOf = a game call + a heap string). Keyed on the class pointer AND its FName (+0x18), so
// an address reused by another class can't give the old name. Game thread only (other threads: a plain NameOf).
struct ClsNameE { void* cls; uint64_t fname; char nm[96]; };
static ClsNameE g_ClsName[1024];
static void ClassNameOf(void* cls, char* out, size_t n)
{
    if (!cls || GetCurrentThreadId() != g_GameThreadId) { NameOf(cls, out, n); return; }
    uint64_t fn = *(uint64_t*)((unsigned char*)cls + 0x18);
    unsigned h = (unsigned)(((uintptr_t)cls >> 4) * 2654435761u) & 1023;
    ClsNameE* e = nullptr;
    for (int probe = 0; probe < 16; ++probe, h = (h + 1) & 1023)
    {
        ClsNameE& c = g_ClsName[h];
        if (c.cls == cls && c.fname == fn) { e = &c; break; }
        if (!c.cls || c.cls == cls) { char nm[128]; NameOf(cls, nm, sizeof(nm)); if (strlen(nm) >= sizeof(c.nm)) break; c.cls = cls; c.fname = fn; strcpy(c.nm, nm); e = &c; break; }
    }
    if (!e) { NameOf(cls, out, n); return; }   // table crowded here / a very long name
    size_t L = strlen(e->nm); if (L >= n) L = n - 1;
    memcpy(out, e->nm, L); out[L] = 0;
}
static void ObjClassName(void* obj, char* out, size_t n) { ClassNameOf(obj ? *(void**)((unsigned char*)obj + 0x10) : nullptr, out, n); }
static bool ClassIs(void* cls, const char* want) { char nm[128]; ClassNameOf(cls, nm, sizeof(nm)); return strcmp(nm, want) == 0; }




static bool Readable(const void* p, size_t n)
{
    if ((uintptr_t)p < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}
static long long Rva(uint64_t p) { return (p >= g_Base && p < g_Base + 0x6000000) ? (long long)(p - g_Base) : -1; }









