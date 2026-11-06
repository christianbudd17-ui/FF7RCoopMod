// f1.154: the monitors layout maths (pip_hud/30c MonitorRegions) - which part of the canvas each player gets.
// Build: x86_64-w64-mingw32-g++-posix -O1 -static -o t.exe tools/tests/unit_monitors.cpp ; WINEDEBUG=-all wine t.exe
// (plain g++ works too - the function is pure maths)
#define COOP_UNIT_TEST 1
#include <cstdio>
#include "../../pip_hud/30c_monitors.inc"
static int fails = 0;
static void Exp(const char* what, float r[4], float x, float y, float w, float h)
{
    bool ok = r[0] == x && r[1] == y && r[2] == w && r[3] == h;
    if (!ok) { ++fails; printf("FAIL %s: got %.0f,%.0f %.0fx%.0f want %.0f,%.0f %.0fx%.0f\n", what, r[0], r[1], r[2], r[3], x, y, w, h); }
}
int main()
{
    float o[3][4];
    {   // 1: two 1080p monitors, main on the left, window over both, canvas = window
        MonRect m[2] = {{0, 0, 1920, 1080, true}, {1920, 0, 1920, 1080, false}};
        int u = MonitorRegions(m, 2, 0, 0, 3840, 1080, 3840, 1080, 0, true, true, o);
        if (u != 2) { ++fails; printf("FAIL 1 used %d\n", u); }
        Exp("1 P1", o[0], 0, 0, 1920, 1080); Exp("1 P2 columns", o[1], 1920, 0, 960, 1080); Exp("1 P3 columns", o[2], 2880, 0, 960, 1080);
        MonitorRegions(m, 2, 0, 0, 3840, 1080, 3840, 1080, 1, true, true, o);
        Exp("1 P2 rows", o[1], 1920, 0, 1920, 540); Exp("1 P3 rows", o[2], 1920, 540, 1920, 540);
        MonitorRegions(m, 2, 0, 0, 3840, 1080, 3840, 1080, 0, true, false, o);
        Exp("1 P2 alone", o[1], 1920, 0, 1920, 1080); Exp("1 P3 off", o[2], 0, 0, 0, 0);
        MonitorRegions(m, 2, 0, 0, 3840, 1080, 3840, 1080, 0, false, true, o);
        Exp("1 P3 alone", o[2], 1920, 0, 1920, 1080); Exp("1 P2 off", o[1], 0, 0, 0, 0);
    }
    {   // 2: main monitor on the RIGHT (the left one at -1920)
        MonRect m[2] = {{-1920, 0, 1920, 1080, false}, {0, 0, 1920, 1080, true}};
        MonitorRegions(m, 2, -1920, 0, 3840, 1080, 3840, 1080, 0, true, true, o);
        Exp("2 P1", o[0], 1920, 0, 1920, 1080); Exp("2 P2", o[1], 0, 0, 960, 1080); Exp("2 P3", o[2], 960, 0, 960, 1080);
    }
    {   // 3: three monitors, main in the middle
        MonRect m[3] = {{0, 0, 1920, 1080, true}, {1920, 0, 1920, 1080, false}, {-1920, 0, 1920, 1080, false}};
        int u = MonitorRegions(m, 3, -1920, 0, 5760, 1080, 5760, 1080, 0, true, true, o);
        if (u != 3) { ++fails; printf("FAIL 3 used %d\n", u); }
        Exp("3 P1 middle", o[0], 1920, 0, 1920, 1080); Exp("3 P2 left", o[1], 0, 0, 1920, 1080); Exp("3 P3 right", o[2], 3840, 0, 1920, 1080);
        MonitorRegions(m, 3, -1920, 0, 5760, 1080, 5760, 1080, 0, true, false, o);
        Exp("3 P3 off", o[2], 0, 0, 0, 0); Exp("3 P2 still left", o[1], 0, 0, 1920, 1080);
    }
    {   // 4: the game renders at a smaller canvas (render scale): regions scale with it
        MonRect m[2] = {{0, 0, 1920, 1080, true}, {1920, 0, 1920, 1080, false}};
        MonitorRegions(m, 2, 0, 0, 3840, 1080, 2560, 720, 0, true, false, o);
        Exp("4 P1 scaled", o[0], 0, 0, 1280, 720); Exp("4 P2 scaled", o[1], 1280, 0, 1280, 720);
    }
    {   // 5: the window on one monitor only -> no monitors layout
        MonRect m[2] = {{0, 0, 1920, 1080, true}, {1920, 0, 1920, 1080, false}};
        int u = MonitorRegions(m, 2, 0, 0, 1920, 1080, 1920, 1080, 0, true, true, o);
        if (u != 1) { ++fails; printf("FAIL 5 used %d (want 1)\n", u); }
    }
    {   // 6: main monitor not covered by the window (window over the two others) -> the leftmost covered is player 1's
        MonRect m[3] = {{0, 0, 1920, 1080, true}, {1920, 0, 1920, 1080, false}, {3840, 0, 1920, 1080, false}};
        MonitorRegions(m, 3, 1920, 0, 3840, 1080, 3840, 1080, 0, true, true, o);
        Exp("6 P1 leftmost", o[0], 0, 0, 1920, 1080); Exp("6 P2", o[1], 1920, 0, 960, 1080);
    }
    {   // 7: different heights (a 1440p main + a 1080p second, tops aligned): regions clipped to the drawing area
        MonRect m[2] = {{0, 0, 2560, 1440, true}, {2560, 0, 1920, 1080, false}};
        MonitorRegions(m, 2, 0, 0, 4480, 1440, 4480, 1440, 0, true, false, o);
        Exp("7 P1", o[0], 0, 0, 2560, 1440); Exp("7 P2", o[1], 2560, 0, 1920, 1080);
    }
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
