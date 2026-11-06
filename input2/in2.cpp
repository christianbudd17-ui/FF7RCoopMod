// input2/in2.cpp - the controller module: the device list, the background thread and the interface (in2.h).
// (review #19): the channels are in their own files - in2_hid.cpp (PlayStation / HID), in2_dinput.cpp (DirectInput),
// in2_xinput.cpp (XInput slots); what they share is in in2_internal.h.
// Its own translation unit: nothing here reads or writes the old code's variables.
#include "in2_internal.h"

namespace in2 {
namespace detail {

// ---------------------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------------------
Dev   g_d[MAXD];
Device g_pub[MAXD];
int   g_n = 0, g_pubN = 0, g_nextId = 1;
CRITICAL_SECTION g_lock;
Host  g_h;
volatile LONG g_running = 0;
IDirectInput8W* g_di = nullptr;
bool  g_xConn[4] = {false, false, false, false};
XState g_x[4];
uint16_t g_xPrev[4] = {0, 0, 0, 0}, g_xEdgeBtn[4] = {0, 0, 0, 0};
DWORD g_xEdgeT[4] = {0, 0, 0, 0};
int   g_xBoundTo[4] = {-1, -1, -1, -1};   // device index tied to the slot
int   g_pressLogsTotal = 0;
uint32_t g_rej[256]; int g_rejN = 0;   // HID interfaces already found not to be controllers (path hashes)
DWORD g_scanNo = 0;
bool  g_diWaitWindow = false;   // DirectInput controllers wait for the game window (scan again soon)

// ---------------------------------------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------------------------------------
void Lower(char* s) { for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

void WideToUtf8(const wchar_t* w, char* out, size_t n)
{
    if (!n) return;
    out[0] = 0;
    if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n - 1, nullptr, nullptr);
    out[n - 1] = 0;
}

int HexVal(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

// "vid_054c&pid_0ce6" (USB) or "vid&0002054c_pid&0ce6" (Bluetooth): the last 4 hex digits of each
bool VidPidFromPath(const char* p, uint16_t* vid, uint16_t* pid)
{
    auto grab = [&](const char* key, uint16_t* v) -> bool
    {
        const char* s = strstr(p, key); if (!s) return false;
        s += 4; unsigned val = 0; int n = 0;
        while (n < 8 && HexVal(s[n]) >= 0) { val = (val << 4) | (unsigned)HexVal(s[n]); ++n; }
        if (n < 4) return false;
        *v = (uint16_t)(val & 0xFFFF); return true;
    };
    uint16_t a = 0, b = 0;
    bool ok = (grab("vid_", &a) || grab("vid&", &a)) && (grab("pid_", &b) || grab("pid&", &b));
    if (ok) { *vid = a; *pid = b; }
    return ok;
}

int SonyKind(uint16_t vid, uint16_t pid)
{
    if (vid != 0x054C) return 0;
    if (pid == 0x0CE6 || pid == 0x0DF2) return 2;                     // DualSense / DualSense Edge
    if (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0) return 1;   // DualShock 4 v1 / v2 / wireless adapter
    return 0;
}

const char* ButtonName(uint16_t bit)
{
    switch (bit)
    {
    case 0x0001: return "D-pad up";   case 0x0002: return "D-pad down"; case 0x0004: return "D-pad left"; case 0x0008: return "D-pad right";
    case 0x0010: return "Start";      case 0x0020: return "Back";       case 0x0040: return "LS";         case 0x0080: return "RS";
    case 0x0100: return "LB";         case 0x0200: return "RB";         case 0x1000: return "A";          case 0x2000: return "B";
    case 0x4000: return "X";          case 0x8000: return "Y";
    }
    return "?";
}

uint16_t PovToDpad(DWORD pov)
{
    if ((pov & 0xFFFF) == 0xFFFF) return 0;
    int d = (int)((pov + 2250) % 36000) / 4500;   // 0 up, 1 up-right, 2 right ... 7 up-left
    static const uint16_t m[8] = {0x1, 0x9, 0x8, 0xA, 0x2, 0x6, 0x4, 0x5};
    return m[d & 7];
}

int16_t ClampS(long v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

int FindByPathIdx(const char* path)
{
    for (int i = 0; i < g_n; ++i) if (!strcmp(g_d[i].pub.path, path)) return i;
    return -1;
}

int NewDev(const char* path)
{
    int slot = g_n;
    if (g_n >= MAXD)
    {   // full: reuse an entry that is gone and has nothing open (its reader thread has finished)
        slot = -1;
        for (int i = 0; i < g_n && slot < 0; ++i)
            if (!g_d[i].pub.present && !g_d[i].di && !g_d[i].readerOn && g_d[i].pub.xinputSlot < 0) slot = i;
        static bool full = false;
        if (slot < 0) { if (!full) { full = true; LOG("INPUT2: more than %d controllers - the rest are ignored", MAXD); } return -1; }
    }
    Dev& d = g_d[slot];
    memset(&d, 0, sizeof(d));
    d.pub.id = g_nextId++;
    d.pub.xinputSlot = -1;
    d.pub.layout = LAYOUT_GENERIC;
    d.dgLastCnt = -1;
    strncpy(d.pub.path, path, sizeof(d.pub.path) - 1);
    d.pub.xinputFamily = strstr(path, "ig_") != nullptr;
    d.matchSlot = -1;
    if (slot == g_n) ++g_n;
    return slot;
}

void SetIdentity(Dev& d, uint16_t vid, uint16_t pid, const char* name)
{
    if (vid || pid) { d.pub.vid = vid; d.pub.pid = pid; }
    else if (!d.pub.vid && !d.pub.pid) VidPidFromPath(d.pub.path, &d.pub.vid, &d.pub.pid);
    if (name && name[0] && (!d.pub.name[0] || !strcmp(d.pub.name, "controller"))) { memset(d.pub.name, 0, sizeof(d.pub.name)); strncpy(d.pub.name, name, sizeof(d.pub.name) - 1); }   // a real name replaces the placeholder
    if (!d.pub.name[0]) strcpy(d.pub.name, "controller");
    d.sony = SonyKind(d.pub.vid, d.pub.pid);
    d.pub.layout = d.pub.xinputFamily ? LAYOUT_XBOX : d.sony ? LAYOUT_PLAYSTATION : LAYOUT_GENERIC;
    bool was = d.pub.gamepad;
    d.pub.gamepad = LooksLikeGamepad(d.pub.xinputFamily, d.sony != 0, d.hidUsage, d.diType, d.diButtons, d.diAxes);
    if (d.pub.present && was != d.pub.gamepad)
        LOG("INPUT2: #%d %s - %s (DirectInput type 0x%02X, %d buttons, %d axes; HID usage %d)", d.pub.id, d.pub.name,
            d.pub.gamepad ? "a gamepad" : "not a gamepad - Auto never picks it", d.diType, d.diButtons, d.diAxes, d.hidUsage);
}

void DescribeTo(const Device& d, char* out, size_t n)
{
    char apis[24] = "";
    if (d.apis & API_XINPUT) strcat(apis, "XInput+");
    if (d.apis & API_HID)    strcat(apis, "HID+");
    if (d.apis & API_DINPUT) strcat(apis, "DI+");
    size_t l = strlen(apis); if (l) apis[l - 1] = 0; else strcpy(apis, "gone");
    const char* lay = d.layout == LAYOUT_XBOX ? "Xbox" : d.layout == LAYOUT_PLAYSTATION ? "PlayStation" : "generic";
    char slot[24] = ""; if (d.xinputSlot >= 0) snprintf(slot, sizeof(slot), ", XInput slot %d", d.xinputSlot);
    snprintf(out, n, "#%d %s %04X:%04X (%s, %s%s%s)", d.id, d.name, d.vid, d.pid, apis, lay, slot, d.gamepad ? "" : ", not a gamepad");
}

// ---------------------------------------------------------------------------------------------------------
// the background thread
// ---------------------------------------------------------------------------------------------------------
void LogPresses(Dev& d, const char* via)
{
    uint16_t down = (uint16_t)(d.pub.state.buttons & ~d.prevBtn);
    uint32_t rdown = d.pub.state.raw & ~d.prevRaw;
    d.prevBtn = d.pub.state.buttons; d.prevRaw = d.pub.state.raw;
    if (down || rdown) { d.edgeBtn = down; d.edgeT = down ? GetTickCount() : d.edgeT; }
    if ((!down && !rdown) || d.pressLogs >= 24 || g_pressLogsTotal >= 300) return;
    char what[160] = ""; size_t p = 0;
    for (int b = 0; b < 16; ++b) if (down & (1u << b)) p += snprintf(what + p, sizeof(what) - p, "%s%s", p ? "+" : "", ButtonName((uint16_t)(1u << b)));
    for (int b = 0; b < 32 && p < sizeof(what) - 16; ++b) if (rdown & (1u << b)) p += snprintf(what + p, sizeof(what) - p, "%sbutton %d", p ? "+" : "", b + 1);
    ++d.pressLogs; ++g_pressLogsTotal;
    LOG("INPUT2: #%d %s pressed %s (via %s)", d.pub.id, d.pub.name, what, via);
}

void PollDevices()
{
    DWORD now = GetTickCount();
    for (int i = 0; i < g_n; ++i)
    {
        Dev& d = g_d[i];
        if (!d.pub.present) continue;
        Pad st; memset(&st, 0, sizeof(st)); uint8_t src = 0;
        // DirectInput is read for every controller that has it (XInput-family ones too: their presses tie them to a slot)
        Pad dis; bool diOk = false;
        Pad hs; DWORD hms;
        bool hfull = false;
        EnterCriticalSection(&g_lock); hs = d.hidState; hms = d.hidMs; hfull = d.hidFull; LeaveCriticalSection(&g_lock);
        // (code review B4): only a pad in Bluetooth SIMPLE mode (reports only on change) keeps its last report while its
        // reader runs. Full reports come continuously (hundreds a second): if they stop (a Bluetooth pad dying with the stick
        // held) the last one goes stale like any HID read - the glue drops it after 1 s - instead of running forever.
        bool hidKeep = d.sony && d.readerOn && !hfull;
        // a PlayStation pad whose own HID reader runs isn't also polled through DirectInput (on Bluetooth that can
        // be a slow round trip every 4 ms - and its DirectInput state is never used while the reader runs)
        bool skipDi = d.sony && d.readerOn && hms && now - hms < 1000;
        if (skipDi) InterlockedIncrement(&d.dgDiSkipped);
        if (d.di && !skipDi)
        {
            DIJOYSTATE2 js; memset(&js, 0, sizeof(js));
            LARGE_INTEGER q0, q1, qf; QueryPerformanceCounter(&q0);
            d.di->Poll();
            HRESULT hr = DiRead(d.di, &js);
            if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) { d.di->Acquire(); d.di->Poll(); hr = DiRead(d.di, &js); }
            QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&qf);
            { LONG us = (LONG)((q1.QuadPart - q0.QuadPart) * 1000000 / qf.QuadPart); if (us > d.dgDiMaxUs) d.dgDiMaxUs = us; }   // DIAG
            if (hr == E_PENDING) {}   // skipped this time (see DiRead)
            else if (SUCCEEDED(hr)) { DecodeDi(js, d.pub.layout, dis); diOk = true; d.diFails = 0; }
            else if (hr == (HRESULT)DIERR_UNPLUGGED) DiClose(d);   // unplugged - just closed; opened again as soon as a scan finds it
            else if (++d.diFails > 250)
            {
                char ds[160]; DescribeTo(d.pub, ds, sizeof(ds));
                if (d.diReopens++ < 3) LOG("INPUT2: %s stopped answering through DirectInput - trying again in 30 s", ds);
                d.diSkipGuid = d.diInst; d.diSkipUntil = now + 30000;
                DiClose(d);
            }
        }
        int s = d.pub.xinputSlot;
        if (s >= 0 && g_xConn[s])
        {
            const XGamepad& g = g_x[s].g;
            st.buttons = g.b; st.lt = g.lt; st.rt = g.rt; st.lx = g.lx; st.ly = g.ly; st.rx = g.rx; st.ry = g.ry; src = API_XINPUT;
        }
        else if (hms && (now - hms < HID_FRESH_MS || hidKeep))   // a PlayStation pad whose reader runs keeps (simple mode only)
        { st = hs; src = API_HID; }                                            // its last report (Bluetooth may send only on change)
        else if (diOk) { st = dis; src = API_DINPUT; }
        if (!src) continue;   // (an XInput-family pad not tied to its slot yet reads through DirectInput: its press
                              // edges, recorded in LogPresses, are what MatchXInput compares with the slots)
        d.pub.state = st; d.pub.source = src; d.pub.stateMs = (src == API_HID && !hidKeep) ? hms : now;   // HID: when the report came (a running PlayStation reader in simple mode = current)
        LogPresses(d, src == API_XINPUT ? "XInput" : src == API_HID ? "HID" : "DirectInput");
    }
}

void UpdatePresence()
{
    for (int i = 0; i < g_n; ++i)
    {
        Dev& d = g_d[i];
        if (d.di && d.diSeenScan != g_scanNo) DiClose(d);
        if (d.hidSeen && d.hidSeenScan != g_scanNo) { d.hidSeen = false; d.pub.apis &= (uint8_t)~API_HID; }
        bool present = (d.pub.apis & (API_DINPUT | API_HID)) != 0;
        if (present != d.pub.present)
        {
            d.pub.present = present;
            if (!present) { for (int s = 0; s < 4; ++s) if (g_xBoundTo[s] == i) Unbind(s, "controller gone"); }
            d.diSkipUntil = 0;   // unplugged / plugged in again - no "try again in 30 s" wait (the GC30 re-plug took 24 s in f1.7)
            char ds[160]; DescribeTo(d.pub, ds, sizeof(ds));
            LOG("INPUT2: %s %s", present ? "+ controller" : "- controller gone:", ds);
            LOG("INPUT2 PATH: #%d %s", d.pub.id, d.pub.path);   // the Windows device path only in the detailed log (a PC-specific id)
        }
    }
}

void Publish()
{
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_n; ++i)
    {
        g_pub[i] = g_d[i].pub;
        if (g_pub[i].xinputSlot >= 0) g_pub[i].apis |= API_XINPUT;
    }
    g_pubN = g_n;
    LeaveCriticalSection(&g_lock);
}

void LogInventory()
{
    int n = 0;
    for (int i = 0; i < g_n; ++i) if (g_d[i].pub.present) ++n;
    LOG("INPUT2: %d controller(s) found; XInput slots connected: %s%s%s%s", n,
        g_xConn[0] ? "0 " : "", g_xConn[1] ? "1 " : "", g_xConn[2] ? "2 " : "", g_xConn[3] ? "3 " : "");
}

DWORD WINAPI Worker(void*)
{
    LoadHidApi();
    DWORD lastScan = 0; bool first = true;
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
    LONG loops = 0, maxUs = 0, maxScanUs = 0; DWORD lastDiag = GetTickCount();
    while (g_running)
    {
        LARGE_INTEGER q0; QueryPerformanceCounter(&q0);
        DWORD now = GetTickCount();
        bool scan = first || now - lastScan >= SCAN_MS || (g_diWaitWindow && now - lastScan >= 250);
        if (scan)
        {
            ++g_scanNo; lastScan = now;
            ScanHid();
            ScanDi();
            UpdatePresence();
        }
        PollXInput(scan);
        PollDevices();
        MatchXInput();
        Publish();
        if (first) { first = false; LogInventory(); }
        {   // DIAG: how fast this loop runs (every controller's state is published once per loop)
            LARGE_INTEGER q1; QueryPerformanceCounter(&q1);
            LONG us = (LONG)((q1.QuadPart - q0.QuadPart) * 1000000 / qf.QuadPart);
            ++loops; if (scan) { if (us > maxScanUs) maxScanUs = us; } else if (us > maxUs) maxUs = us;
            if (now - lastDiag >= 30000)
            {
                lastDiag = now;
                LOG("DIAG IN2: last 30 s - %ld loops (%.0f per s), slowest %.1f ms (with a device scan %.1f ms)", loops, loops / 30.0, maxUs / 1000.0, maxScanUs / 1000.0);
                for (int i = 0; i < g_n; ++i)
                {
                    Dev& d = g_d[i];
                    if (!d.pub.present) continue;
                    if (d.sony) LOG("DIAG IN2: #%d %s - HID reader %s: %ld reports (%.0f per s), longest gap %ld ms, %ld read at once (queued), %ld sent by the pad but never arrived (its counter jumped); DirectInput reads skipped %ld, slowest DirectInput read %.1f ms",
                                    d.pub.id, d.pub.name, d.readerOn ? "on" : "OFF", d.dgReports, d.dgReports / 30.0, d.dgMaxGapMs, d.dgQuick, d.dgLost, d.dgDiSkipped, d.dgDiMaxUs / 1000.0);
                    else if (d.di) LOG("DIAG IN2: #%d %s - slowest DirectInput read %.1f ms", d.pub.id, d.pub.name, d.dgDiMaxUs / 1000.0);
                    d.dgReports = d.dgQuick = d.dgMaxGapMs = d.dgDiMaxUs = d.dgDiSkipped = d.dgLost = 0;
                }
                loops = maxUs = maxScanUs = 0;
            }
        }
        Sleep(POLL_MS);
    }
    return 0;
}

}   // namespace detail
using namespace detail;

