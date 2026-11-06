// Test for input2/seats.cpp (f1.11): Auto takes only gamepads, fixed picks are resolved before Auto seats, the host
// veto, keeping a controller by identity, autoWaiting. No game needed. Build: tools/test.sh (-DIN2_TEST_HOOKS, linked
// with input2/in2*.cpp + seats.cpp). (review #20): the made-up controllers come through in2.h's test hooks
// (TestAddController / TestSetPresent) - the test no longer reaches inside in2.cpp.
#include "../../input2/in2.h"
#include "../../input2/seats.h"
#include <cassert>
#include <cstdarg>
using namespace in2;
static void Log(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); }
static int Add(const char* path, uint16_t vid, uint16_t pid, bool gamepad) { return TestAddController(path, vid, pid, gamepad); }   // its id
static void Plug(int id, bool present) { assert(TestSetPresent(id, present)); }
static seats::Seat S(int s) { seats::Seat t; seats::Get(s, &t); return t; }
int main()
{
    TestStart(Log);
    seats::SetLogger(Log);
    int pedal = Add("\\\\?\\hid#vid_0eb7&pid_183b#1", 0x0EB7, 0x183B, false);   // first in the list, not a gamepad
    int ds = Add("\\\\?\\hid#vid_054c&pid_0ce6#2", 0x054C, 0x0CE6, true);
    int gc = Add("\\\\?\\hid#vid_0d22&pid_0c34#3", 0x0D22, 0x0C34, true);
    seats::Want w;
    // seat 1 Auto, seat 2 names the DualSense (the first gamepad in the list), seat 3 Auto
    assert(seats::ParseWant("Auto", &w)); seats::SetWant(0, w);
    assert(seats::ParseWant("054C:0CE6", &w)); seats::SetWant(1, w);
    assert(seats::ParseWant("Auto", &w)); seats::SetWant(2, w);
    seats::Update();
    printf("seat1 #%d seat2 #%d seat3 %s\n", S(0).deviceId, S(1).deviceId, S(2).desc);
    assert(S(1).deviceId == ds);          // the named controller goes to its seat even though Auto comes first
    assert(S(0).deviceId == gc);          // Auto: never the pedals, never seat 2's DualSense
    assert(S(2).kind == seats::SEAT_NONE && S(2).waiting && S(2).autoWaiting);   // nothing left
    // unplug the GC30: seat 1 waits for it (not Auto any more - it keeps that controller), seat 3 doesn't take it later
    Plug(gc, false); seats::Update();
    assert(S(0).waiting && !S(0).autoWaiting);
    int x = Add("\\\\?\\hid#vid_045e&pid_028e&ig_00#4", 0x045E, 0x028E, true); seats::Update();
    assert(S(2).deviceId == x);           // a new pad: the waiting Auto seat 3 takes it
    Plug(gc, true); seats::Update();
    assert(S(0).deviceId == gc);          // back by identity
    // (f1.57: the host-veto test went with SetAutoFilter)
    seats::Want n; memset(&n, 0, sizeof(n)); seats::SetWant(1, n); seats::SetWant(2, n); seats::Update();
    int y = Add("\\\\?\\hid#vid_046d&pid_c21d&ig_00#5", 0x046D, 0xC21D, true);
    (void)y;
    // f1.28 (code review B1): seat 1's GC30 re-plugged into ANOTHER USB port (new path, same model) - seat 1 takes it back,
    // a waiting Auto seat does not grab it first
    {
        seats::Want z; memset(&z, 0, sizeof(z)); seats::SetWant(1, z);
        Plug(x, false); Plug(ds, false); Plug(y, false);   // no free pad: seat 3 waits on Auto
        assert(seats::ParseWant("Auto", &w)); seats::SetWant(2, w); seats::Update();
        assert(S(2).autoWaiting);
        printf("before: seat1 #%d seat3 %s\n", S(0).deviceId, S(2).desc);
        Plug(gc, false); seats::Update();
        assert(S(0).waiting);
        int gc2 = Add("\\\\?\\hid#vid_0d22&pid_0c34#7", 0x0D22, 0x0C34, true); seats::Update();
        printf("after re-plug: seat1 #%d (new #%d) seat3 %s\n", S(0).deviceId, gc2, S(2).desc);
        assert(S(0).deviceId == gc2);         // seat 1 took its model back on the new port
        assert(S(2).deviceId != gc2);         // Auto seat 3 didn't grab it
        // the old port comes back too (a second GC30): seat 1 keeps the new one, the old path is free for Auto
        Plug(gc, true); seats::Update();
        assert(S(0).deviceId == gc2 && S(2).deviceId == gc);   // the old-port GC30 is free -> Auto seat 3
        printf("old port back: seat1 #%d seat3 #%d\n", S(0).deviceId, S(2).deviceId);
        // a seat never takes a same-model pad another seat keeps by path
        seats::Want t; memset(&t, 0, sizeof(t)); seats::SetWant(2, t); seats::Update();
        assert(seats::TieToDevice(2, gc)); seats::Update();
        Plug(gc2, false); seats::Update();
        assert(S(0).waiting && S(2).deviceId == gc);   // seat 1 waits, doesn't steal seat 3's GC30
    }
    printf("ALL OK\n");
    return 0;
}
