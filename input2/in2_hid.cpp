// input2/in2_hid.cpp - the controller module's HID channel: identity for every gamepad, and the DualShock 4 /
// DualSense reports read from its own handle (one reader thread per PlayStation pad). Split from in2.cpp (review #19).
#include "in2_internal.h"

namespace in2 {
namespace detail {

namespace {

// ---------------------------------------------------------------------------------------------------------
// decoding
// ---------------------------------------------------------------------------------------------------------
int16_t Axis8(uint8_t v, bool flip) { int t = ((int)v - 128) * 256; if (flip) t = -t - 1; return ClampS(t); }

// Sony face/shoulder bytes (same bits on DS4 and DualSense): f = D-pad nibble + square 0x10 cross 0x20 circle 0x40
// triangle 0x80; s = L1 0x01 R1 0x02 (L2 0x04 R2 0x08 digital, unused) Share/Create 0x10 Options 0x20 L3 0x40 R3 0x80
uint16_t SonyButtons(uint8_t f, uint8_t s)
{
    static const uint8_t  fb[4] = {0x10, 0x20, 0x40, 0x80};
    static const uint16_t fx[4] = {0x4000, 0x1000, 0x2000, 0x8000};   // square X, cross A, circle B, triangle Y
    static const uint8_t  sb[6] = {0x01, 0x02, 0x10, 0x20, 0x40, 0x80};
    static const uint16_t sx[6] = {0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080};   // LB RB Back Start LS RS
    uint16_t b = 0;
    for (int i = 0; i < 4; ++i) if (f & fb[i]) b |= fx[i];
    for (int i = 0; i < 6; ++i) if (s & sb[i]) b |= sx[i];
    return b;
}

}   // namespace


// DualShock 4 report data (USB 0x01 from byte 1, Bluetooth 0x11 from byte 3):
// LX LY RX RY, [4] D-pad nibble + square cross circle triangle, [5] L1 R1 L2 R2 Share Options L3 R3, [6] PS pad, [7] L2 [8] R2
void DecodeDs4(const uint8_t* p, Pad& o)
{
    memset(&o, 0, sizeof(o));
    o.lx = Axis8(p[0], false); o.ly = Axis8(p[1], true); o.rx = Axis8(p[2], false); o.ry = Axis8(p[3], true);
    static const uint16_t dpad[8] = {0x1, 0x9, 0x8, 0xA, 0x2, 0x6, 0x4, 0x5};
    int dp = p[4] & 0x0F; uint16_t b = dp < 8 ? dpad[dp] : 0;
    b |= SonyButtons(p[4], p[5]);
    o.buttons = b; o.lt = p[7]; o.rt = p[8];
}

// DualSense report data (USB 0x01 from byte 1, Bluetooth 0x31 from byte 2):
// LX LY RX RY L2 R2 counter, [7] D-pad nibble + square cross circle triangle, [8] L1 R1 L2 R2 Create Options L3 R3, [9] PS pad mute
void DecodeDualSense(const uint8_t* p, Pad& o)
{
    memset(&o, 0, sizeof(o));
    o.lx = Axis8(p[0], false); o.ly = Axis8(p[1], true); o.rx = Axis8(p[2], false); o.ry = Axis8(p[3], true);
    o.lt = p[4]; o.rt = p[5];
    static const uint16_t dpad[8] = {0x1, 0x9, 0x8, 0xA, 0x2, 0x6, 0x4, 0x5};
    int dp = p[7] & 0x0F; uint16_t b = dp < 8 ? dpad[dp] : 0;
    b |= SonyButtons(p[7], p[8]);
    o.buttons = b;
}

namespace {

// ---------------------------------------------------------------------------------------------------------
// HID: identity for every gamepad, reports for DualShock 4 / DualSense
// ---------------------------------------------------------------------------------------------------------
typedef HDEVINFO (WINAPI* SdGetClassDevs_t)(const GUID*, PCWSTR, HWND, DWORD);
typedef BOOL (WINAPI* SdEnumIfaces_t)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL (WINAPI* SdIfaceDetail_t)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI* SdDestroy_t)(HDEVINFO);
typedef BOOLEAN (__stdcall* HidAttr_t)(HANDLE, PHIDD_ATTRIBUTES);
typedef BOOLEAN (__stdcall* HidProd_t)(HANDLE, PVOID, ULONG);
typedef BOOLEAN (__stdcall* HidPre_t)(HANDLE, PHIDP_PREPARSED_DATA*);
typedef BOOLEAN (__stdcall* HidFree_t)(PHIDP_PREPARSED_DATA);
typedef NTSTATUS (__stdcall* HidCaps_t)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);
typedef BOOLEAN (__stdcall* HidFeat_t)(HANDLE, PVOID, ULONG);
struct HidApi { SdGetClassDevs_t gcd; SdEnumIfaces_t ei; SdIfaceDetail_t det; SdDestroy_t des; HidAttr_t attr; HidProd_t prod; HidPre_t pre; HidFree_t fre; HidCaps_t caps; HidFeat_t getFeat; bool ok; } g_hidApi;


}   // namespace

