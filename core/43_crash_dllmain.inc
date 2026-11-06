// crash reporter. A fatal exception writes where it happened (module + offset, a few return
// addresses found on the stack, the last breadcrumb) to the log, then lets the crash continue as usual.
static void CrumbAddr(uintptr_t a, char* out, size_t n)
{
    HMODULE m = nullptr; wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &m) && m)
    {
        GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* b = wcsrchr(path, L'\\'); b = b ? b + 1 : path;
        snprintf(out, n, "%ls+0x%llX", b, (unsigned long long)(a - (uintptr_t)m));
    }
    else snprintf(out, n, "%p", (void*)a);
}
// a loaded module's size in memory, from its own PE header (finding #37 - were fixed guesses: 2 MB for the mod, 96 MB
// for the game)
static uintptr_t ModuleImageSize(uintptr_t base)
{
    if (!base) return 0;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}
static LONG CALLBACK CrashVeh(EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000005 && code != 0xC000001D && code != 0xC0000094 && code != 0xC00000FD && code != 0xC0000409 && code != 0xC0000374) return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG n = 0; if (InterlockedIncrement(&n) > 6) return EXCEPTION_CONTINUE_SEARCH;
    char where[300]; CrumbAddr((uintptr_t)ep->ExceptionRecord->ExceptionAddress, where, sizeof(where));
    unsigned long long acc = ep->ExceptionRecord->NumberParameters >= 2 ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    HMODULE self = nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&CrashVeh, &self);
    uintptr_t ea = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    uintptr_t selfSize = ModuleImageSize((uintptr_t)self), exeSize = ModuleImageSize(g_Base);
    bool mine = self && ea >= (uintptr_t)self && ea < (uintptr_t)self + selfSize;
    // this handler sees errors before their owner does. Only errors inside the mod are called CRASH; others
    // (UE4SS faults on every quit) are often handled by the module itself and are not the mod's.
    const char* tag = mine ? "CRASH: in the co-op mod -" : (strstr(where, "UE4SS") ? "NOTE: error inside UE4SS (a modding tool, not part of the co-op mod) -" : "NOTE: error outside the mod (may be handled by its owner) -");
    CoopLog("%s exception 0x%08lX at %s (%s address %llX), thread %lu%s, last step: %s", tag, code, where,
        ep->ExceptionRecord->NumberParameters >= 1 && ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading", acc,
        GetCurrentThreadId(), GetCurrentThreadId() == g_GameThreadId ? " (game thread)" : "", (const char*)g_Crumb);
    uintptr_t* sp = (uintptr_t*)ep->ContextRecord->Rsp; int found = 0; char line[900]; int lp = 0;
    for (int i = 0; i < 512 && found < 12; ++i)
    {
        if (!Readable(sp + i, 8)) break;
        uintptr_t v = sp[i];
        bool inExe = v >= g_Base && v < g_Base + exeSize, inSelf = self && v >= (uintptr_t)self && v < (uintptr_t)self + selfSize;
        if (!inExe && !inSelf) continue;
        char a[120]; CrumbAddr(v, a, sizeof(a)); lp += snprintf(line + lp, sizeof(line) - lp, " %s", a); ++found;
    }
    line[lp] = 0; if (!lp) line[0] = 0;
    CoopLog("%s return addresses on the stack:%s", mine ? "CRASH:" : "NOTE:", line);
    return EXCEPTION_CONTINUE_SEARCH;
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        AddVectoredExceptionHandler(1, CrashVeh);   // crash reporter (only logs)
        // Deliberately NOT calling LoadRealXInput() here. Windows holds a global loader
        // lock while DllMain(DLL_PROCESS_ATTACH) runs, and calling LoadLibrary this early
        // (which is what LoadRealXInput() does) is a well-documented way to corrupt or
        // crash the process - this is what caused the access-violation crash on launch.
        // Every exported function below already calls LoadRealXInput() itself on first
        // use instead, which happens safely later, once the game is actually running.
    }
    return TRUE;
}
