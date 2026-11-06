// input2/in2_dinput.cpp - the controller module's DirectInput channel: its own device objects for every game
// controller, read with the ORIGINAL GetDeviceState (never the mod's filter). Split from in2.cpp (review #19).
#include "in2_internal.h"

namespace in2 {
namespace detail {


// DIJOYSTATE2 with every axis ranged -32768..32767 (set when the device is opened).
void DecodeDi(const DIJOYSTATE2& js, Layout lay, Pad& o)
{
    memset(&o, 0, sizeof(o));
    auto B = [&](int i) { return (js.rgbButtons[i] & 0x80) != 0; };
    for (int i = 0; i < 32; ++i) if (B(i)) o.raw |= 1u << i;
    o.diButtons = o.raw;
    const LONG ax[8] = {js.lX, js.lY, js.lZ, js.lRx, js.lRy, js.lRz, js.rglSlider[0], js.rglSlider[1]};
    for (int i = 0; i < 8; ++i) o.diAxis[i] = ax[i];
    o.diPov = js.rgdwPOV[0];
    uint16_t b = PovToDpad(js.rgdwPOV[0]);
    if (lay == LAYOUT_XBOX)
    {   // XInput pad seen through DirectInput: A B X Y LB RB Back Start LS RS = 0..9, sticks X/Y + Rx/Ry, triggers share Z
        static const uint16_t m[10] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080};
        for (int i = 0; i < 10; ++i) if (B(i)) b |= m[i];
        o.lx = ClampS(js.lX); o.ly = ClampS(-(long)js.lY - 1); o.rx = ClampS(js.lRx); o.ry = ClampS(-(long)js.lRy - 1);
        if (js.lZ > 1000) o.lt = (uint8_t)(js.lZ > 32767 ? 255 : js.lZ / 128);
        if (js.lZ < -1000) o.rt = (uint8_t)(-js.lZ > 32767 ? 255 : -js.lZ / 128);
        o.raw = 0;
    }
    else if (lay == LAYOUT_PLAYSTATION)
    {   // DS4 through DirectInput: square cross circle triangle L1 R1 L2 R2 Share Options L3 R3 PS pad = 0..13,
        // left X/Y, right Z/Rz, triggers Rx/Ry
        static const uint16_t m[12] = {0x4000, 0x1000, 0x2000, 0x8000, 0x0100, 0x0200, 0, 0, 0x0020, 0x0010, 0x0040, 0x0080};
        for (int i = 0; i < 12; ++i) if (m[i] && B(i)) b |= m[i];
        o.lx = ClampS(js.lX); o.ly = ClampS(-(long)js.lY - 1); o.rx = ClampS(js.lZ); o.ry = ClampS(-(long)js.lRz - 1);
        o.lt = (uint8_t)((js.lRx + 32768) >> 8); o.rt = (uint8_t)((js.lRy + 32768) >> 8);
        o.raw = 0;
    }
    else
    {   // unknown layout: D-pad and the left stick are safe guesses; the rest stays raw
        o.lx = ClampS(js.lX); o.ly = ClampS(-(long)js.lY - 1); o.rx = ClampS(js.lZ); o.ry = ClampS(-(long)js.lRz - 1);
    }
    o.buttons = b;
}

namespace {

// ---------------------------------------------------------------------------------------------------------
// DirectInput
// ---------------------------------------------------------------------------------------------------------
typedef HRESULT (STDMETHODCALLTYPE* DiGetState_t)(void*, DWORD, void*);

struct DiFound { int n; GUID g[MAXD]; wchar_t name[MAXD][64]; wchar_t inst[MAXD][64]; uint8_t type[MAXD]; };
BOOL CALLBACK DiEnumCb(LPCDIDEVICEINSTANCEW in, void* ctx)
{
    DiFound* f = (DiFound*)ctx;
    if (f->n >= MAXD) return DIENUM_STOP;
    f->g[f->n] = in->guidInstance;
    f->type[f->n] = (uint8_t)(in->dwDevType & 0xFF);   // DI8DEVTYPE_*
    wcsncpy(f->name[f->n], in->tszProductName, 63); f->name[f->n][63] = 0;
    wcsncpy(f->inst[f->n], in->tszInstanceName, 63); f->inst[f->n][63] = 0;
    ++f->n;
    return DIENUM_CONTINUE;
}

}   // namespace


HRESULT DiRead(IDirectInputDevice8W* dev, DIJOYSTATE2* js)
{
    // never through the mod's filter: the host hands back the original GetDeviceState, or nullptr = skip this read
    void* orig = g_h.diOrigGetState ? g_h.diOrigGetState(dev) : (*(void***)dev)[9];
    if (!orig) return E_PENDING;
    return ((DiGetState_t)orig)(dev, sizeof(*js), js);
}

void DiClose(Dev& d)
{
    if (d.di) { d.di->Unacquire(); d.di->Release(); d.di = nullptr; }
    d.pub.apis &= (uint8_t)~API_DINPUT;
}

void ScanDi()
{
    if (!g_di) return;
    HWND hwnd = g_h.gameWindow ? g_h.gameWindow() : nullptr;
    static bool waitLogged = false;
    g_diWaitWindow = !hwnd;   // the worker looks again every 250 ms until the window is there
    if (!hwnd) { if (!waitLogged) { waitLogged = true; LOG("INPUT2: waiting for the game window before opening DirectInput controllers"); } return; }
    DiFound f; memset(&f, 0, sizeof(f));
    if (FAILED(g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, DiEnumCb, &f, DIEDFL_ATTACHEDONLY))) return;
    for (int k = 0; k < f.n; ++k)
    {
        int have = -1;
        for (int i = 0; i < g_n; ++i) if (g_d[i].di && IsEqualGUID(g_d[i].diInst, f.g[k])) { have = i; break; }
        if (have >= 0) { g_d[have].diSeenScan = g_scanNo; continue; }
        bool skip = false;
        for (int i = 0; i < g_n; ++i)
            if (g_d[i].diSkipUntil && IsEqualGUID(g_d[i].diSkipGuid, f.g[k]))
            {
                if ((int)(GetTickCount() - g_d[i].diSkipUntil) < 0) { skip = true; g_d[i].diSeenScan = g_scanNo; }
                else g_d[i].diSkipUntil = 0;
            }
        if (skip) continue;
        IDirectInputDevice8W* dev = nullptr;
        if (FAILED(g_di->CreateDevice(f.g[k], &dev, nullptr)) || !dev) continue;
        if (FAILED(dev->SetDataFormat(&c_dfDIJoystick2)) || FAILED(dev->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
        { dev->Release(); continue; }
        DIPROPGUIDANDPATH gp; memset(&gp, 0, sizeof(gp));
        gp.diph.dwSize = sizeof(gp); gp.diph.dwHeaderSize = sizeof(DIPROPHEADER); gp.diph.dwHow = DIPH_DEVICE;
        char path[200] = "";
        if (SUCCEEDED(dev->GetProperty(DIPROP_GUIDANDPATH, &gp.diph))) { WideToUtf8(gp.wszPath, path, sizeof(path)); Lower(path); }
        if (!path[0])
        {   // no path (virtual device?) - identity falls back to the instance GUID
            snprintf(path, sizeof(path), "di-instance-{%08lx-%04x-%04x}", f.g[k].Data1, f.g[k].Data2, f.g[k].Data3);
        }
        DIPROPDWORD vp; memset(&vp, 0, sizeof(vp));
        vp.diph.dwSize = sizeof(vp); vp.diph.dwHeaderSize = sizeof(DIPROPHEADER); vp.diph.dwHow = DIPH_DEVICE;
        uint16_t vid = 0, pid = 0;
        if (SUCCEEDED(dev->GetProperty(DIPROP_VIDPID, &vp.diph))) { vid = LOWORD(vp.dwData); pid = HIWORD(vp.dwData); }
        DIPROPRANGE rg; memset(&rg, 0, sizeof(rg));
        rg.diph.dwSize = sizeof(rg); rg.diph.dwHeaderSize = sizeof(DIPROPHEADER); rg.diph.dwHow = DIPH_DEVICE; rg.lMin = -32768; rg.lMax = 32767;
        dev->SetProperty(DIPROP_RANGE, &rg.diph);   // this device object only (the game's own objects keep their ranges)
        dev->Acquire();
        int idx = FindByPathIdx(path);
        if (idx < 0) idx = NewDev(path);
        if (idx < 0) { dev->Release(); continue; }   // table full (logged in NewDev)
        Dev& d = g_d[idx];
        if (d.di) DiClose(d);
        d.di = dev; d.diInst = f.g[k]; d.diFails = 0; d.diSeenScan = g_scanNo; d.pub.apis |= API_DINPUT;
        {   // what kind of game controller it says it is (LooksLikeGamepad, in SetIdentity below)
            DIDEVCAPS caps; memset(&caps, 0, sizeof(caps)); caps.dwSize = sizeof(caps);
            d.diType = f.type[k];
            if (SUCCEEDED(dev->GetCapabilities(&caps))) { d.diButtons = (int)caps.dwButtons; d.diAxes = (int)caps.dwAxes; }
        }
        char name[64]; WideToUtf8(f.name[k], name, sizeof(name));
        SetIdentity(d, vid, pid, name);
        memcpy(d.pub.diName, name, sizeof(d.pub.diName)); d.pub.diName[sizeof(d.pub.diName) - 1] = 0;   // (same size as name)
        WideToUtf8(f.inst[k], d.pub.diInstName, sizeof(d.pub.diInstName));
    }
}

}   // namespace detail
}   // namespace in2