void LoadHidApi()
{
    memset(&g_hidApi, 0, sizeof(g_hidApi));
    HMODULE sa = LoadLibraryW(L"setupapi.dll"), hd = LoadLibraryW(L"hid.dll");
    if (!sa || !hd) { LOG("INPUT2: setupapi.dll / hid.dll not loaded - PlayStation pads only through DirectInput"); return; }
    g_hidApi.gcd  = (SdGetClassDevs_t)(void*)GetProcAddress(sa, "SetupDiGetClassDevsW");
    g_hidApi.ei   = (SdEnumIfaces_t)(void*)GetProcAddress(sa, "SetupDiEnumDeviceInterfaces");
    g_hidApi.det  = (SdIfaceDetail_t)(void*)GetProcAddress(sa, "SetupDiGetDeviceInterfaceDetailW");
    g_hidApi.des  = (SdDestroy_t)(void*)GetProcAddress(sa, "SetupDiDestroyDeviceInfoList");
    g_hidApi.attr = (HidAttr_t)(void*)GetProcAddress(hd, "HidD_GetAttributes");
    g_hidApi.prod = (HidProd_t)(void*)GetProcAddress(hd, "HidD_GetProductString");
    g_hidApi.pre  = (HidPre_t)(void*)GetProcAddress(hd, "HidD_GetPreparsedData");
    g_hidApi.fre  = (HidFree_t)(void*)GetProcAddress(hd, "HidD_FreePreparsedData");
    g_hidApi.caps = (HidCaps_t)(void*)GetProcAddress(hd, "HidP_GetCaps");
    g_hidApi.getFeat = (HidFeat_t)(void*)GetProcAddress(hd, "HidD_GetFeature");   // (optional)
    g_hidApi.ok = g_hidApi.gcd && g_hidApi.ei && g_hidApi.det && g_hidApi.des && g_hidApi.attr && g_hidApi.prod && g_hidApi.pre && g_hidApi.fre && g_hidApi.caps;
    if (!g_hidApi.ok) LOG("INPUT2: HID functions missing - PlayStation pads only through DirectInput");
}

