// ---- tiny inline-hook helper: 14-byte absolute jump, prologue copied to a trampoline ----
// The copied first instructions must not depend on where they run (no RIP-relative address, no relative jump / call):
// each caller's byte pattern was checked for that by hand (register saves / stack setup) - finding #36.
// f1.337 (pre-release review): outOrig (optional) gets the trampoline BEFORE the jump is written - a hook another thread can
// enter (the PlayStation HID reader thread, ProcessEvent) must never see its "original" pointer still null right after the
// other threads resume (before, the caller stored it only after this returned - after a VirtualProtect and a log line)
static void* MakeInlineHook(uintptr_t target, void* hook, size_t stealLen, const unsigned char* expect, const char* name, void** outOrig)
{
    if (memcmp((void*)target, expect, stealLen) != 0)
    {
        CoopLog("HOOK FAILED %s: bytes at target don't match this game version - not hooking", name);
        ++g_HooksFailed;
        return nullptr;
    }
    unsigned char* tramp = (unsigned char*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) { CoopLog("HOOK FAILED %s: VirtualAlloc", name); ++g_HooksFailed; return nullptr; }
    memcpy(tramp, (void*)target, stealLen);
    unsigned char* j = tramp + stealLen;
    j[0] = 0xFF; j[1] = 0x25; *(DWORD*)(j + 2) = 0; *(uint64_t*)(j + 6) = target + stealLen;

    DWORD old;
    if (!VirtualProtect((void*)target, stealLen, PAGE_EXECUTE_READWRITE, &old)) { CoopLog("HOOK FAILED %s: VirtualProtect", name); VirtualFree(tramp, 0, MEM_RELEASE); ++g_HooksFailed; return nullptr; }   // (finding #38: the trampoline is freed)
    unsigned char patch[32];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xFF; patch[1] = 0x25; *(DWORD*)(patch + 2) = 0; *(uint64_t*)(patch + 6) = (uint64_t)hook;
    if (outOrig) { *(void* volatile*)outOrig = tramp; MemoryBarrier(); }   // f1.337: published before the hook goes live
    bool clear; int paused, notPaused;
    {   // (finding #34) the other threads stay paused only for the write - NO log line / allocation in this block
        OtherThreadsPaused p(target, stealLen);
        clear = p.clear; paused = p.n; notPaused = p.notPaused;
        memcpy((void*)target, patch, stealLen);
        FlushInstructionCache(GetCurrentProcess(), (void*)target, stealLen);
    }
    VirtualProtect((void*)target, stealLen, old, &old);
    if (!clear) CoopLog("HOOK NOTE %s: a game thread was inside its first bytes 20 times - written anyway (as before f1.205)", name);
    if (notPaused) CoopLog("HOOK NOTE %s: %d game thread(s) couldn't be paused while it was written", name, notPaused);
    CoopLog("Hooked %s (%d other threads paused while writing it)", name, paused);
    return tramp;
}


// =================================================================================
// Weak object references (GUObjectArray index + serial)
// =================================================================================

static bool MakeWeak(void* obj, int* outIndex, int* outSerial)
{
    if (!obj) return false;
    int idx = *(int*)((unsigned char*)obj + 0xC);
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    if (!items || idx < 0 || idx >= num) return false;
    unsigned char* item = items + (size_t)idx * 24;
    if (*(void**)item != obj) return false;
    volatile LONG* ser = (volatile LONG*)(item + 0x10);
    LONG serial = *ser;
    if (serial == 0)
    {
        // no serial yet (object just created). Assign one from a private range far above the
        // engine's own counter; if the engine got there first, use its value.
        static volatile LONG s_next = 0x7E000000;
        LONG mine = InterlockedIncrement(&s_next);
        LONG prev = InterlockedCompareExchange(ser, mine, 0);
        serial = prev ? prev : mine;
    }
    *outIndex = idx; *outSerial = (int)serial;
    return true;
}




// Weak reference -> object (null when the object is gone or being destroyed)

static void* ResolveWeak(int idx, int serial)
{
    if (serial == 0 || idx < 0) return nullptr;
    int num = *(int*)(g_Base + RVA_GOBJ_NUM);
    if (idx >= num) return nullptr;
    unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS);
    if (!items) return nullptr;
    unsigned char* item = items + (size_t)idx * 24;
    if (*(int*)(item + 0x10) != serial) return nullptr;
    if (*(DWORD*)(item + 8) & 0x30000000) return nullptr;
    return *(void**)item;
}



