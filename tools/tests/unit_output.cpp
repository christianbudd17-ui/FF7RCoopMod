// Test for input2/output.cpp (no game needed): a fake FEndDirectInput vtable + mode flag at the RVAs the module
// patches; checks the slot check, the patch, refusals and the Tick-time deselect. Build like unit_in2.cpp.
#include "../../input2/output.cpp"
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <vector>
using namespace in2::output;
static void Log(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); }
static int ticks = 0, deselects = 0;
static void FakeTick(void* self, float dt) { (void)self; assert(dt == 0.5f); ++ticks; }
static void FakeDeselect(void* self) { *(int*)((unsigned char*)self + 0x28) = 0; ++deselects; }
int main()
{
    std::vector<unsigned char> mem(0xC00000, 0);
    uintptr_t base = (uintptr_t)mem.data() - RVA_EDI_VTABLE + 0x1000;   // vtable at mem+0x1000
    uintptr_t* tbl = (uintptr_t*)(base + RVA_EDI_VTABLE);
    assert((unsigned char*)(base + RVA_DI_MODE) < mem.data() + mem.size());
    tbl[SLOT_TICK] = base + FN_TICK; tbl[SLOT_SELECT] = base + FN_SELECT; tbl[SLOT_SELECT_PRODUCT] = base + FN_SELECT_PRODUCT; tbl[SLOT_DESELECT] = base + FN_DESELECT;
    *(unsigned char*)(base + RVA_DI_MODE) = 1;   // the game picked a DirectInput controller before the mod started
    Start(CHANNEL_XINPUT, Log, base);
    assert(Active() == CHANNEL_XINPUT && GameInDirectInputMode());
    g_origTick = FakeTick; g_deselect = FakeDeselect;   // stand-ins for the game's functions
    typedef bool (*Sel_t)(void*, const void*, const void*); typedef bool (*SelP_t)(void*, const void*); typedef void (*T_t)(void*, float);
    unsigned char obj[0x100] = {}; *(int*)(obj + 0x28) = 1;   // one selected controller
    ((T_t)tbl[SLOT_TICK])(obj, 0.5f);
    assert(deselects == 1 && ticks == 1 && !GameInDirectInputMode() && *(int*)(obj + 0x28) == 0);
    ((T_t)tbl[SLOT_TICK])(obj, 0.5f);
    assert(deselects == 1 && ticks == 2);
    assert(((Sel_t)tbl[SLOT_SELECT])(obj, nullptr, nullptr) == false);
    assert(((SelP_t)tbl[SLOT_SELECT_PRODUCT])(obj, nullptr) == false);
    assert(tbl[SLOT_DESELECT] == base + FN_DESELECT);   // untouched
    printf("patched OK\n");
    // version mismatch: nothing patched
    g_started = false; g_ch = CHANNEL_NONE;
    tbl[SLOT_TICK] = base + FN_TICK; tbl[SLOT_SELECT] = base + 0x1234; tbl[SLOT_SELECT_PRODUCT] = base + FN_SELECT_PRODUCT;
    Start(CHANNEL_XINPUT, Log, base);
    assert(Active() == CHANNEL_NONE && tbl[SLOT_TICK] == base + FN_TICK && tbl[SLOT_SELECT_PRODUCT] == base + FN_SELECT_PRODUCT);
    printf("ALL OK\n");
    return 0;
}
