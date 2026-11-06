// Test for input2/map.cpp: ApplyMap must give exactly what the old code's VPadGet mapping gives (copied below from
// input/40_dinput_pads.inc, f1.2) for random DirectInput readings and random button maps. No game needed.
#include "../../input2/map.cpp"
#include <cstdio>
#include <cstdlib>
#include <cassert>
using namespace in2;
struct OldCfg { int btn[12]; int axis[4]; bool trigZ; };
struct OldJs { LONG lX, lY, lZ, lRx, lRy, lRz, sl[2]; DWORD pov; BYTE b[32]; };
static SHORT VAxis(LONG v, bool flip) { if (flip) v = -v - 1; if (v < -32768) v = -32768; if (v > 32767) v = 32767; return (SHORT)v; }
static XPad OldMap(const OldJs& js, const OldCfg& c)   // the old VPadGet's mapping, verbatim apart from names
{
    auto B = [&](int n) { return n >= 1 && n <= 32 && (js.b[n - 1] & 0x80); };
    static const WORD xb[12] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0, 0, 0x0020, 0x0010, 0x0040, 0x0080};
    XPad x = {};
    for (int k = 0; k < 12; ++k) if (xb[k] && B(c.btn[k])) x.buttons |= xb[k];
    if (B(c.btn[6])) x.lt = 255;
    if (B(c.btn[7])) x.rt = 255;
    DWORD pov = js.pov;
    if ((pov & 0xFFFF) != 0xFFFF)
    {
        DWORD a = pov % 36000;
        if (a >= 31500 || a <= 4500)  x.buttons |= 0x0001;
        if (a >= 4500 && a <= 13500)  x.buttons |= 0x0008;
        if (a >= 13500 && a <= 22500) x.buttons |= 0x0002;
        if (a >= 22500 && a <= 31500) x.buttons |= 0x0004;
    }
    LONG ax[8] = {js.lX, js.lY, js.lZ, js.lRx, js.lRy, js.lRz, js.sl[0], js.sl[1]};
    x.lx = VAxis(ax[c.axis[0] & 7], false); x.ly = VAxis(ax[c.axis[1] & 7], true);
    x.rx = VAxis(ax[c.axis[2] & 7], false); x.ry = VAxis(ax[c.axis[3] & 7], true);
    if (c.trigZ)
    {
        LONG z = js.lZ;
        if (z > 8000)  { LONG t = (z - 8000) * 255 / (32767 - 8000); x.lt = (BYTE)(t > 255 ? 255 : t); }
        if (z < -8000) { LONG t = (-z - 8000) * 255 / (32768 - 8000); x.rt = (BYTE)(t > 255 ? 255 : t); }
    }
    return x;
}
int main()
{
    srand(12345);
    auto R = [](int lo, int hi) { return lo + (int)(((unsigned)rand() * 32768u + (unsigned)rand()) % (unsigned)(hi - lo + 1)); };
    static const DWORD povs[] = {0xFFFFFFFF, 0, 4500, 9000, 13500, 18000, 22500, 27000, 31500, 35999, 1234};
    for (int t = 0; t < 200000; ++t)
    {
        OldJs js = {}; OldCfg c = {};
        js.lX = R(-32768, 32767); js.lY = R(-32768, 32767); js.lZ = R(-32768, 32767); js.lRx = R(-32768, 32767);
        js.lRy = R(-32768, 32767); js.lRz = R(-32768, 32767); js.sl[0] = R(-32768, 32767); js.sl[1] = R(-32768, 32767);
        js.pov = povs[R(0, 10)];
        for (int i = 0; i < 32; ++i) js.b[i] = R(0, 3) == 0 ? 0x80 : 0;
        for (int k = 0; k < 12; ++k) c.btn[k] = R(0, 32);
        for (int k = 0; k < 4; ++k) c.axis[k] = R(0, 7);
        c.trigZ = R(0, 1);
        Pad p = {}; p.diPov = js.pov;
        LONG ax[8] = {js.lX, js.lY, js.lZ, js.lRx, js.lRy, js.lRz, js.sl[0], js.sl[1]};
        for (int i = 0; i < 8; ++i) p.diAxis[i] = ax[i];
        for (int i = 0; i < 32; ++i) if (js.b[i] & 0x80) p.diButtons |= 1u << i;
        ButtonMap m; for (int k = 0; k < 12; ++k) m.btn[k] = c.btn[k]; for (int k = 0; k < 4; ++k) m.axis[k] = c.axis[k]; m.trigZ = c.trigZ;
        XPad a, b = OldMap(js, c); ApplyMap(p, m, &a);
        if (memcmp(&a, &b, sizeof(a))) { printf("MISMATCH at %d: new %04X %d %d %d %d %d %d, old %04X %d %d %d %d %d %d\n", t,
            a.buttons, a.lt, a.rt, a.lx, a.ly, a.rx, a.ry, b.buttons, b.lt, b.rt, b.lx, b.ly, b.rx, b.ry); return 1; }
    }
    {   // f1.11: [Pad.VID:PID] sections
        wchar_t ini[MAX_PATH]; GetTempPathW(MAX_PATH, ini); wcscat(ini, L"unit_map_test.ini");
        FILE* f = _wfopen(ini, L"wb");
        fputs("[Pad.0D22:0C34]\r\nName=msi GC30 V2\r\nA=1\r\nB = 2 \r\nX=4\r\nY=5\r\nLB=7\r\nRB=8\r\nLT=9\r\nRT=10\r\nBack=11\r\nStart=12\r\nLS=14\r\nRS=15\r\nLeftX=X\r\nLeftY=Y\r\nRightX=z\r\nRightY=Rz\r\n"
              "[Pad.045E:028E]\r\nTriggersOnZ=1\r\nRightX=Rx\r\nRightY=Ry\r\nLT=0\r\nRT=0\r\n", f);
        fclose(f);
        ButtonMap m; memset(&m, 0, sizeof(m));
        assert(MapFromIni(ini, 0x0D22, 0x0C34, &m));
        static const int want[12] = {1, 2, 4, 5, 7, 8, 9, 10, 11, 12, 14, 15};
        for (int k = 0; k < 12; ++k) assert(m.btn[k] == want[k]);
        assert(m.axis[0] == 0 && m.axis[1] == 1 && m.axis[2] == 2 && m.axis[3] == 5 && !m.trigZ);
        assert(MapFromIni(ini, 0x045E, 0x028E, &m));
        assert(!m.trigZ && m.axis[2] == 3 && m.axis[3] == 4 && m.btn[6] == 0 && m.btn[0] == 2);   // missing keys: PlayStation numbering; f1.79: TriggersOnZ ignored
        ButtonMap u; memset(&u, 0x55, sizeof(u));
        assert(!MapFromIni(ini, 0x054C, 0x0CE6, &u) && u.btn[0] == 0x55555555);   // no section: untouched
        DeleteFileW(ini);
        printf("MapFromIni OK\n");
    }
    printf("ALL OK (200000 random readings + maps identical to the old mapping)\n");
    return 0;
}
