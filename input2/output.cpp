// input2/output.cpp - the new leader-output module. See output.h for the RE background.
#include "output.h"
#include <cstring>

namespace in2 {
namespace output {
namespace {

// FEndDirectInput (the game's DirectInput IInputDevice, module "EndDirectInput")
constexpr uintptr_t RVA_EDI_VTABLE = 0x4C29EB0;
constexpr int SLOT_TICK = 1, SLOT_SELECT = 17, SLOT_SELECT_PRODUCT = 18, SLOT_DESELECT = 19;
constexpr uintptr_t FN_TICK = 0x16E9F90;            // IInputDevice::Tick(float)
constexpr uintptr_t FN_SELECT = 0x16EA980;          // bool Select(const FGuid* instance, const FGuid* product)
constexpr uintptr_t FN_SELECT_PRODUCT = 0x16EAC70;  // bool SelectProduct(const FGuid* product) - every pad of that product
constexpr uintptr_t FN_DESELECT = 0x16EAF60;        // void DeselectAll()
constexpr size_t    OFF_SELECTED_NUM = 0x28;        // TArray<TSharedPtr<device>> at +0x20: the selected controllers
constexpr uintptr_t RVA_DI_MODE = 0x57E9A0C;        // bool: the game reads gamepads through DirectInput (else XInput)

typedef void (*Tick_t)(void* self, float dt);
typedef void (*Deselect_t)(void* self);

Channel   g_ch = CHANNEL_NONE;
bool      g_started = false;
LogFn     g_log = nullptr;
uintptr_t g_base = 0;
Tick_t    g_origTick = nullptr;
Deselect_t g_deselect = nullptr;
volatile LONG g_refusals = 0, g_undone = 0;

void LogRefusal(const char* what, void* ret)
{
    LONG n = InterlockedIncrement(&g_refusals);
    if (n <= 6 && g_log)
        g_log("INPUT2 OUTPUT: the game tried to pick a DirectInput controller (%s, from exe+0x%llX) - refused, it stays in XInput mode",
              what, (unsigned long long)((uintptr_t)ret - g_base));
}

bool Hook_Select(void* self, const void* instance, const void* product)
{
    (void)self; (void)instance; (void)product;
    LogRefusal("Select", __builtin_return_address(0));
    return false;
}

bool Hook_SelectProduct(void* self, const void* product)
{
    (void)self; (void)product;
    LogRefusal("SelectProduct", __builtin_return_address(0));
    return false;
}

void Hook_Tick(void* self, float dt)
{
    // a controller picked before the mod started (game settings load) - undo it the way the game does
    if (self && *(int*)((unsigned char*)self + OFF_SELECTED_NUM) > 0)
    {
        g_deselect(self);
        *(volatile unsigned char*)(g_base + RVA_DI_MODE) = 0;
        LONG n = InterlockedIncrement(&g_undone);
        if (n <= 6 && g_log) g_log("INPUT2 OUTPUT: the game had a DirectInput controller selected - deselected, back to XInput mode");
    }
    g_origTick(self, dt);
}

bool PatchSlots(uintptr_t vt, const int* slots, const uintptr_t* expect, void* const* hooks, int n)
{
    uintptr_t* tbl = (uintptr_t*)(g_base + vt);
    for (int i = 0; i < n; ++i)
        if (tbl[slots[i]] != g_base + expect[i])
        {
            if (g_log) g_log("INPUT2 OUTPUT: FEndDirectInput slot %d holds exe+0x%llX, expected exe+0x%llX - game version differs, not patched",
                             slots[i], (unsigned long long)(tbl[slots[i]] - g_base), (unsigned long long)expect[i]);
            return false;
        }
    DWORD old = 0;
    uintptr_t lo = (uintptr_t)&tbl[0], hi = (uintptr_t)&tbl[SLOT_DESELECT + 1];
    if (!VirtualProtect((void*)lo, hi - lo, PAGE_READWRITE, &old)) { if (g_log) g_log("INPUT2 OUTPUT: VirtualProtect failed - not patched"); return false; }
    for (int i = 0; i < n; ++i) tbl[slots[i]] = (uintptr_t)hooks[i];
    VirtualProtect((void*)lo, hi - lo, old, &old);
    return true;
}

}   // namespace

const char* ChannelName(Channel ch)
{
    switch (ch)
    {
    case CHANNEL_XINPUT: return "XInput only (the game's DirectInput mode is blocked; the leader is XInput slot 0)";
    default: return "unchanged (the game picks XInput or DirectInput itself)";
    }
}

bool Start(Channel ch, LogFn log, uintptr_t exeBase)
{
    if (g_started) return true;
    g_started = true; g_log = log; g_base = exeBase;
    g_ch = CHANNEL_NONE;
    if (ch == CHANNEL_XINPUT && g_base)
    {
        static const int slots[3] = {SLOT_TICK, SLOT_SELECT, SLOT_SELECT_PRODUCT};
        static const uintptr_t expect[3] = {FN_TICK, FN_SELECT, FN_SELECT_PRODUCT};
        uintptr_t* tbl = (uintptr_t*)(g_base + RVA_EDI_VTABLE);
        if (tbl[SLOT_DESELECT] != g_base + FN_DESELECT)
        {
            if (log) log("INPUT2 OUTPUT: FEndDirectInput Deselect slot doesn't match - game version differs, not patched");
        }
        else
        {
            g_origTick = (Tick_t)(g_base + FN_TICK);
            g_deselect = (Deselect_t)(g_base + FN_DESELECT);
            void* const hooks[3] = {(void*)&Hook_Tick, (void*)&Hook_Select, (void*)&Hook_SelectProduct};
            if (PatchSlots(RVA_EDI_VTABLE, slots, expect, hooks, 3)) g_ch = CHANNEL_XINPUT;
        }
    }
    if (log)
    {
        log("INPUT2 OUTPUT: game controller input: %s", ChannelName(g_ch));
        if (g_base) log("INPUT2 OUTPUT: the game is in %s mode right now", GameInDirectInputMode() ? "DirectInput" : "XInput");
    }
    return true;
}

Channel Active() { return g_ch; }

bool GameInDirectInputMode() { return g_base && *(volatile unsigned char*)(g_base + RVA_DI_MODE) != 0; }

}   // namespace output
}   // namespace in2
