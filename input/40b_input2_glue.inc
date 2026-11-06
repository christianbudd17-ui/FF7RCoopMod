// ---------------------------------------------------------------------------------
// GLUE TO THE NEW INPUT MODULES (input2/: in2 = controllers, seats, map, output = leader controller to the game)
// ---------------------------------------------------------------------------------
// History: f1.1 watch only, f1.2 GameInput=XInput, f1.3 / f1.4 reads from the new module, f1.6 the seats module
// decides each seat's controller, f1.7 the new modules are the only reader (old VPad / DualSense reader removed).
// (step 2.3 of the switch-over): NO CONTROLLER NUMBERS. The old code's "controller codes" (0-3 XInput slot,
// 4-6 DirectInput controller; g_P1PadPhys, PLAYER2_INDEX, g_HideSlot, g_PadSlotOf, MapPad) are gone. Everything asks
// for a SEAT's controller: PadGetSeat(seat) / PadGetMine() (the loaded player's seat) = In2SeatRead here.
//  - seat 0 = the leader's (the game reads it as XInput slot 0), seat 1 = the player-2 state's, seat 2 = player-3's.
//  - f1.14: each seat gets its controller from [Input2] SeatN only (Keyboard / None / Auto / VID:PID / VID:PID#n /
//    path:..., blank = Auto) - the old [PlayerN] ControllerSlot / DirectInputController / Device keys are no longer read.
//    The seats module keeps each seat's physical controller by identity (unplug / re-plug, hot swap = seats traded).
//  - a DirectInput controller's buttons go through its model's [Pad.VID:PID] map (input2/map), else the default layout.
// [Input2] GameInput=XInput (default) keeps the game in XInput mode - see input2/output.h.
// FF7RCoopMod_Controllers2.txt next to the ini: the controller list by identity (for the settings app).
#include "../input2/in2.h"
#include "../input2/seats.h"
#include "../input2/output.h"
#include "../input2/map.h"

static bool g_In2On = false;          // the devices module is running
static bool g_In2XOnly = true;        // GameInput=XInput (default)
// the ORIGINAL GetDeviceState for one of in2's own DirectInput devices (never the mod's filter, which would treat it
// as one of the game's devices); nullptr = skip this read (the table is being patched right now)
static void* In2DiOrig(void* dev)
{
    const DiVt* v = DiVtOf(dev); if (v) return (void*)v->st;
    void* e = (*(void***)dev)[9];
    return e == (void*)&Hook_DiState ? nullptr : e;
}
// the game's own window for in2's DirectInput devices: g_GameHwnd once the game has been in front, before that the
// game's main window (class UnrealWindow) found directly (so the players' pads open at once, in the background too)
static HWND In2Window()
{
    if (g_GameHwnd) return g_GameHwnd;
    struct F { static BOOL CALLBACK cb(HWND h, LPARAM l) {
        DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId() || GetWindow(h, GW_OWNER)) return TRUE;
        wchar_t cls[64] = L""; GetClassNameW(h, cls, 64);
        if (wcscmp(cls, L"UnrealWindow")) return TRUE;
        *(HWND*)l = h; return FALSE; } };
    HWND h = nullptr; EnumWindows(F::cb, (LPARAM)&h);
    return h;
}

