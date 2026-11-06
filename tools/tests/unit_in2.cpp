// Test for the new input modules - build with mingw (see the command in README_FORK1.txt) and run under wine or Windows.
// No game needed. unit_in2 includes in2_internal.h (and is linked with input2/in2*.cpp - tools/test.sh) to check the
// decoding / matching internals directly; smoke_in2 starts the module.
#include "../../input2/in2_internal.h"
#include <cassert>
#include <cstdarg>
using namespace in2;
using namespace in2::detail;
static void Log(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); }
int main()
{
    g_h.log = Log; InitializeCriticalSection(&g_lock);
    uint16_t v, p;
    assert(VidPidFromPath("\\\\?\\hid#vid_054c&pid_0ce6&mi_03#7&1", &v, &p) && v == 0x054C && p == 0x0CE6);
    assert(VidPidFromPath("\\\\?\\hid#{00001124-0000-1000-8000-00805f9b34fb}_vid&0002054c_pid&0ce6#9&1", &v, &p) && v == 0x054C && p == 0x0CE6);
    assert(VidPidFromPath("\\\\?\\hid#vid_046d&pid_c21d&ig_00#8&2", &v, &p) && v == 0x046D && p == 0xC21D);
    assert(PovToDpad(0) == 0x1 && PovToDpad(9000) == 0x8 && PovToDpad(18000) == 0x2 && PovToDpad(27000) == 0x4 && PovToDpad(4500) == 0x9 && PovToDpad(31500) == 0x5 && PovToDpad(0xFFFFFFFF) == 0);
    // DualSense data: LX LY RX RY L2 R2 cnt [7] faces [8] shoulders
    uint8_t ds[10] = {0x80, 0x00, 0xFF, 0x80, 200, 10, 0, 0x08 | 0x20 | 0x80, 0x01 | 0x20, 0};
    Pad o; DecodeDualSense(ds, o);
    printf("DS: b=%04X lt=%d rt=%d lx=%d ly=%d rx=%d\n", o.buttons, o.lt, o.rt, o.lx, o.ly, o.rx);
    assert(o.buttons == (0x1000 | 0x8000 | 0x0100 | 0x0010) && o.lt == 200 && o.rt == 10 && o.ly == 32767 && o.rx > 32000);
    uint8_t d4[9] = {0x80, 0x80, 0x80, 0x80, 0x00 | 0x40, 0x02 | 0x10, 0, 255, 0};
    DecodeDs4(d4, o); printf("DS4: b=%04X lt=%d\n", o.buttons, o.lt);
    assert(o.buttons == (0x0001 | 0x2000 | 0x0200 | 0x0020) && o.lt == 255);
    DIJOYSTATE2 js; memset(&js, 0, sizeof(js)); js.rgdwPOV[0] = 0xFFFFFFFF; js.rgbButtons[0] = 0x80; js.rgbButtons[7] = 0x80; js.lZ = 32767; js.lY = -32768;
    DecodeDi(js, LAYOUT_XBOX, o); printf("DIx: b=%04X lt=%d rt=%d ly=%d\n", o.buttons, o.lt, o.rt, o.ly);
    assert(o.buttons == (0x1000 | 0x0010) && o.lt == 255 && o.rt == 0 && o.ly == 32767);
    js.lZ = -32768; DecodeDi(js, LAYOUT_XBOX, o); assert(o.rt == 255 && o.lt == 0);
    DecodeDi(js, LAYOUT_GENERIC, o); assert(o.raw == ((1u << 0) | (1u << 7)) && o.buttons == 0);
    // matching: two XInput-family pads, two slots; press A on device 1 and slot 1 within the window -> needs 2 matches
    int a = NewDev("\\\\?\\hid#vid_046d&pid_c21d&ig_00#1"); int b = NewDev("\\\\?\\hid#vid_045e&pid_028e&ig_00#2");
    int ab[2] = {a, b}; for (int i : ab) { g_d[i].pub.present = true; g_d[i].pub.apis = API_DINPUT; SetIdentity(g_d[i], 0, 0, "pad"); }
    assert(g_d[a].pub.layout == LAYOUT_XBOX && g_d[a].pub.vid == 0x046D);
    g_xConn[0] = g_xConn[1] = true;
    for (int round = 0; round < 3; ++round)   // f1.39 (B5): both pads pressed together with slot 1 -> ambiguous, nobody is tied
    {
        DWORD t = GetTickCount();
        g_d[a].edgeBtn = 0x1000; g_d[a].edgeT = t - 30; g_d[b].edgeBtn = 0x1000; g_d[b].edgeT = t - 20; g_xEdgeBtn[1] = 0x1000; g_xEdgeT[1] = t - 10;
        MatchXInput();
    }
    printf("together: a slot %d, b slot %d\n", g_d[a].pub.xinputSlot, g_d[b].pub.xinputSlot);
    assert(g_d[a].pub.xinputSlot == -1 && g_d[b].pub.xinputSlot == -1);
    g_d[a].edgeT = g_d[b].edgeT = 0; g_d[a].matchCount = g_d[b].matchCount = 0; g_d[a].matchSlot = g_d[b].matchSlot = -1;
    for (int round = 0; round < 2; ++round)
    {
        DWORD t = GetTickCount();
        g_d[b].edgeBtn = 0x1000; g_d[b].edgeT = t; g_xEdgeBtn[1] = 0x1000; g_xEdgeT[1] = t + 20;
        MatchXInput();
    }
    printf("b slot %d, a slot %d\n", g_d[b].pub.xinputSlot, g_d[a].pub.xinputSlot);
    assert(g_d[b].pub.xinputSlot == 1);
    MatchXInput();   // one free slot (0) + one free device (a), but no press yet -> not tied
    assert(g_d[a].pub.xinputSlot == -1);
    { DWORD t = GetTickCount(); g_d[a].edgeBtn = 0x2000; g_d[a].edgeT = t; g_xEdgeBtn[0] = 0x2000; g_xEdgeT[0] = t + 5; }
    MatchXInput();   // one matching press is enough for the only pair
    assert(g_d[a].pub.xinputSlot == 0);
    // f1.11: gamepad or not
    assert(LooksLikeGamepad(true, false, 4, 0x14, 0, 0));          // Xbox family
    assert(LooksLikeGamepad(false, true, 5, 0, 0, 0));             // PlayStation
    assert(LooksLikeGamepad(false, false, 5, 0, 0, 0));            // HID gamepad usage
    assert(LooksLikeGamepad(false, false, 4, 0x15, 12, 4));        // DirectInput gamepad
    assert(LooksLikeGamepad(false, false, 4, 0x14, 12, 4));        // cheap pad calling itself a joystick
    assert(!LooksLikeGamepad(false, false, 4, 0x14, 0, 3));        // pedals: axes, no buttons
    assert(!LooksLikeGamepad(false, false, 4, 0x16, 20, 4));       // wheel
    assert(!LooksLikeGamepad(false, false, 4, 0x17, 12, 4));       // flight stick
    assert(!LooksLikeGamepad(false, false, 4, 0x1C, 0, 3));        // supplemental (pedals)
    assert(!LooksLikeGamepad(false, false, 4, 0, 0, 0));           // HID joystick, no DirectInput info yet
    printf("ALL OK\n");
    return 0;
}
