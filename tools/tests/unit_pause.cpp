// unit_pause.cpp (finding #34, f1.205) - checks core/02 OtherThreadsPaused: the other threads really stop while it is
// alive and run again afterwards; a thread whose next instruction is inside the given bytes is reported (clear = false).
// No game needed. Build: tools/test.sh.
#define COOP_MOD_VERSION "test"
#include "../../core/00_includes_xinput_types.inc"
#include "../../core/02_state_globals_log.inc"
#include <cassert>
static void LoadP3(bool) {}
static volatile LONG g_count[4];
static volatile bool g_stop = false;
static DWORD WINAPI Spin(void* p) { int i = (int)(intptr_t)p; while (!g_stop) InterlockedIncrement(&g_count[i]); return 0; }
// a thread that sits on a tight loop at a known address (the "patched bytes" test)
static volatile bool g_loopRun = true;
__attribute__((noinline)) static void TightLoop() { while (g_loopRun) {} }
static DWORD WINAPI Looper(void*) { TightLoop(); return 0; }
int main()
{
    HANDLE t[4];
    for (int i = 0; i < 4; ++i) t[i] = CreateThread(nullptr, 0, Spin, (void*)(intptr_t)i, 0, nullptr);
    Sleep(100);
    LONG before[4], after[4];
    int paused;
    {
        OtherThreadsPaused p(0, 0);
        paused = p.n;
        for (int i = 0; i < 4; ++i) before[i] = g_count[i];
        Sleep(100);
        for (int i = 0; i < 4; ++i) after[i] = g_count[i];
        assert(p.clear);
    }
    printf("paused %d threads; counts while paused: %ld->%ld %ld->%ld %ld->%ld %ld->%ld\n", paused, before[0], after[0], before[1], after[1], before[2], after[2], before[3], after[3]);
    assert(paused >= 4);
    for (int i = 0; i < 4; ++i) assert(before[i] == after[i]);          // nothing ran while paused
    LONG c0 = g_count[0]; Sleep(100); assert(g_count[0] != c0);          // runs again afterwards
    // a thread inside the bytes: TightLoop's whole body (a loop never leaves it) - must come back "not clear"
    HANDLE lt = CreateThread(nullptr, 0, Looper, nullptr, 0, nullptr); Sleep(50);
    {
        OtherThreadsPaused p((uintptr_t)&TightLoop, 64);
        printf("thread inside the bytes: clear = %d (want 0)\n", (int)p.clear);
        assert(!p.clear);
    }
    g_loopRun = false; WaitForSingleObject(lt, 2000);
    g_stop = true; WaitForMultipleObjects(4, t, TRUE, 2000);
    printf("ALL OK\n");
    return 0;
}
