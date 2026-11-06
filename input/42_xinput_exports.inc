// ---------------------------------------------------------------------------------
// The exported functions. These are what the game actually calls, believing this DLL
// IS xinput1_3.dll. Game slot 0 = the leader seat's controller (any kind - input2 reads it); slots 1-3 = connected but
// idle, so only the leader's controller reaches the game (players 2 / 3 are fed by the mod's own passes).
// ---------------------------------------------------------------------------------
static constexpr SHORT XINPUT_STICK_MOVED = 8000;        // a stick past this counts as "moved" (diagnostics)
static constexpr WORD  XINPUT_CAPS_ALL_BUTTONS = 0xF3FF; // every button bit a pad can report (0x0400 / 0x0800 are unused)
static constexpr SHORT XINPUT_CAPS_STICK_RES = (SHORT)0xFFC0;   // "full stick resolution" as real pads report it
static constexpr DWORD XINPUT_USER_ANY = 0xFF;           // = XUSER_INDEX_ANY: "any controller"
// (finding #33) the tests every export repeated:
// the real xinput1_3 is loaded and has this function (the settings are read first - EnsureConfig)
template <class Fn> static bool RealReady(Fn& real) { EnsureConfig(); return LoadRealXInput() && real; }
// the game asks for its controller 0 and the leader plays on a controller (not the keyboard)
static bool LeaderPadAsked(DWORD userIndex) { return userIndex == 0 && g_Cfg.p1Controller && In2SeatHasController(0); }
// the XInput slot of the leader's controller when the game asks for it (asked), else -1 (or not an XInput controller)
static int LeaderXSlotIf(bool asked) { return asked && g_Cfg.p1Controller ? In2SeatXSlot(0) : -1; }
extern "C"
{
    // game slot 0 = the leader seat's controller (whatever it is: XInput, DirectInput, PlayStation - by identity);
    // game slots 1-3 = connected XInput controllers are reported connected but idle (only the leader's reaches the game).
    __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
    {
        if (!RealReady(Real_XInputGetState)) return ERROR_DEVICE_NOT_CONNECTED;
        if (!LooksLikeValidPointer(pState)) return ERROR_DEVICE_NOT_CONNECTED;

        InstallCoopHooks(); // v2.0 - runs once, on the game's own input thread
        // f1.337 (pre-release review): co-op switched itself off (another game build / a hook the mod needs failed) - the game
        // gets plain XInput, as without the mod (before: slots still reported connected-but-idle, so a DirectInput pad that the
        // game only picks when no XInput pad is there could stop working, despite the message "the game runs normally")
        if (g_CoopOff) return Real_XInputGetState(dwUserIndex, pState);

        // once per launch, record which of the 4 slots have something connected.
        static LONG slotReportDone = 0;
        if (InterlockedCompareExchange(&slotReportDone, 1, 0) == 0)   // into the main log
        {
            char line[96] = ""; int pos = 0;
            for (DWORD i = 0; i < 4; ++i)
            {
                XINPUT_STATE tmp{};
                DWORD r = Real_XInputGetState(i, &tmp);
                pos += snprintf(line + pos, sizeof(line) - pos, "%lu:%s ", i, r == ERROR_SUCCESS ? "connected" : "empty");
            }
            CoopLog("PAD: controller slots at launch - %s", line);
        }

        bool isP1 = LeaderPadAsked(dwUserIndex);
        if (dwUserIndex == 0) { InterlockedIncrement(&g_DgS0Calls); if (!isP1) InterlockedIncrement(&g_DgS0Idle); }   // DIAG
        if (!isP1)
        {   // any other slot (and slot 0 while the leader plays on the keyboard): connected but idle,
            // so the game keeps polling it and nothing but the leader's controller reaches it
            DWORD result = dwUserIndex < 4 ? Real_XInputGetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
            if (result != ERROR_SUCCESS) return result;
            memset(&pState->Gamepad, 0, sizeof(pState->Gamepad));
            return ERROR_SUCCESS;
        }
        DWORD result = PadGetSeat(0, pState);
        {   // DIAG
            InterlockedIncrement(&g_DgS0Lead);
            if (result != ERROR_SUCCESS) InterlockedIncrement(&g_DgS0Fail);
            else
            {
                const XINPUT_GAMEPAD& g = pState->Gamepad;
                if (g.wButtons || g.sThumbLX > XINPUT_STICK_MOVED || g.sThumbLX < -XINPUT_STICK_MOVED || g.sThumbLY > XINPUT_STICK_MOVED || g.sThumbLY < -XINPUT_STICK_MOVED)
                { InterlockedIncrement(&g_DgS0Input); g_DgS0Btn = g.wButtons; g_DgS0LX = g.sThumbLX; g_DgS0LY = g.sThumbLY; }
            }
        }
        if (result != ERROR_SUCCESS)   // not there (yet) - keep the game's slot 0 "connected" (idle) so it keeps polling it
        {
            memset(pState, 0, sizeof(*pState)); result = ERROR_SUCCESS;
            static int logs = 0; if (logs < 3) { ++logs; CoopLog("PAD: player 1's controller not ready - game slot 0 reported connected, idle"); }
        }
        if (g_AutoStepOn && !g_InCutscene && GetTickCount64() < g_AutoStepUntil)
        {   // f1.259 AUTOSTEP: the leader's stick points at the gate's jump spot (seats/24b / camera/38b), no buttons
            memset(&pState->Gamepad, 0, sizeof(pState->Gamepad));
            pState->Gamepad.sThumbLX = g_AutoStepLX; pState->Gamepad.sThumbLY = g_AutoStepLY;
        }
        return result;
    }

    __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
    {
        if (!RealReady(Real_XInputSetState)) return ERROR_DEVICE_NOT_CONNECTED;
        if (!LooksLikeValidPointer(pVibration)) return ERROR_DEVICE_NOT_CONNECTED;
        if (g_CoopOff) return Real_XInputSetState(dwUserIndex, pVibration);   // f1.337: co-op off - plain XInput
        if (!LeaderPadAsked(dwUserIndex))
        {   // only the leader's controller rumbles for the game; other slots: nothing (the mod rumbles players 2/3 itself)
            if (dwUserIndex == 0) GameRumbleStop("leader on keyboard / no controller");
            XINPUT_STATE t{}; return dwUserIndex < 4 && Real_XInputGetState(dwUserIndex, &t) == ERROR_SUCCESS ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
        }
        int xs = In2SeatXSlot(0);
        if (xs < 0) { GameRumbleStop("leader on a DirectInput controller"); return ERROR_SUCCESS; }   // a DirectInput controller leading (or not here) - no rumble through XInput
        {   // the old pad stops (one atomic swap - no window where a stop and a new slot cross)
            LONG prev = InterlockedExchange(&g_GameRumSlot, xs);
            if (prev >= 0 && prev != xs) { XINPUT_VIBRATION z{0, 0}; Real_XInputSetState((DWORD)prev, &z); }
        }
        return Real_XInputSetState((DWORD)xs, pVibration);
    }

    __declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities)
    {
        if (!RealReady(Real_XInputGetCapabilities)) return ERROR_DEVICE_NOT_CONNECTED;
        // (slots 1-3 are reported connected but idle - see XInputGetState)
        if (!LooksLikeValidPointer(pCapabilities)) return ERROR_DEVICE_NOT_CONNECTED;
        if (g_CoopOff) return Real_XInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities);   // f1.337: co-op off - plain XInput
        if (LeaderPadAsked(dwUserIndex))
        {   // the leader seat's controller: an XInput one as it is, anything else (or not here) as a plain gamepad
            int xs = In2SeatXSlot(0);
            if (xs >= 0 && Real_XInputGetCapabilities((DWORD)xs, dwFlags, pCapabilities) == ERROR_SUCCESS) return ERROR_SUCCESS;
            memset(pCapabilities, 0, sizeof(*pCapabilities));
            pCapabilities->Type = 1; pCapabilities->SubType = 1;   // gamepad
            pCapabilities->Gamepad.wButtons = XINPUT_CAPS_ALL_BUTTONS;
            pCapabilities->Gamepad.bLeftTrigger = pCapabilities->Gamepad.bRightTrigger = 0xFF;   // full trigger range
            pCapabilities->Gamepad.sThumbLX = pCapabilities->Gamepad.sThumbLY = XINPUT_CAPS_STICK_RES;
            pCapabilities->Gamepad.sThumbRX = pCapabilities->Gamepad.sThumbRY = XINPUT_CAPS_STICK_RES;
            return ERROR_SUCCESS;
        }
        return Real_XInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities);
    }

    __declspec(dllexport) void WINAPI XInputEnable(BOOL enable)
    {
        if (!LoadRealXInput() || !Real_XInputEnable) return;
        Real_XInputEnable(enable);
    }

    __declspec(dllexport) DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid)
    {
        if (!RealReady(Real_XInputGetDSoundAudioDeviceGuids)) return ERROR_DEVICE_NOT_CONNECTED;
        if (!LooksLikeValidPointer(pDSoundRenderGuid) || !LooksLikeValidPointer(pDSoundCaptureGuid)) return ERROR_DEVICE_NOT_CONNECTED;
        if (g_CoopOff) return Real_XInputGetDSoundAudioDeviceGuids(dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid);   // f1.337: co-op off
        int xs = LeaderXSlotIf(dwUserIndex == 0);   // the leader's XInput controller only
        if (xs < 0) return ERROR_DEVICE_NOT_CONNECTED;
        return Real_XInputGetDSoundAudioDeviceGuids((DWORD)xs, pDSoundRenderGuid, pDSoundCaptureGuid);
    }

    __declspec(dllexport) DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation)
    {
        if (!RealReady(Real_XInputGetBatteryInformation)) return ERROR_DEVICE_NOT_CONNECTED;
        if (!LooksLikeValidPointer(pBatteryInformation)) return ERROR_DEVICE_NOT_CONNECTED;
        if (g_CoopOff) return Real_XInputGetBatteryInformation(dwUserIndex, devType, pBatteryInformation);   // f1.337: co-op off
        int xs = LeaderXSlotIf(dwUserIndex == 0);   // the leader's XInput controller only
        if (xs < 0) return ERROR_DEVICE_NOT_CONNECTED;
        DWORD phys = (DWORD)xs;

        CachedBatteryInfo& cache = g_BatteryCache[phys][devType == 1 ? 1 : 0];   // (code review B6)
        ULONGLONG now = GetTickCount64();

        if (!cache.hasResult || (now - cache.lastQueryTickMs) >= BATTERY_CACHE_MS)
        {
            cache.lastResult = Real_XInputGetBatteryInformation(phys, devType, &cache.lastInfo);
            cache.lastQueryTickMs = now;
            cache.hasResult = true;
        }

        *pBatteryInformation = cache.lastInfo;
        return cache.lastResult;
    }

    __declspec(dllexport) DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke)
    {
        if (!RealReady(Real_XInputGetKeystroke)) return ERROR_DEVICE_NOT_CONNECTED;
        if (!LooksLikeValidPointer(pKeystroke)) return ERROR_DEVICE_NOT_CONNECTED;
        if (g_CoopOff) return Real_XInputGetKeystroke(dwUserIndex, dwReserved, pKeystroke);   // f1.337: co-op off - plain XInput
        // keystrokes only from the leader's XInput controller (slot 0 or "any"); everything else has none
        int xs = LeaderXSlotIf(dwUserIndex == 0 || dwUserIndex == XINPUT_USER_ANY);
        if (xs < 0) return ERROR_EMPTY;
        DWORD r = Real_XInputGetKeystroke((DWORD)xs, dwReserved, pKeystroke);
        if (r == ERROR_SUCCESS) pKeystroke->UserIndex = 0;   // (it is the game's controller 0)
        return r;
    }
}