// ---------------------------------------------------------------------------------------------------------
// interface
// ---------------------------------------------------------------------------------------------------------
bool Start(const Host& host)
{
    if (g_running) return true;
    g_h = host;
    InitializeCriticalSection(&g_lock);
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void*)&Start, &self);
    {   // System32's dinput8, loaded now (an import would load it - or a dinput8.dll in the game folder - with this DLL)
        typedef HRESULT (WINAPI* Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
        wchar_t path[MAX_PATH]; GetSystemDirectoryW(path, MAX_PATH); wcscat(path, L"\\dinput8.dll");
        HMODULE m = LoadLibraryW(path);
        Create_t create = m ? (Create_t)(void*)GetProcAddress(m, "DirectInput8Create") : nullptr;
        if (!create || FAILED(create(self ? self : GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&g_di, nullptr)))
        { g_di = nullptr; LOG("INPUT2: DirectInput not available - DirectInput controllers not watched"); }
    }
    // xinput1_4 is NOT loaded (f1.11 loaded it for XInputGetCapabilitiesEx - test 10 showed the game stopped calling
    // this DLL's XInputGetState right after the start, suspected cause). A press ties an XInput slot.
    InterlockedExchange(&g_running, 1);
    HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!t) { InterlockedExchange(&g_running, 0); LOG("INPUT2: couldn't start its thread - not running"); return false; }
    CloseHandle(t);
    LOG("INPUT2: new input module started (it reads every player's controller)");
    return true;
}