namespace {


// over Bluetooth a DualSense / DualShock 4 starts in "simple" mode: short report 0x01 (DS4 layout, fewer reports)
// until the host reads its calibration feature report (DualSense 0x05, DS4 0x02) - then full reports 0x31 / 0x11.
// decoded the DualSense's simple report with the USB layout (wrong buttons) and relied on the game to switch it
// to full reports (laggy movement). Now the reader switches it itself and decodes both kinds correctly.
void SonyFullMode(HANDLE h, int idx, int kind)
{
    if (!g_hidApi.getFeat) return;
    uint8_t fb[128]; memset(fb, 0, sizeof(fb));
    ULONG fl = g_d[idx].featLen >= 2 && g_d[idx].featLen <= sizeof(fb) ? g_d[idx].featLen : (kind == 2 ? 41 : 37);
    fb[0] = kind == 2 ? 0x05 : 0x02;
    BOOLEAN ok = g_hidApi.getFeat(h, fb, fl);
    LOG("INPUT2: #%d %s (Bluetooth) - asked for full reports (feature report 0x%02X): %s", g_d[idx].pub.id, g_d[idx].pub.name, fb[0], ok ? "done" : "failed");
}
DWORD WINAPI HidReader(void* arg)
{
    int idx = (int)(intptr_t)arg;
    HANDLE h = g_d[idx].hidRead; int kind = g_d[idx].sony;
    uint8_t buf[1024];   // ReadFile needs at least the collection's input report length (Bluetooth DS4: 547)
    DWORD len = g_d[idx].inLen >= 11 && g_d[idx].inLen <= sizeof(buf) ? g_d[idx].inLen : 64;
    bool bt = g_d[idx].inLen > 64;   // USB: 64-byte input reports; Bluetooth: DualSense 78, DS4 547
    bool any = false; int lastId = -1, simpleN = 0, asks = 0, modeLogs = 0; DWORD askT = 0;
    if (bt) { SonyFullMode(h, idx, kind); askT = GetTickCount(); asks = 1; }
    LARGE_INTEGER qf, qlast; QueryPerformanceFrequency(&qf); qlast.QuadPart = 0;
    while (g_running)
    {
        DWORD got = 0;
        if (!ReadFile(h, buf, len, &got, nullptr)) break;   // unplugged / closed
        any = true;
        if (kind == 2 && buf[0] == 0x31 && got >= 9)
        {   // DIAG: the DualSense's own report counter (data byte 6) - jumps = reports it sent that never arrived here
            uint8_t c = buf[8];
            if (g_d[idx].dgLastCnt >= 0) { int jump = (int)(uint8_t)(c - (uint8_t)g_d[idx].dgLastCnt); if (jump > 1) InterlockedExchangeAdd(&g_d[idx].dgLost, jump - 1); }
            g_d[idx].dgLastCnt = c;
        }
        {   // DIAG: report rate, longest gap, reads that returned at once (= reports were queued up -> we're behind)
            LARGE_INTEGER qn; QueryPerformanceCounter(&qn);
            if (qlast.QuadPart)
            {
                LONG us = (LONG)((qn.QuadPart - qlast.QuadPart) * 1000000 / qf.QuadPart);
                if (us < 300) InterlockedIncrement(&g_d[idx].dgQuick);
                if (us / 1000 > g_d[idx].dgMaxGapMs) g_d[idx].dgMaxGapMs = us / 1000;
            }
            qlast = qn; InterlockedIncrement(&g_d[idx].dgReports);
        }
        if (got < 10) continue;
        Pad p; bool ok = false; int id = buf[0];
        bool simple = id == 0x01 && bt;   // Bluetooth simple report: DS4 layout from byte 1 (both kinds)
        if (kind == 2 && !simple)
        {
            int off = id == 0x31 ? 2 : id == 0x01 ? 1 : -1;
            if (off > 0 && got >= (DWORD)off + 10) { DecodeDualSense(buf + off, p); ok = true; }
        }
        else
        {
            int off = id == 0x01 ? 1 : id == 0x11 ? 3 : -1;
            if (off > 0 && got >= (DWORD)off + 9) { DecodeDs4(buf + off, p); ok = true; }
        }
        if (id != lastId && modeLogs < 6)
        {
            lastId = id; ++modeLogs;
            LOG("INPUT2: #%d %s - %s reports (0x%02X)", g_d[idx].pub.id, g_d[idx].pub.name, simple ? "Bluetooth simple" : bt ? "Bluetooth full" : "USB", id);
        }
        if (simple && ++simpleN > 250 && asks < 5 && GetTickCount() - askT > 2000)
        {   // still simple reports: ask again (a few times)
            simpleN = 0; askT = GetTickCount(); ++asks; SonyFullMode(h, idx, kind);
        }
        if (ok) { EnterCriticalSection(&g_lock); g_d[idx].hidState = p; g_d[idx].hidMs = GetTickCount(); g_d[idx].hidFull = !simple; LeaveCriticalSection(&g_lock); }
    }
    DWORD err = GetLastError();
    CloseHandle(h);
    EnterCriticalSection(&g_lock); g_d[idx].hidRead = nullptr; g_d[idx].hidMs = 0; if (!any) g_d[idx].readFails = (int)(err ? err : 1); LeaveCriticalSection(&g_lock);
    InterlockedExchange(&g_d[idx].readerOn, 0);
    return 0;
}

void StartHidReader(int idx, const wchar_t* wpath)
{
    Dev& d = g_d[idx];
    if (d.readerOn) return;
    DWORD now = GetTickCount();
    int rf; EnterCriticalSection(&g_lock); rf = d.readFails; d.readFails = 0; LeaveCriticalSection(&g_lock);
    if (rf)
    {   // the last reader failed on its very first read: say so once, then retry only every 30 s
        char ds[160]; DescribeTo(d.pub, ds, sizeof(ds));
        if (d.readFailLogs++ < 2) LOG("INPUT2: %s - reading its HID reports failed (error %d, report length %u) - retrying every 30 s", ds, rf, (unsigned)d.inLen);
        d.readRetryT = now + 30000;
    }
    if (d.readRetryT && (int)(now - d.readRetryT) < 0) return;
    // read access only: a handle with write access would make a later open by the game that shares only reading fail
    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        if (!d.readLogged) { d.readLogged = true; char ds[160]; DescribeTo(d.pub, ds, sizeof(ds)); LOG("INPUT2: %s - can't open its HID reports (error %lu) - read through DirectInput instead", ds, GetLastError()); }
        return;
    }
    d.hidRead = h;
    InterlockedExchange(&d.readerOn, 1);
    HANDLE t = CreateThread(nullptr, 0, HidReader, (void*)(intptr_t)idx, 0, nullptr);
    if (!t) { InterlockedExchange(&d.readerOn, 0); d.hidRead = nullptr; CloseHandle(h); return; }
    CloseHandle(t);
    if (!d.readLogged) { d.readLogged = true; char ds[160]; DescribeTo(d.pub, ds, sizeof(ds)); LOG("INPUT2: %s - reading its HID reports", ds); }
}

}   // namespace


