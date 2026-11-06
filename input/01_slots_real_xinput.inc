// f1.259 (seats/24b + camera/38b AUTOSTEP): the mod steers the leader's left stick for a moment (the Failed Experiment gate
// hand-off - at most 2 s, until the jump cutscene). Read by XInputGetState (input/42) for game slot 0.
static volatile bool g_AutoStepOn = false;
static volatile ULONGLONG g_AutoStepUntil = 0;
static volatile SHORT g_AutoStepLX = 0, g_AutoStepLY = 0;
// ---------------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------------
// the players' controllers are no longer numbered (the v4-v19 "controller slots" PLAYER2_INDEX / g_P1PadPhys /
// g_HideSlot / g_PadSlotOf and MapPad are gone). Each SEAT has its controller (input/40b, input2/seats): seat 0 = the
// leader's (the game reads it as XInput slot 0), seat 1 = the player-2 state's, seat 2 = the player-3 state's.
static DWORD PadGetSeat(int seat, XINPUT_STATE* st);   // input/40b_input2_glue.inc
static DWORD PadGetMine(XINPUT_STATE* st);              // the loaded player's (2 / 3) seat
static bool  In2SeatAutoWaiting(int seat);              // on Auto and no pad found yet
static bool  In2Running();                              // the controller module runs (seats by identity)
static bool  In2SeatHasController(int seat);            // a controller (here or waited for) - not the keyboard / nobody
static int   In2SeatXSlot(int seat);                    // its controller's XInput slot, -1 = none / not an XInput controller
static void  In2SeatText(int seat, char* out, size_t n);
static void  In2SeatsTraded(int a, int b);
// DIAG (temporary - removal checklist C): what the game's XInput slot 0 and the players' passes get, logged every 2 s
// for 60 s after the start and after each hot swap (input/40b In2Tick)
static volatile LONG g_DgS0Calls = 0, g_DgS0Lead = 0, g_DgS0Input = 0, g_DgS0Idle = 0, g_DgS0Fail = 0;
static volatile SHORT g_DgS0LX = 0, g_DgS0LY = 0; static volatile WORD g_DgS0Btn = 0;
static volatile LONG g_DgPadCalls[2] = {0, 0}, g_DgPadOk[2] = {0, 0}, g_DgPadStick[2] = {0, 0}, g_DgPadBtn[2] = {0, 0};

// ---------------------------------------------------------------------------------
// Loading the real xinput1_3.dll from System32 (NOT the one next to the game, which
// is now us - loading by full path avoids us accidentally loading ourselves again).
// ---------------------------------------------------------------------------------
static HMODULE g_RealDll = nullptr;

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION*);
typedef DWORD(WINAPI* XInputGetCapabilities_t)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef void(WINAPI* XInputEnable_t)(BOOL);
typedef DWORD(WINAPI* XInputGetDSoundAudioDeviceGuids_t)(DWORD, GUID*, GUID*);
typedef DWORD(WINAPI* XInputGetBatteryInformation_t)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD(WINAPI* XInputGetKeystroke_t)(DWORD, DWORD, PXINPUT_KEYSTROKE);

static XInputGetState_t Real_XInputGetState = nullptr;
static XInputSetState_t Real_XInputSetState = nullptr;
// (code review B2): the XInput slot the game's rumble (slot 0 = the leader) was last forwarded to. When the leader's
// controller changes (hot swap, keyboard lead) the old pad got no "stop" and kept vibrating until that player was hit again.
static volatile LONG g_GameRumSlot = -1;
static void GameRumbleStop(const char* why)
{
    LONG s = InterlockedExchange(&g_GameRumSlot, -1);
    if (s >= 0 && Real_XInputSetState) { XINPUT_VIBRATION z{0, 0}; Real_XInputSetState((DWORD)s, &z); }
    (void)why;
}
static XInputGetCapabilities_t Real_XInputGetCapabilities = nullptr;
static XInputEnable_t Real_XInputEnable = nullptr;
static XInputGetDSoundAudioDeviceGuids_t Real_XInputGetDSoundAudioDeviceGuids = nullptr;
static XInputGetBatteryInformation_t Real_XInputGetBatteryInformation = nullptr;
static XInputGetKeystroke_t Real_XInputGetKeystroke = nullptr;