bool Running() { return g_running != 0; }

bool LooksLikeGamepad(bool xinputFamily, bool sony, int hidUsage, int diType, int diButtons, int diAxes)
{
    if (xinputFamily || sony) return true;
    if (diType >= 0x16 && diType <= 0x1C) return false;   // driving, flight, first person, device control, pointer, remote, supplemental
    if (diType == 0x15 || hidUsage == 0x05) return true;  // gamepad
    if (diType == 0x14 || hidUsage == 0x04) return diButtons >= 8 && diAxes >= 2;   // joystick: by its buttons / axes (DirectInput caps)
    return false;
}

int Count()
{
    if (!g_running) return 0;
    EnterCriticalSection(&g_lock); int n = g_pubN; LeaveCriticalSection(&g_lock);
    return n;
}

bool Get(int index, Device* out)
{
    if (!g_running || !out) return false;
    EnterCriticalSection(&g_lock);
    bool ok = index >= 0 && index < g_pubN;
    if (ok) *out = g_pub[index];
    LeaveCriticalSection(&g_lock);
    return ok;
}

bool FindById(int id, Device* out)
{
    if (!g_running || !out || id <= 0) return false;
    bool ok = false;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_pubN && !ok; ++i) if (g_pub[i].id == id) { *out = g_pub[i]; ok = true; }
    LeaveCriticalSection(&g_lock);
    return ok;
}

