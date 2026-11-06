// input2/map.cpp - see map.h. Mirrors the mapping part of the old code's VPadGet (input/40_dinput_pads.inc, removed f1.7 / f1.85).
#include "map.h"
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cstdlib>

namespace in2 {
namespace {
// a trigger pair on one Z axis (TrigZ): beyond this much travel either way it counts as LT / RT (finding #21)
constexpr int TRIGGER_ON_Z_DEADZONE = 8000;
int16_t Axis(long v, bool flip) { if (flip) v = -v - 1; if (v < -32768) v = -32768; if (v > 32767) v = 32767; return (int16_t)v; }
void TrimW(wchar_t* s)
{
    size_t l = wcslen(s); while (l && iswspace(s[l - 1])) s[--l] = 0;
    size_t i = 0; while (s[i] && iswspace(s[i])) ++i;
    if (i) memmove(s, s + i, (wcslen(s + i) + 1) * sizeof(wchar_t));
}
}

bool MapFromIni(const wchar_t* iniPath, uint16_t vid, uint16_t pid, ButtonMap* out)
{
    if (!iniPath || !out) return false;
    wchar_t sec[32]; swprintf(sec, 32, L"Pad.%04X:%04X", vid, pid);
    wchar_t keys[512];
    if (!GetPrivateProfileStringW(sec, nullptr, L"", keys, 512, iniPath)) return false;   // no such section (or empty)
    static const wchar_t* bk[12] = {L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT", L"RT", L"Back", L"Start", L"LS", L"RS"};
    static const int bd[12] = {2, 3, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    static const wchar_t* ak[4] = {L"LeftX", L"LeftY", L"RightX", L"RightY"};
    static const wchar_t* an[8] = {L"X", L"Y", L"Z", L"Rx", L"Ry", L"Rz", L"Slider1", L"Slider2"};
    static const int ad[4] = {0, 1, 2, 5};
    ButtonMap m;
    for (int i = 0; i < 12; ++i)
    {
        wchar_t v[32]; GetPrivateProfileStringW(sec, bk[i], L"", v, 32, iniPath); TrimW(v);
        int b = bd[i];
        if (v[0]) { wchar_t* e = nullptr; long n = wcstol(v, &e, 10); if (e != v && n >= 0 && n <= 32) b = (int)n; }
        m.btn[i] = b;
    }
    for (int i = 0; i < 4; ++i)
    {
        wchar_t v[32]; GetPrivateProfileStringW(sec, ak[i], L"", v, 32, iniPath); TrimW(v);
        m.axis[i] = ad[i];
        for (int k = 0; k < 8; ++k) if (!_wcsicmp(v, an[k])) m.axis[i] = k;
    }
    m.trigZ = false;   // [Pad.*] TriggersOnZ no longer read (the app stopped offering it in f1.39t). Only the built-in
                       // Xbox-type default (In2MapFor, input/40b) still sets trigZ - for an Xbox-type pad read through DirectInput.
    *out = m;
    return true;
}

void ApplyMap(const Pad& raw, const ButtonMap& m, XPad* out)
{
    XPad x; memset(&x, 0, sizeof(x));
    auto B = [&](int n) { return n >= 1 && n <= 32 && (raw.diButtons & (1u << (n - 1))); };
    static const uint16_t xb[12] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0, 0, 0x0020, 0x0010, 0x0040, 0x0080};
    for (int k = 0; k < 12; ++k) if (xb[k] && B(m.btn[k])) x.buttons |= xb[k];
    if (B(m.btn[6])) x.lt = 255;
    if (B(m.btn[7])) x.rt = 255;
    uint32_t pov = raw.diPov;
    if ((pov & 0xFFFF) != 0xFFFF)
    {
        uint32_t a = pov % 36000;
        if (a >= 31500 || a <= 4500)  x.buttons |= 0x0001;   // up
        if (a >= 4500 && a <= 13500)  x.buttons |= 0x0008;   // right
        if (a >= 13500 && a <= 22500) x.buttons |= 0x0002;   // down
        if (a >= 22500 && a <= 31500) x.buttons |= 0x0004;   // left
    }
    x.lx = Axis(raw.diAxis[m.axis[0] & 7], false);
    x.ly = Axis(raw.diAxis[m.axis[1] & 7], true);   // DirectInput: down is +, XInput: up is +
    x.rx = Axis(raw.diAxis[m.axis[2] & 7], false);
    x.ry = Axis(raw.diAxis[m.axis[3] & 7], true);
    if (m.trigZ)
    {
        long z = raw.diAxis[2];
        if (z > TRIGGER_ON_Z_DEADZONE)  { long t = (z - TRIGGER_ON_Z_DEADZONE) * 255 / (32767 - TRIGGER_ON_Z_DEADZONE); x.lt = (uint8_t)(t > 255 ? 255 : t); }
        if (z < -TRIGGER_ON_Z_DEADZONE) { long t = (-z - TRIGGER_ON_Z_DEADZONE) * 255 / (32768 - TRIGGER_ON_Z_DEADZONE); x.rt = (uint8_t)(t > 255 ? 255 : t); }
    }
    *out = x;
}

void FromPad(const Pad& p, XPad* out)
{
    out->buttons = p.buttons; out->lt = p.lt; out->rt = p.rt;
    out->lx = p.lx; out->ly = p.ly; out->rx = p.rx; out->ry = p.ry;
}

}   // namespace in2