static bool LoadRealXInput()
{
    if (g_RealDll) return Real_XInputGetState != nullptr;

    // Forwarding to the REAL xinput1_3.dll in System32 (not the copy sitting next to the
    // game's .exe, which is now us). We tried xinput1_4.dll instead for a while, but its
    // internal implementation pulls in modern device-access/permission APIs that caused
    // heap corruption when called from inside an injected proxy DLL like this one. The
    // old xinput1_3.dll is exactly what the FFVIIHook mod already uses successfully on
    // this exact game with this exact proxy-DLL technique, so it's the safer target -
    // our earlier crashes with it were caused by garbage pointers from the game itself,
    // which we now guard against separately (see LooksLikeValidPointer below).
    wchar_t sysPath[MAX_PATH]{};
    GetSystemDirectoryW(sysPath, MAX_PATH);
    wcscat_s(sysPath, L"\\xinput1_3.dll");

    g_RealDll = LoadLibraryW(sysPath);
    if (!g_RealDll) return false;

    Real_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(g_RealDll, "XInputGetState"));
    Real_XInputSetState = reinterpret_cast<XInputSetState_t>(GetProcAddress(g_RealDll, "XInputSetState"));
    Real_XInputGetCapabilities = reinterpret_cast<XInputGetCapabilities_t>(GetProcAddress(g_RealDll, "XInputGetCapabilities"));
    Real_XInputEnable = reinterpret_cast<XInputEnable_t>(GetProcAddress(g_RealDll, "XInputEnable"));
    Real_XInputGetDSoundAudioDeviceGuids = reinterpret_cast<XInputGetDSoundAudioDeviceGuids_t>(GetProcAddress(g_RealDll, "XInputGetDSoundAudioDeviceGuids"));
    Real_XInputGetBatteryInformation = reinterpret_cast<XInputGetBatteryInformation_t>(GetProcAddress(g_RealDll, "XInputGetBatteryInformation"));
    Real_XInputGetKeystroke = reinterpret_cast<XInputGetKeystroke_t>(GetProcAddress(g_RealDll, "XInputGetKeystroke"));

    return Real_XInputGetState != nullptr;
}

// ---------------------------------------------------------------------------------
// Sanity-check a pointer the game handed us before we forward it into the real DLL.
// The lowest 64KB of address space (0x0 - 0xFFFF) is always reserved/unmapped on
// Windows and can never be a legitimate pointer - a null pointer or a tiny garbage
// value (like the uninitialized one we caught in the debug log - 0x755) will always
// fall in that range. Real stack/heap addresses are always much higher. This won't
// catch every possible bad pointer, but it catches exactly the pattern we found.
// ---------------------------------------------------------------------------------
static bool LooksLikeValidPointer(const void* p)
{
    return reinterpret_cast<uintptr_t>(p) >= 0x10000;
}

// ---------------------------------------------------------------------------------
// XInputGetBatteryInformation is a genuinely expensive call compared to the others -
// unlike XInputGetState (which just reads a cached value), checking battery level
// means actually reaching out to the controller's hardware driver. The debug log
// showed the game calling this for all 4 controller slots EVERY SINGLE FRAME, which
// is a huge amount of real hardware-driver traffic that this old (2007) DLL was
// almost certainly never tested against. We cache each index's last real result and
// only actually re-query the hardware a few times a second - battery level never
// changes fast enough for the game to notice or care about the difference.
// ---------------------------------------------------------------------------------
struct CachedBatteryInfo
{
    bool hasResult = false;
    ULONGLONG lastQueryTickMs = 0;
    DWORD lastResult = ERROR_DEVICE_NOT_CONNECTED;
    XINPUT_BATTERY_INFORMATION lastInfo{};
};
static CachedBatteryInfo g_BatteryCache[4][2];   // (B6): per slot AND device type (0 gamepad, 1 headset)
static const ULONGLONG BATTERY_CACHE_MS = 1000; // re-check actual hardware at most once a second per index


