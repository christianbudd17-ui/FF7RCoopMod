// ---------------------------------------------------------------------------------
// DIRECTINPUT. Besides XInput (which this DLL controls), the game reads controllers through
// DirectInput (it loads dinput8.dll itself, see 0x16EFE00). Every XInput controller also shows up
// there, so hiding a controller from XInput isn't enough: player 1's pad reached player 2's menu,
// and an earlier test had one pad moving both characters. The device vtable in dinput8.dll is
// shared by every device, so patching GetDeviceState / GetDeviceData once covers the game's devices.
//  - v13.6: every DirectInput controller the game opens reads idle (the leader's controller reaches the game as
//    XInput slot 0; with GameInput=XInput the game doesn't open any - this is the safety net)
// ---------------------------------------------------------------------------------
struct DiPropHdr { DWORD dwSize, dwHeaderSize, dwObj, dwHow; };
struct DiPropRange { DiPropHdr h; LONG lMin, lMax; };
struct DiInstW { DWORD dwSize; GUID gInst, gProd; DWORD dwDevType; WCHAR inst[260]; WCHAR prod[260]; GUID gFF; WORD up, u; };
struct DiInstA { DWORD dwSize; GUID gInst, gProd; DWORD dwDevType; CHAR inst[260]; CHAR prod[260]; GUID gFF; WORD up, u; };
typedef HRESULT (WINAPI* DI8Create_t)(HINSTANCE, DWORD, const GUID&, void**, void*);
typedef HRESULT (STDMETHODCALLTYPE* DiGetState_t)(void*, DWORD, void*);
typedef HRESULT (STDMETHODCALLTYPE* DiGetData_t)(void*, DWORD, void*, DWORD*, DWORD);
typedef HRESULT (STDMETHODCALLTYPE* DiGetProp_t)(void*, const GUID*, DiPropHdr*);
typedef HRESULT (STDMETHODCALLTYPE* DiGetInfo_t)(void*, void*);
typedef HRESULT (STDMETHODCALLTYPE* DiCreateDev_t)(void*, const GUID&, void**, void*);
typedef ULONG   (STDMETHODCALLTYPE* DiRelease_t)(void*);
// originals per device table - controller devices may not share the keyboard's table
struct DiVt { void** vt; DiGetState_t st; DiGetData_t dt; bool wide; };
static DiVt g_DiVt[8]; static int g_DiVtN = 0;
static const DiVt* DiVtOf(void* self)
{
    void** vt = *(void***)self;
    for (int i = 0; i < g_DiVtN; ++i) if (g_DiVt[i].vt == vt) return &g_DiVt[i];
    return nullptr;
}

struct DiDev
{
    void* p; bool wide; int kind; GUID gi;   // kind 1 = controller, 2 = something else (keyboard, mouse)
    char name[64];
    LONG center[8]; bool centerOk;
    unsigned char rest[272]; DWORD restCb;
    bool openLogged;
};
static DiDev g_DiDev[16];
static int g_DiDevN = 0;
static CRITICAL_SECTION g_DiLock;
static bool g_DiLockInit = false;
// (finding #39) set when co-op switches itself off (core/41: game version differs / a must-have hook failed): then every
// DirectInput controller reads as it really is again - a player with only a generic DirectInput pad keeps it, as if the
// mod weren't there. (The device hooks go in before that check, so the filters below must ask.)
static volatile bool g_CoopOff = false;

static DiDev* DiFind(void* self, bool wide)
{
    for (int i = 0; i < g_DiDevN; ++i) if (g_DiDev[i].p == self) return &g_DiDev[i];
    if (g_DiDevN >= 16) return nullptr;
    DiDev* d = &g_DiDev[g_DiDevN++]; memset(d, 0, sizeof(*d)); d->p = self; d->wide = wide; d->kind = 2;
    void** vt = *(void***)self; DWORD type = 0;
    if (wide) { DiInstW in; memset(&in, 0, sizeof(in)); in.dwSize = sizeof(in);
        if (((DiGetInfo_t)vt[15])(self, &in) >= 0) { type = in.dwDevType; d->gi = in.gInst; WideCharToMultiByte(CP_UTF8, 0, in.prod, -1, d->name, sizeof(d->name) - 1, nullptr, nullptr); } }
    else { DiInstA in; memset(&in, 0, sizeof(in)); in.dwSize = sizeof(in);
        if (((DiGetInfo_t)vt[15])(self, &in) >= 0) { type = in.dwDevType; d->gi = in.gInst; snprintf(d->name, sizeof(d->name), "%s", in.prod);   /* (finding #3: was strncpy - same result, no warning) */ } }
    BYTE t = (BYTE)(type & 0xFF);
    if (t >= 0x14 && t <= 0x18) d->kind = 1;   // joystick, gamepad, driving, flight, first person
    if (!d->name[0]) strcpy(d->name, "?");
    CoopLog("DINPUT: the game reads device '%s' (type 0x%02X, %s)", d->name, (unsigned)t, d->kind == 1 ? "controller" : "not a controller - untouched");
    return d;
}

