// Test for the new input modules - build with mingw (see the command in README_FORK1.txt) and run under wine or Windows.
// No game needed. unit_in2 #includes in2.cpp to check its decoding / matching internals; smoke_in2 starts the module.
// host-side smoke test for input2 (runs under wine): start, let it scan/poll, query, exercise seats + output
#include "../../input2/in2.h"
#include "../../input2/seats.h"
#include "../../input2/output.h"
#include <cstdio>
#include <cstdarg>
static void Log(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); fflush(stdout); }
typedef DWORD (WINAPI* XGS)(DWORD, void*);
static HWND g_w;
static HWND Win() { return g_w; }
int main()
{
    g_w = CreateWindowW(L"STATIC", L"t", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
    HMODULE x = LoadLibraryW(L"xinput1_3.dll");
    in2::Host h; h.log = Log; h.xGetState = x ? (XGS)(void*)GetProcAddress(x, "XInputGetState") : nullptr; h.diOrigGetState = nullptr; h.gameWindow = Win;
    printf("xinput %p\n", (void*)h.xGetState);
    in2::seats::SetLogger(Log);
    if (!in2::Start(h)) return 1;
    in2::output::Start(in2::output::CHANNEL_NONE, Log, 0);
    in2::seats::Want w;
    const char* tests[] = {"Keyboard", "none", "Auto", "054C:0CE6", "054c:0ce6#2", "path:\\\\?\\hid#vid_054c", "zz", "054C:0CE6#0", "12:34#1x", ""};
    for (const char* t : tests) { bool ok = in2::seats::ParseWant(t, &w); printf("parse '%s' -> %d kind %d %04X:%04X #%d path '%s'\n", t, ok, ok ? w.kind : -1, w.vid, w.pid, w.nth, w.path); }
    in2::seats::ParseWant("Keyboard", &w); in2::seats::SetWant(0, w);
    in2::seats::ParseWant("Auto", &w); in2::seats::SetWant(1, w);
    in2::seats::ParseWant("054C:0CE6", &w); in2::seats::SetWant(2, w);
    for (int k = 0; k < 30; ++k) { in2::seats::Update(); Sleep(100); }
    in2::seats::Trade(0, 2); in2::seats::Update();
    printf("count %d\n", in2::Count());
    printf("done\n");
    return 0;
}