static void In2Start()   // from InstallCoopHooks, once, after the old hooks are in
{
    // [Input2] GameInput=XInput keeps the game in XInput mode (its DirectInput selection refused), so XInput
    // slot 0 - the leader seat's controller - is the only controller the game reads.
    wchar_t gi[32]; IniStr(L"Input2", L"GameInput", gi, 32);
    bool xonly = _wcsicmp(gi, L"Game") != 0;   // XInput is the default; GameInput=Game = the game decides
    g_In2XOnly = xonly;
    if (xonly) in2::output::Start(in2::output::CHANNEL_XINPUT, CoopLog, g_Base);
    else
    {
        in2::output::Start(in2::output::CHANNEL_NONE, CoopLog, g_Base);
        CoopLog("INPUT2: [Input2] GameInput=Game - the game picks XInput or DirectInput itself. NOTE: since f1.4 the leader's controller only reaches the game in XInput mode");
    }
    if (xonly && in2::output::Active() != in2::output::CHANNEL_XINPUT)   // the patch didn't go in (checks failed)
        CoopLog("INPUT2 OUTPUT: WARNING - GameInput=XInput could not be applied. If the game's controller setting is a DirectInput pad the leader's controller WON'T reach the game (the old pass-through was removed in f1.4) - report this log");
    {   // the old switches
        static const wchar_t* gone[4] = {L"Mode", L"Slot0", L"Pads", L"Seats"};
        for (const wchar_t* k : gone) { wchar_t v[32]; IniStr(L"Input2", k, v, 32); if (v[0]) CoopLog("INPUT2: [Input2] %ls=%ls isn't needed any more (since f1.7 the new controller modules are always on) - ignored", k, v); }
    }
    in2::Host h;
    h.log = CoopLog;
    h.xGetState = (in2::XGetState_t)Real_XInputGetState;   // the real System32 one, not this DLL's exports
    h.diOrigGetState = In2DiOrig;
    h.gameWindow = In2Window;
    in2::seats::SetLogger(CoopLog);
    if (!in2::Start(h))
    {
        CoopLog("INPUT2: WARNING - the controller module didn't start: only XInput controllers work (read directly), DirectInput / PlayStation controllers don't");
        return;
    }
    for (int s = 0; s < 3; ++s)
    {   // [Input2] SeatN only - blank = Auto (the first free gamepad, seats filled in order)
        wchar_t key[8]; swprintf(key, 8, L"Seat%d", s + 1);
        char a[220]; bool ok = true;
        in2::seats::Want w = SeatWant(s + 1, a, sizeof(a), &ok);   // (review #30): the settings' own reading (settings/03)
        if (!ok) CoopLog("INPUT2 SEATS: [Input2] %ls='%s' not understood - seat %d uses Auto", key, a, s + 1);
        if (s == 0 && w.kind == in2::seats::SEAT_NONE) w.kind = in2::seats::SEAT_KEYBOARD;   // player 1 always has the keyboard
        if (s == 2 && !g_P3Enabled) { memset(&w, 0, sizeof(w)); w.kind = in2::seats::SEAT_NONE; }   // player 3 off: seat 3 holds nothing
        in2::seats::SetWant(s, w);
        CoopLog("INPUT2 SEATS: seat %d picks '%s' ([Input2] %ls%s)%s", s + 1, a[0] ? a : "Auto", key, a[0] ? "" : " blank = Auto", s == 2 && !g_P3Enabled ? " - player 3 is off" : "");
    }
    in2::seats::Update();
    {   // old controller keys are ignored now - say so once (an old ini behaves differently: blank SeatN = Auto)
        static const wchar_t* secs[3] = {L"Player1", L"Player2", L"Player3"};
        static const wchar_t* keys[3] = {L"Device", L"ControllerSlot", L"DirectInputController"};
        for (int i = 0; i < 3; ++i) for (const wchar_t* k : keys)
        {
            wchar_t v[64]; IniStr(secs[i], k, v, 64);
            if (v[0]) CoopLog("INPUT2 SEATS: NOTE - [%ls] %ls=%ls is ignored since f1.14 - which controller a player uses is [Input2] Seat%d only (blank = Auto); the settings app writes it", secs[i], k, v, i + 1);
        }
    }
    g_In2On = true;
    CoopLog("INPUT2: every player's controller is read by the new controller module; which controller each seat has is decided by the seats module (by identity, from [Input2] SeatN)");
}