void ScanHid()
{
    if (!g_hidApi.ok) return;
    static const GUID hidGuid = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    HDEVINFO set = g_hidApi.gcd(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return;
    for (DWORD k = 0; k < 256; ++k)
    {
        SP_DEVICE_INTERFACE_DATA ifd; memset(&ifd, 0, sizeof(ifd)); ifd.cbSize = sizeof(ifd);
        if (!g_hidApi.ei(set, nullptr, &hidGuid, k, &ifd)) break;
        union { SP_DEVICE_INTERFACE_DETAIL_DATA_W det; unsigned char raw[sizeof(DWORD) + 520 * sizeof(wchar_t)]; } u;
        memset(&u, 0, sizeof(u)); u.det.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!g_hidApi.det(set, &ifd, &u.det, sizeof(u), nullptr, nullptr)) continue;
        const wchar_t* wpath = u.det.DevicePath;
        char path[200]; WideToUtf8(wpath, path, sizeof(path)); Lower(path);
        int idx = FindByPathIdx(path);
        uint32_t ph = 2166136261u; for (const char* c = path; *c; ++c) ph = (ph ^ (uint8_t)*c) * 16777619u;   // FNV-1a
        if (idx < 0) { bool rej = false; for (int r = 0; r < g_rejN; ++r) if (g_rej[r] == ph) { rej = true; break; } if (rej) continue; }
        if (idx >= 0 && g_d[idx].hidSeen) { g_d[idx].hidSeenScan = g_scanNo; if (g_d[idx].sony && !g_d[idx].readerOn) StartHidReader(idx, wpath); continue; }
        // a HID interface we haven't sorted yet: is it a joystick / gamepad?
        HANDLE q = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (q == INVALID_HANDLE_VALUE) continue;
        bool pad = false, capsOk = false; USHORT inLen = 0, featLen = 0, usage = 0; HIDD_ATTRIBUTES at; memset(&at, 0, sizeof(at)); at.Size = sizeof(at);
        PHIDP_PREPARSED_DATA pp = nullptr;
        if (g_hidApi.pre(q, &pp) && pp)
        {
            HIDP_CAPS c; memset(&c, 0, sizeof(c));
            if (g_hidApi.caps(pp, &c) == HIDP_STATUS_SUCCESS) { capsOk = true; inLen = c.InputReportByteLength; featLen = c.FeatureReportByteLength; usage = c.Usage; pad = c.UsagePage == 0x01 && (c.Usage == 0x04 || c.Usage == 0x05); }
            g_hidApi.fre(pp);
        }
        wchar_t wn[64] = L""; char name[64] = "";
        bool haveAttr = pad && g_hidApi.attr(q, &at);
        if (pad && g_hidApi.prod(q, wn, sizeof(wn) - sizeof(wchar_t))) { wn[63] = 0; WideToUtf8(wn, name, sizeof(name)); }
        CloseHandle(q);
        if (!pad) { if (capsOk && g_rejN < 256) g_rej[g_rejN++] = ph; continue; }   // not a controller: don't open it again
        if (idx < 0) idx = NewDev(path);
        if (idx < 0) continue;   // table full (logged in NewDev)
        Dev& d = g_d[idx];
        d.inLen = inLen; d.featLen = featLen; d.hidUsage = (uint8_t)usage;
        d.hidSeen = true; d.hidSeenScan = g_scanNo; d.pub.apis |= API_HID;
        SetIdentity(d, haveAttr ? at.VendorID : 0, haveAttr ? at.ProductID : 0, name);
        if (d.sony) StartHidReader(idx, wpath);
    }
    g_hidApi.des(set);
}

}   // namespace detail
}   // namespace in2