static void DiCenters(DiDev* d)
{
    if (d->centerOk) return;
    d->centerOk = true;
    void** vt = *(void***)d->p;
    for (int i = 0; i < 8; ++i)
    {
        DiPropRange r; memset(&r, 0, sizeof(r));
        r.h.dwSize = sizeof(r); r.h.dwHeaderSize = sizeof(DiPropHdr); r.h.dwObj = (DWORD)(i * 4); r.h.dwHow = 1; // DIPH_BYOFFSET
        if (((DiGetProp_t)vt[5])(d->p, (const GUID*)(uintptr_t)4, &r.h) >= 0 && r.lMax > r.lMin) d->center[i] = (LONG)(((long long)r.lMin + r.lMax) / 2);
        else d->center[i] = 32767;   // DirectInput's default 0..65535
    }
}

// the v8.x-v12.1 press-timing match of the game's DirectInput devices against the players' XInput presses
// (DiPollX / DiTrack / DiSeatsTraded: "which of the game's devices is player N's controller") was removed - every
// controller the game opens through DirectInput is idle for it anyway (the leader reaches it as XInput slot 0).
static void DiNeutral(DiDev* d, DWORD cb, void* data)
{
    unsigned char* b = (unsigned char*)data;
    if (cb == 80 || cb == 272)   // DIJOYSTATE / DIJOYSTATE2
    {
        DiCenters(d);
        LONG* ax = (LONG*)b; for (int i = 0; i < 8; ++i) ax[i] = d->center[i];
        DWORD* pov = (DWORD*)(b + 32); for (int i = 0; i < 4; ++i) pov[i] = 0xFFFFFFFF;
        memset(b + 48, 0, cb - 48);
    }
    else if (d->restCb == cb) memcpy(b, d->rest, cb);
}