static void In2WriteList()   // FF7RCoopMod_Controllers2.txt (next to the ini / log), when the list changes
{
    static uint32_t lastSig = 0;
    uint32_t sig = 2166136261u; int n = in2::Count();
    for (int i = 0; i < n; ++i)
    {
        in2::Device d; if (!in2::Get(i, &d)) continue;
        uint32_t v[4] = {(uint32_t)d.id, (uint32_t)d.present, (uint32_t)(d.xinputSlot + 1), (uint32_t)d.apis};
        for (uint32_t x : v) sig = (sig ^ x) * 16777619u;
    }
    if (sig == lastSig) return;
    lastSig = sig;
    wchar_t p[MAX_PATH]; wcscpy(p, g_IniPath); wchar_t* sl = wcsrchr(p, L'\\'); if (sl) sl[1] = 0; else p[0] = 0;
    wcscat(p, L"FF7RCoopMod_Controllers2.txt");
    FILE* f = nullptr;
    if (_wfopen_s(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# Controllers found by the new input module (by identity). One line per controller:\n");
    fprintf(f, "# id | present | VID:PID | name | channels | layout | XInput slot | device path | DirectInput name | DirectInput instance name\n");
    for (int i = 0; i < n; ++i)
    {
        in2::Device d; if (!in2::Get(i, &d)) continue;
        char apis[24] = "";
        if (d.apis & in2::API_XINPUT) strcat(apis, "XInput ");
        if (d.apis & in2::API_HID) strcat(apis, "HID ");
        if (d.apis & in2::API_DINPUT) strcat(apis, "DirectInput ");
        fprintf(f, "%d | %s | %04X:%04X | %s | %s| %s | %d | %s | %s | %s\n", d.id, d.present ? "yes" : "no", d.vid, d.pid, d.name, apis,
            d.layout == in2::LAYOUT_XBOX ? "Xbox" : d.layout == in2::LAYOUT_PLAYSTATION ? "PlayStation" : "generic", d.xinputSlot, d.path, d.diName, d.diInstName);
    }
    fclose(f);
}

// TradeLead (seats/24) - seats a and b trade what they hold (the seats module's wants)
static ULONGLONG g_DgUntil = 0;   // DIAG window (In2Diag)
static void In2SeatsTraded(int a, int b)
{
    if (a < 0 || a > 2 || b < 0 || b > 2 || a == b) return;
    g_DgUntil = GetTickCount64() + 60000;   // DIAG
    in2::seats::Trade(a, b);
    if (g_In2On) in2::seats::Update();
}
// is something connected on XInput slot k? (asking an empty slot is slow: at most every 2 s)
// only for the fallback when the controller module couldn't start (then the leader = XInput slot 0, nobody else)
static bool In2SlotConnected(int k)
{
    if (k < 0 || k > 3) return false;
    static volatile DWORD lastT[4] = {0, 0, 0, 0}; static volatile LONG conn[4] = {0, 0, 0, 0};
    DWORD now = GetTickCount();
    if (!lastT[k] || now - lastT[k] > 2000)
    {
        lastT[k] = now ? now : 1;
        XINPUT_STATE t; conn[k] = Real_XInputGetState && Real_XInputGetState((DWORD)k, &t) == ERROR_SUCCESS;
    }
    return conn[k] != 0;
}
// seat s holds a controller (one that is here, or one it waits for) - not the keyboard / nobody
static bool In2SeatHasController(int s)
{
    if (s < 0 || s > 2) return false;
    if (!g_In2On) return s == 0 && g_Cfg.p1Controller;   // fallback: the leader on XInput slot 0
    in2::seats::SeatBrief st; if (!in2::seats::GetBrief(s, &st)) return false;
    return st.kind == in2::seats::SEAT_DEVICE || (st.waiting && !st.autoWaiting);   // Auto that never found a pad = no controller (player 1: the keyboard)
}
static bool In2Running() { return g_In2On; }
static bool In2SeatAutoWaiting(int s)
{
    if (s < 0 || s > 2 || !g_In2On) return false;
    in2::seats::SeatBrief st; return in2::seats::GetBrief(s, &st) && st.autoWaiting;
}
// the XInput slot of seat s's controller right now (rumble, battery ...), -1 = none / not an XInput controller
static int In2SeatXSlot(int s)
{
    if (s < 0 || s > 2) return -1;
    if (!g_In2On) return s == 0 && In2SlotConnected(0) ? 0 : -1;
    in2::seats::SeatBrief st; in2::Reading d;
    if (!in2::seats::GetBrief(s, &st) || st.kind != in2::seats::SEAT_DEVICE || !in2::ReadById(st.deviceId, &d) || !d.present) return -1;
    return d.xinputSlot;
}
// the USB ids of seat s's controller (for its button icons). false = no controller in that seat (or the module is off).
static bool In2SeatVidPid(int s, unsigned* vid, unsigned* pid)
{
    *vid = *pid = 0;
    if (s < 0 || s > 2 || !g_In2On) return false;
    in2::seats::SeatBrief st; in2::Reading d;
    if (!in2::seats::GetBrief(s, &st) || st.kind != in2::seats::SEAT_DEVICE || !in2::ReadById(st.deviceId, &d) || !d.present) return false;
    *vid = d.vid; *pid = d.pid;
    return true;
}
static void In2SeatText(int s, char* out, size_t n)   // for log lines
{
    in2::seats::Seat st;
    if (g_In2On && in2::seats::Get(s, &st)) snprintf(out, n, "%s", st.desc);
    else snprintf(out, n, "%s", s == 0 ? "XInput slot 0 (controller module not running)" : "nobody (controller module not running)");
}

// DIAG (temporary): every 2 s for 60 s after the start / a hot swap - what each seat's controller reads, what the
// game's XInput slot 0 returned, what the players' passes read
static void In2Diag()
{
    static bool started = false; static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (!started) { started = true; g_DgUntil = now + 60000; }
    if (now > g_DgUntil || now - last < 2000) return;
    last = now;
    char seat[3][120];
    for (int s = 0; s < 3; ++s)
    {
        in2::seats::Seat st; in2::Device d;
        if (!in2::seats::Get(s, &st)) { snprintf(seat[s], sizeof(seat[s]), "?"); continue; }
        if (st.kind != in2::seats::SEAT_DEVICE || !in2::FindById(st.deviceId, &d))
        { snprintf(seat[s], sizeof(seat[s]), "%s%s", st.kind == in2::seats::SEAT_KEYBOARD ? "keyboard" : "none", st.waiting ? " (waiting)" : ""); continue; }
        LONG age = d.stateMs ? (LONG)(GetTickCount() - d.stateMs) : -1;
        snprintf(seat[s], sizeof(seat[s]), "#%d %s age %ldms b=%04X lx=%d ly=%d", d.id, d.source == in2::API_XINPUT ? "XI" : d.source == in2::API_HID ? "HID" : "DI",
                 age, d.state.buttons, d.state.lx, d.state.ly);
    }
    bool c2 = g_Player->joined; bool c3 = false;
    if (g_P3Enabled) { WithP3 w; c3 = g_Player->joined; }
    CoopLog("DIAG SWAP: seat1 %s | seat2 %s | seat3 %s", seat[0], seat[1], seat[2]);
    CoopLog("DIAG SWAP: game slot 0: %ld calls, %ld leader reads (%ld with input, %ld failed), %ld idle (p1Controller %d, seat 1 has a controller %d), last input b=%04X lx=%d ly=%d | P2 reads %ld ok %ld stick %ld btn %ld (in %d) | P3 reads %ld ok %ld stick %ld btn %ld (in %d) | seats hold players %d %d %d",
            g_DgS0Calls, g_DgS0Lead, g_DgS0Input, g_DgS0Fail, g_DgS0Idle, (int)g_Cfg.p1Controller, (int)In2SeatHasController(0), g_DgS0Btn, g_DgS0LX, g_DgS0LY,
            g_DgPadCalls[0], g_DgPadOk[0], g_DgPadStick[0], g_DgPadBtn[0], (int)c2, g_DgPadCalls[1], g_DgPadOk[1], g_DgPadStick[1], g_DgPadBtn[1], (int)c3,
            g_SeatPerson[0] + 1, g_SeatPerson[1] + 1, g_SeatPerson[2] + 1);
    g_DgS0Calls = g_DgS0Lead = g_DgS0Input = g_DgS0Fail = g_DgS0Idle = 0;
    for (int i = 0; i < 2; ++i) g_DgPadCalls[i] = g_DgPadOk[i] = g_DgPadStick[i] = g_DgPadBtn[i] = 0;
}
static void In2Tick()   // game thread, every frame (from Hook_Tick)
{
    if (in2::output::Active() != in2::output::CHANNEL_XINPUT)
    {   // the DirectInput pass-through is gone - say so once if the game goes into DirectInput mode (GameInput=Game,
        // or the XInput patch couldn't be applied)
        static bool warned = false;
        if (!warned && in2::output::GameInDirectInputMode())
        {
            warned = true;
            CoopLog("INPUT2 OUTPUT: WARNING - the game switched to DirectInput mode (%s): the leader's controller can't reach it", g_In2XOnly ? "GameInput=XInput could not be applied" : "GameInput=Game - remove it from [Input2]");
        }
    }
    if (!g_In2On) return;
    In2Diag();   // DIAG
    DWORD now = GetTickCount();
    static DWORD last = 0;
    if (now - last < 250) return;
    last = now;
    in2::seats::Update();
    static DWORD lastList = 0;
    if (now - lastList >= 2000) { lastList = now; In2WriteList(); }
}

// ---- THE read of a player's controller: by seat (PadGetSeat / PadGetMine, the game's slot 0) ----
// [Pad.VID:PID] maps (input2/map.h), read once per controller model and kept; F6 (reload) reads them again
struct In2PadMap { uint16_t vid, pid; bool found; in2::ButtonMap m; };
static In2PadMap g_In2PadMaps[32]; static int g_In2PadMapN = 0;
static SRWLOCK g_In2PadMapLock = SRWLOCK_INIT;
// (finding #32) the standard layout of a controller without a [Pad.VID:PID] map, worked out once per controller (by its
// device id - never reused) instead of on every read; under the same lock, cleared with the maps
struct In2DefMap { int id; in2::ButtonMap m; };
static In2DefMap g_In2DefMaps[32]; static int g_In2DefMapN = 0;
static void In2PadMapsReload()   // from ReloadSettings
{
    AcquireSRWLockExclusive(&g_In2PadMapLock); g_In2PadMapN = 0; g_In2DefMapN = 0; ReleaseSRWLockExclusive(&g_In2PadMapLock);
}
static bool In2PadMapOf(uint16_t vid, uint16_t pid, in2::ButtonMap* m)
{
    AcquireSRWLockShared(&g_In2PadMapLock);
    for (int i = 0; i < g_In2PadMapN; ++i)
        if (g_In2PadMaps[i].vid == vid && g_In2PadMaps[i].pid == pid)
        {
            bool f = g_In2PadMaps[i].found; if (f) *m = g_In2PadMaps[i].m;
            ReleaseSRWLockShared(&g_In2PadMapLock); return f;
        }
    bool full = g_In2PadMapN >= 32;
    ReleaseSRWLockShared(&g_In2PadMapLock);
    if (full) return false;   // (32 models - never read the ini on every read)
    In2PadMap e; memset(&e, 0, sizeof(e)); e.vid = vid; e.pid = pid;
    e.found = in2::MapFromIni(g_IniPath, vid, pid, &e.m);
    AcquireSRWLockExclusive(&g_In2PadMapLock);
    bool have = false;
    for (int i = 0; i < g_In2PadMapN; ++i) if (g_In2PadMaps[i].vid == vid && g_In2PadMaps[i].pid == pid) have = true;   // another thread was quicker
    bool added = !have && g_In2PadMapN < 32;
    if (added) g_In2PadMaps[g_In2PadMapN++] = e;
    ReleaseSRWLockExclusive(&g_In2PadMapLock);
    if (added)
    {
        if (e.found)
            CoopLog("INPUT2 MAP: %04X:%04X uses its own button map [Pad.%04X:%04X] (A%d B%d X%d Y%d LB%d RB%d LT%d RT%d Back%d Start%d LS%d RS%d, sticks %d/%d %d/%d%s)",
                vid, pid, vid, pid, e.m.btn[0], e.m.btn[1], e.m.btn[2], e.m.btn[3], e.m.btn[4], e.m.btn[5], e.m.btn[6], e.m.btn[7], e.m.btn[8], e.m.btn[9],
                e.m.btn[10], e.m.btn[11], e.m.axis[0], e.m.axis[1], e.m.axis[2], e.m.axis[3], e.m.trigZ ? ", triggers on Z" : "");
        else CoopLog("INPUT2 MAP: %04X:%04X has no [Pad.%04X:%04X] button map - the standard layout (Xbox-type or PlayStation numbering)", vid, pid, vid, pid);
    }
    if (e.found) *m = e.m;
    return e.found;
}
static void In2MapFor(int id, const in2::Reading& r, in2::ButtonMap* m)
{
    if (In2PadMapOf(r.vid, r.pid, m)) return;   // the controller model's own map ([Pad.VID:PID]); else the defaults below
    AcquireSRWLockShared(&g_In2PadMapLock);
    for (int i = 0; i < g_In2DefMapN; ++i) if (g_In2DefMaps[i].id == id) { *m = g_In2DefMaps[i].m; ReleaseSRWLockShared(&g_In2PadMapLock); return; }
    ReleaseSRWLockShared(&g_In2PadMapLock);
    in2::Device d; if (!in2::FindById(id, &d)) d.name[0] = 0;   // its name (read once here, not on every read)
    char low[64]; int li = 0; for (; d.name[li] && li < 63; ++li) low[li] = (char)tolower((unsigned char)d.name[li]); low[li] = 0;
    bool xbox = r.xinputFamily || !strncmp(low, "controller (", 12) || strstr(low, "xbox") || strstr(low, "xinput") || strstr(low, "f310") || strstr(low, "f710");
    static const int bd[12] = {2, 3, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12}, ad[4] = {0, 1, 2, 5};
    static const int xb[12] = {1, 2, 3, 4, 5, 6, 0, 0, 7, 8, 9, 10}, xa[4] = {0, 1, 3, 4};
    for (int i = 0; i < 12; ++i) m->btn[i] = xbox ? xb[i] : bd[i];
    for (int i = 0; i < 4; ++i) m->axis[i] = xbox ? xa[i] : ad[i];
    m->trigZ = xbox;
    AcquireSRWLockExclusive(&g_In2PadMapLock);
    bool have = false; for (int i = 0; i < g_In2DefMapN; ++i) if (g_In2DefMaps[i].id == id) have = true;   // another thread was quicker
    if (!have && g_In2DefMapN < 32) { g_In2DefMaps[g_In2DefMapN].id = id; g_In2DefMaps[g_In2DefMapN].m = *m; ++g_In2DefMapN; }
    ReleaseSRWLockExclusive(&g_In2PadMapLock);
}
// Returns ERROR_SUCCESS with *st = seat s's controller state, or ERROR_DEVICE_NOT_CONNECTED (keyboard / nobody /
// its controller unplugged / not found yet).
static DWORD In2SeatRead(int s, XINPUT_STATE* st)
{
    if (!st) return ERROR_BAD_ARGUMENTS;
    memset(st, 0, sizeof(*st));
    if (s < 0 || s > 2) return ERROR_DEVICE_NOT_CONNECTED;
    static volatile LONG lastUse[3] = {-1, -1, -1}, logs = 0, nCtl = 0, nRaw = 0, nNone = 0, lastSum = 0;
    static volatile LONG pk[3] = {0, 0, 0}, lastHash[3] = {0, 0, 0};
    DWORD r = ERROR_DEVICE_NOT_CONNECTED; LONG use = 0;   // 1 = its controller, 2 = an XInput slot read directly, 3 = unplugged, 0 = nothing
    in2::Reading d; bool have = false; int devId = 0;   // (finding #32) the reading only - the names when a line is logged
    if (!g_In2On)
    {   // the controller module couldn't start - the leader reads XInput slot 0 directly, nobody else has a controller
        if (s == 0 && Real_XInputGetState && In2SlotConnected(0)) { r = Real_XInputGetState(0, st); use = r == ERROR_SUCCESS ? 2 : 0; }
    }
    else
    {
        in2::seats::SeatBrief ss;
        if (in2::seats::GetBrief(s, &ss))
        {
            if (ss.kind == in2::seats::SEAT_DEVICE && in2::ReadById(ss.deviceId, &d) && d.present) { have = true; devId = ss.deviceId; }
            else if (ss.waiting) use = 3;
        }
        if (have)
        {
            DWORD now = GetTickCount();
            LONG age = d.stateMs ? (LONG)(now - d.stateMs) : 0x7FFFFFFF;
            // (a DirectInput read can lag during a device scan: its last state for up to 1 s)
            bool fresh = age < 150 || (d.source != in2::API_XINPUT && age < 1000);
            if (fresh)
            {
                in2::XPad x;
                if (d.source == in2::API_DINPUT) { in2::ButtonMap m; In2MapFor(devId, d, &m); in2::ApplyMap(d.state, m, &x); }
                else in2::FromPad(d.state, &x);   // XInput / PlayStation HID: already in the common layout
                st->Gamepad.wButtons = x.buttons; st->Gamepad.bLeftTrigger = x.lt; st->Gamepad.bRightTrigger = x.rt;
                st->Gamepad.sThumbLX = x.lx; st->Gamepad.sThumbLY = x.ly; st->Gamepad.sThumbRX = x.rx; st->Gamepad.sThumbRY = x.ry;
                r = ERROR_SUCCESS; use = 1;
            }
            else if (d.xinputSlot >= 0 && Real_XInputGetState) { r = Real_XInputGetState((DWORD)d.xinputSlot, st); use = r == ERROR_SUCCESS ? 2 : 0; }
        }
    }
    if (r == ERROR_SUCCESS)
    {   // dwPacketNumber: one counter per seat, moved whenever what is returned changes
        const unsigned char* b = (const unsigned char*)&st->Gamepad; uint32_t h = 2166136261u;
        for (size_t i = 0; i < sizeof(st->Gamepad); ++i) h = (h ^ b[i]) * 16777619u;
        if (InterlockedExchange(&lastHash[s], (LONG)h) != (LONG)h) InterlockedIncrement(&pk[s]);
        st->dwPacketNumber = (DWORD)pk[s];
    }
    else memset(st, 0, sizeof(*st));
    InterlockedIncrement(use == 1 ? &nCtl : use == 2 ? &nRaw : &nNone);
    if (InterlockedExchange(&lastUse[s], use) != use && InterlockedIncrement(&logs) <= 80)
    {
        char ds[180] = "-"; in2::Device full; if (have && in2::FindById(devId, &full)) in2::Describe(full, ds, sizeof(ds));
        CoopLog("INPUT2 READ: seat %d -> %s", s + 1,
                use == 1 ? ds : use == 2 ? "XInput slot 0 read directly (controller module not running)" : use == 3 ? "not connected (its controller is unplugged)" : "no controller");
    }
    LONG nowL = (LONG)GetTickCount(), ls = lastSum;
    if (!ls) InterlockedCompareExchange(&lastSum, nowL, 0);
    else if (nowL - ls > 30000 && InterlockedCompareExchange(&lastSum, nowL, ls) == ls)
    {
        CoopLog("INPUT2 READ: last 30 s - %ld reads of a controller, %ld of an XInput slot directly, %ld no controller", nCtl, nRaw, nNone);
        nCtl = nRaw = nNone = 0;
    }
    return r;
}
static DWORD PadGetSeat(int seat, XINPUT_STATE* st) { return In2SeatRead(seat, st); }
static DWORD PadGetMine(XINPUT_STATE* st) { return In2SeatRead(g_Player->num == 3 ? 2 : 1, st); }   // the loaded player's (2 / 3) seat