bool ReadById(int id, Reading* out)
{
    if (!g_running || !out || id <= 0) return false;
    bool ok = false;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_pubN && !ok; ++i)
        if (g_pub[i].id == id)
        {
            const Device& d = g_pub[i];
            out->present = d.present; out->source = d.source; out->layout = d.layout; out->xinputFamily = d.xinputFamily;
            out->xinputSlot = d.xinputSlot; out->vid = d.vid; out->pid = d.pid; out->state = d.state; out->stateMs = d.stateMs;
            ok = true;
        }
    LeaveCriticalSection(&g_lock);
    return ok;
}

void Describe(const Device& d, char* out, size_t n) { DescribeTo(d, out, n); }

#ifdef IN2_TEST_HOOKS
// (review #20) TEST ONLY - made-up controllers for tools/tests/unit_seats.cpp (built with -DIN2_TEST_HOOKS; not in the DLL)
void TestStart(LogFn log) { g_h.log = log; InitializeCriticalSection(&g_lock); g_running = 1; }
int TestAddController(const char* path, uint16_t vid, uint16_t pid, bool gamepad)
{
    int i = NewDev(path); if (i < 0) return 0;
    g_d[i].pub.present = true; g_d[i].pub.apis = API_DINPUT; SetIdentity(g_d[i], vid, pid, "pad");
    g_d[i].pub.gamepad = gamepad;
    Publish();
    return g_d[i].pub.id;
}
bool TestSetPresent(int id, bool present)
{
    for (int i = 0; i < g_n; ++i) if (g_d[i].pub.id == id) { g_d[i].pub.present = present; Publish(); return true; }
    return false;
}
#endif


}   // namespace in2