static HRESULT DiGetStateCommon(void* self, DWORD cb, void* data)
{
    const DiVt* v = DiVtOf(self); if (!v) return E_FAIL;
    bool wide = v->wide;
    HRESULT hr = v->st(self, cb, data);
    if (hr < 0 || !data || !g_DiLockInit || g_CoopOff) return hr;
    EnterCriticalSection(&g_DiLock);
    DiDev* d = DiFind(self, wide);
    if (d && d->kind == 1)
    {
        unsigned char* b = (unsigned char*)data;
        if (cb != 80 && cb != 272 && !d->restCb && cb <= sizeof(d->rest)) { memcpy(d->rest, b, cb); d->restCb = cb; CoopLog("DINPUT: '%s' uses an unusual data format (%lu bytes) - its first reading is used as 'idle'", d->name, cb); }
        // the v19.5-v19.8 pass-through of the leader's controller (and the v19.8 synthesised Xbox reading) was
        // removed - the game is kept in XInput mode (GameInput=XInput, the default) and gets the leader through XInput
        // slot 0, so no DirectInput controller the game reads carries anything to it.
        if (!d->openLogged)
        {   // no DirectInput controller reaches the game
            d->openLogged = true;
            CoopLog("DINPUT: the game opened '%s' - idle for the game (players' controllers are read by the mod and the leader's reaches the game as XInput slot 0)", d->name);
        }
        DiNeutral(d, cb, data);   // hidden from the game
    }
    LeaveCriticalSection(&g_DiLock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE Hook_DiState(void* s, DWORD cb, void* d) { return DiGetStateCommon(s, cb, d); }

static HRESULT DiGetDataCommon(void* self, DWORD cbObj, void* rg, DWORD* inOut, DWORD flags)
{
    const DiVt* v = DiVtOf(self); if (!v) return E_FAIL;
    bool wide = v->wide;
    HRESULT hr = v->dt(self, cbObj, rg, inOut, flags);
    if (hr < 0 || !rg || !inOut || !g_DiLockInit || g_CoopOff) return hr;
    EnterCriticalSection(&g_DiLock);
    DiDev* d = DiFind(self, wide);
    if (d && d->kind == 1) *inOut = 0;   // buffered events dropped (every controller - the game gets player 1's through XInput)
    LeaveCriticalSection(&g_DiLock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE Hook_DiData(void* s, DWORD c, void* r, DWORD* n, DWORD f) { return DiGetDataCommon(s, c, r, n, f); }

static bool DiPatchVt(void* dev, bool wide, const char* what)
{
    void** vt = *(void***)dev;
    for (int i = 0; i < g_DiVtN; ++i) if (g_DiVt[i].vt == vt) return true;   // already done
    if (g_DiVtN >= 8) return false;
    DiVt& e = g_DiVt[g_DiVtN];
    e.vt = vt; e.st = (DiGetState_t)vt[9]; e.dt = (DiGetData_t)vt[10]; e.wide = wide;
    ++g_DiVtN;   // entry first, so a call arriving right after the patch finds its original
    DWORD old; VirtualProtect(&vt[9], 2 * sizeof(void*), PAGE_READWRITE, &old);
    vt[9] = (void*)Hook_DiState; vt[10] = (void*)Hook_DiData;
    VirtualProtect(&vt[9], 2 * sizeof(void*), old, &old);
    CoopLog("DINPUT: patched device table %p (%s, %s)", (void*)vt, wide ? "W" : "A", what);
    return true;
}
struct DiGuidList { GUID g[8]; char n[8][64]; int c; };
static BOOL CALLBACK DiCollectCb(LPCDIDEVICEINSTANCEW d, LPVOID ctx)
{
    DiGuidList* l = (DiGuidList*)ctx; if (l->c >= 8) return DIENUM_STOP;
    l->g[l->c] = d->guidInstance; WideCharToMultiByte(CP_UTF8, 0, d->tszProductName, -1, l->n[l->c], 63, nullptr, nullptr); l->n[l->c][63] = 0;
    ++l->c; return DIENUM_CONTINUE;
}

static DiGuidList g_DiCtrl;
static void InstallDInputHooks()
{
    InitializeCriticalSection(&g_DiLock);
    wchar_t path[MAX_PATH]; GetSystemDirectoryW(path, MAX_PATH); wcscat(path, L"\\dinput8.dll");
    HMODULE m = LoadLibraryW(path);
    DI8Create_t create = m ? (DI8Create_t)GetProcAddress(m, "DirectInput8Create") : nullptr;
    if (!create) { CoopLog("DINPUT: couldn't load dinput8.dll - not hooked"); return; }
    static const GUID IID_DI8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
    static const GUID IID_DI8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
    static const GUID SysKbd   = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
    int done = 0;
    for (int w = 1; w >= 0; --w)   // W first (it enumerates the controllers)
    {
        void* di = nullptr;
        if (create(GetModuleHandleW(nullptr), 0x0800, w ? IID_DI8W : IID_DI8A, &di, nullptr) < 0 || !di) continue;
        void* dev = nullptr;
        if (((DiCreateDev_t)(*(void***)di)[3])(di, SysKbd, &dev, nullptr) >= 0 && dev)
        {
            if (DiPatchVt(dev, w != 0, "keyboard")) ++done;
            ((DiRelease_t)(*(void***)dev)[2])(dev);
        }
        // every attached controller too (enumerated through a W interface; the GUIDs work for A as well)
        if (w)
        {
            DiGuidList l; memset(&l, 0, sizeof(l));
            ((IDirectInput8W*)di)->EnumDevices(DI8DEVCLASS_GAMECTRL, DiCollectCb, &l, DIEDFL_ATTACHEDONLY);
            g_DiCtrl = l;
        }
        for (int k = 0; k < g_DiCtrl.c; ++k)
        {
            void* cd = nullptr;
            if (((DiCreateDev_t)(*(void***)di)[3])(di, g_DiCtrl.g[k], &cd, nullptr) >= 0 && cd)
            {
                DiPatchVt(cd, w != 0, g_DiCtrl.n[k]);
                ((DiRelease_t)(*(void***)cd)[2])(cd);
            }
        }
        static void* s_keep = nullptr;   // the first W object stays alive, as it always has (g_DI8 itself is gone - unused since f1.14)
        if (w && !s_keep) s_keep = di;
        else ((DiRelease_t)(*(void***)di)[2])(di);
    }
    g_DiLockInit = done > 0;
    CoopLog("DINPUT: %s (%d device tables patched)", done ? "hooked - player 2/3 controllers get hidden, all controllers idle during their menus" : "hook FAILED", g_DiVtN);
}

