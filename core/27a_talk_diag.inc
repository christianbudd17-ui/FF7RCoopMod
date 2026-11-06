// =================================================================================
// Talking scenes (f1.120; came from the f1.111 TALKDIAG research). The author (f1.101 run): "there was a quasi cut scene. No
// camera changed" - OnBeginCutScene didn't fire. f1.119 run: of the candidates watched (EndFieldAPI.IsEnableFieldControler,
// EndCutAPI.IsPlaying / IsPlaying_TypeSpecify EV / LV / MV / fv, PlayerController.IsMoveInputIgnored) only the field
// controls moved with every talking scene (0 while talking, 1 again after; always 1 in battle).
// the research candidates and the TALKDIAG lines are gone (removal checklist); this is the signal that stays:
// EndFieldAPI.IsEnableFieldControler(bEncount = false) polled 5x a second - field controls off outside a battle = a
// talking scene -> g_TalkScene (camera/38 treats it like a scripted shot). Read-only (a static BlueprintCallable function
// through ProcessEvent). [Camera] TalkScenes=0 turns it off.
// =================================================================================
static void* FindObj(const char* name, const char* clsName);   // pip_hud/30
static void TalkDiagTick(void* pc)
{
    static ULONGLONG last = 0; ULONGLONG now = GetTickCount64();
    if (now - last < 200 || !pc) return;
    last = now;
    static int st = 0;
    static int fIdx = -1, fSer = 0;
    static void* fnCtl = nullptr;
    static int oCtlArg = -1, oCtlRet = -1, oW = -1;
    void* fapi = ResolveWeak(fIdx, fSer);
    if (st == 0 || (st == 1 && !fapi))
    {
        static ULONGLONG lastFind = 0;
        if (st == 1 && now - lastFind < 10000) return;   // (a level change dropped it - look again at most every 10 s)
        lastFind = now; st = -1;
        fapi = FindObj("Default__EndFieldAPI", "EndFieldAPI");
        if (fapi) MakeWeak(fapi, &fIdx, &fSer);
        if (fapi && !fnCtl) { fnCtl = FindFunc(fapi, "IsEnableFieldControler"); if (fnCtl) { oCtlArg = ParamOff(fnCtl, "bEncount"); oCtlRet = ParamOff(fnCtl, "ReturnValue"); } }
        bool ok = fapi && fnCtl && oCtlArg >= 0 && oCtlRet >= 0 && ParmsSize(fnCtl) <= 24;   // review: the buffer is 32 (a world context = +8)
        oW = fnCtl ? ParamOff(fnCtl, "WorldContextObject") : -1;
        CoopLog("TALKSCENE: talking-scene signal (field controls) %s", ok ? "found" : "NOT found - talking scenes keep the co-op camera / split screen");
        st = ok ? 1 : -1;
    }
    if (st != 1 || !fapi) { g_TalkScene = false; return; }   // no signal = no talking-scene step-aside
    int ctl;
    {
        alignas(16) unsigned char b[32] = {};
        if (oW >= 0 && oW <= 24) memcpy(b + oW, &pc, 8);   // a world context: the player controller
        b[oCtlArg] = 0;   // bEncount = false
        Orig_ProcessEvent(fapi, fnCtl, b);
        ctl = b[oCtlRet] != 0;
    }
    {   // f1.235 (core/06): outside a battle, the field controls on and the game's camera back on player 1 (no scripted
        // shot: the gameplay camera, near player 1, not narrowed - camera/38) for 2 s (10 polls) = normal play, so a
        // cutscene the game never ended is over
        static int onPolls = 0;
        onPolls = (ctl != 0 && !InBattleNow() && !g_ScriptedShot && g_InCutscene) ? onPolls + 1 : 0;
        if (onPolls >= 10 && CutsceneSafetyReady()) { onPolls = 0; CutsceneClear("field controls on and the game's camera back on player 1 for 2 s"); }
    }
    bool talk = g_Cfg.camTalkScenes && ctl == 0 && !InBattleNow();
    if (talk != g_TalkScene)
    {
        g_TalkScene = talk;
        static int tlogs = 0;
        if (tlogs < 200) { ++tlogs; CoopLog("TALKSCENE: %s", talk ? "the game took the field controls (talking scene / scripted moment) - game camera, split screen hidden"
                                                                  : "field controls back - co-op camera / split screen back (if they were on)"); }
    }
}
