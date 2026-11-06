// =================================================================================
// THE FOCUS HOOKS (restructure 3d, from menus/34): every push / pop / focus change of the game's focus manager runs in the
// right player's focus context (routing - EnterCtx / LeaveCtx) and is logged with its owner (FOCUS lines, research log)
// =================================================================================
typedef void* (*FPush_t)(unsigned char* mgr, GSp* w, int prio);
typedef void* (*FPushR_t)(unsigned char* mgr, GSp* w, GSp* restore, unsigned char cause, int prio);
typedef void* (*FPop_t)(unsigned char* mgr, GSp* w);
typedef void* (*FSetFocus_t)(unsigned char* mgr, GSp* w, unsigned char cause, unsigned char ignoreStack);
typedef void* (*FSetChild_t)(unsigned char* mgr, GSp* parent, GSp* child, unsigned char cause);
typedef void* (*FReset_t)(unsigned char* mgr, void* a, void* b, void* c);
static FPush_t Orig_FPush = nullptr; static FPushR_t Orig_FPushR = nullptr; static FPop_t Orig_FPop = nullptr;
static FSetFocus_t Orig_FSetFocus = nullptr; static FSetChild_t Orig_FSetChild = nullptr; static FReset_t Orig_FReset = nullptr;
static int g_FocusLogs = 0, g_FocusNest = 0;
static LONG g_FocusCalls = 0;
static const int FOCUS_LOG_CAP = 400;
static bool FocusInBattle() { return GetTickCount64() - g_LastBcmTick < BATTLE_UI_STALE_MS && g_LastBattleUi >= 8; }   // logs only in battles (the caps would be gone by then otherwise)
static bool FocusLogOk(unsigned char* mgr)
{
    if (!mgr || GetCurrentThreadId() != g_GameThreadId) return false;
    static bool checked = false;
    if (!checked)
    {
        checked = true;
        uintptr_t vt = *(uintptr_t*)mgr;
        CoopLog("FOCUS: focus manager %p - the game's pointer %p (%s), vtable %llx (%s); per-player focus contexts %s",   // (no UI dispatcher part)
            mgr, FocusMgr(), mgr == FocusMgr() ? "same" : "DIFFERENT", (unsigned long long)(vt - g_Base), vt == g_Base + 0x4BF24B0 ? "expected" : "NOT the expected 4BF24B0",
            g_FCtxOn ? "ON" : "off");
    }
    return mgr == FocusMgr();
}
// one hooked focus call: who owns its widget(s), which context it runs in, what it changed
struct FocusCall
{
    bool active;   // our manager, game thread
    bool log;
    int owner, owner2, ctx, prevCtx, depth, pathN, leaf; char stack[160];
};
static int OwnerOfArg(GSp* w) { return (w && Readable(w, 16)) ? (w->obj ? FocusOwner(w->obj, w->ctrl) : OWNER_NULL_WIDGET) : OWNER_NO_ARG; }
static void FocusBegin(unsigned char* mgr, GSp* w, GSp* w2, FocusCall& c)
{
    c.active = false; c.log = false; c.prevCtx = g_FCtxLive; c.ctx = g_FCtxLive;
    InterlockedIncrement(&g_FocusCalls);
    bool logWant = g_FocusLogs < FOCUS_LOG_CAP && FocusInBattle();
    if ((!g_FCtxOn && !logWant) || !FocusLogOk(mgr)) return;
    c.active = true; c.log = logWant;
    c.owner = OwnerOfArg(w);
    c.owner2 = OwnerOfArg(w2);
    if (g_FCtxOn)
    {
        int to = RouteOf(c.owner >= 0 ? c.owner : (c.owner2 >= 0 ? c.owner2 : -1));
        c.prevCtx = EnterCtx(to); c.ctx = g_FCtxLive;
        if (c.ctx != c.prevCtx) InterlockedIncrement(&g_FCtxRouted[c.ctx & 3]);
    }
    if (!c.log) return;
    c.depth = *(int*)(mgr + 0x48); c.pathN = PathNum(mgr); c.leaf = PathLeafOwner(mgr);
    StackDesc(mgr, c.stack, sizeof(c.stack));
}
static const char* OwnArg(int o) { return o == OWNER_NULL_WIDGET ? "null" : o == OWNER_NO_ARG ? "-" : OwnTag(o); }
static void FocusEnd(unsigned char* mgr, FocusCall& c, const char* fn, const char* args, void* ret)
{
    if (!c.active) return;
    if (c.log && g_FocusLogs < FOCUS_LOG_CAP)
    {
        ++g_FocusLogs;
        char st[160]; StackDesc(mgr, st, sizeof(st));
        int leaf = PathLeafOwner(mgr);
        uintptr_t r = (uintptr_t)ret;
        char cx[40] = "";
        if (g_FCtxOn) snprintf(cx, sizeof(cx), " | in P%d's context%s", c.ctx, c.ctx != c.prevCtx ? " (switched)" : "");
        CoopLog("FOCUS: %*s%s(%s) owner %s%s%s%s | stack %d -> %d [%s] -> [%s] | path %d -> %d, leaf %s -> %s | from %llx | ui P1 %d P2 %d P3 %d",
            g_FocusNest * 2, "", fn, args, OwnArg(c.owner), c.owner2 != -4 ? " / " : "", c.owner2 != -4 ? OwnArg(c.owner2) : "", cx,
            c.depth, *(int*)(mgr + 0x48), c.stack, st, c.pathN, PathNum(mgr), OwnTag(c.leaf), OwnTag(leaf),
            (unsigned long long)(r >= g_Base && r < g_Base + 0x8000000 ? r - g_Base : r),
            BcmOf(1) ? *(int*)(BcmOf(1) + BCM_UI_STATE) : -1, BcmOf(2) ? *(int*)(BcmOf(2) + BCM_UI_STATE) : -1, BcmOf(3) ? *(int*)(BcmOf(3) + BCM_UI_STATE) : -1);
        if (g_FocusLogs == FOCUS_LOG_CAP) CoopLog("FOCUS: %d calls logged - no more (log cap; contexts routed P1 %ld P2 %ld P3 %ld so far)", FOCUS_LOG_CAP, g_FCtxRouted[1], g_FCtxRouted[2], g_FCtxRouted[3]);
    }
    if (g_FCtxOn) LeaveCtx(c.prevCtx);
}
static void* Hook_FPush(unsigned char* mgr, GSp* w, int prio)
{
    void* ret = __builtin_return_address(0);
    FocusCall c; FocusBegin(mgr, w, nullptr, c);
    ++g_FocusNest; void* r = Orig_FPush(mgr, w, prio); --g_FocusNest;
    char a[32]; snprintf(a, sizeof(a), "prio %d", prio);
    FocusEnd(mgr, c, "Push", a, ret);
    return r;
}
static void* Hook_FPushR(unsigned char* mgr, GSp* w, GSp* restore, unsigned char cause, int prio)
{
    void* ret = __builtin_return_address(0);
    FocusCall c; FocusBegin(mgr, w, restore, c);
    ++g_FocusNest; void* r = Orig_FPushR(mgr, w, restore, cause, prio); --g_FocusNest;
    char a[48]; snprintf(a, sizeof(a), "cause %d, prio %d", (int)cause, prio);
    FocusEnd(mgr, c, "PushR", a, ret);
    return r;
}
static void* Hook_FPop(unsigned char* mgr, GSp* w)
{
    void* ret = __builtin_return_address(0);
    FocusCall c; FocusBegin(mgr, w, nullptr, c);
    ++g_FocusNest; void* r = Orig_FPop(mgr, w); --g_FocusNest;
    FocusEnd(mgr, c, "Pop", "", ret);
    return r;
}
static void* Hook_FSetFocus(unsigned char* mgr, GSp* w, unsigned char cause, unsigned char ignoreStack)
{
    void* ret = __builtin_return_address(0);
    FocusCall c; FocusBegin(mgr, w, nullptr, c);
    ++g_FocusNest; void* r = Orig_FSetFocus(mgr, w, cause, ignoreStack); --g_FocusNest;
    char a[48]; snprintf(a, sizeof(a), "cause %d, ignoreStack %d", (int)cause, (int)ignoreStack);
    FocusEnd(mgr, c, "SetFocus", a, ret);
    return r;
}
static void* Hook_FSetChild(unsigned char* mgr, GSp* parent, GSp* child, unsigned char cause)
{
    void* ret = __builtin_return_address(0);
    FocusCall c; FocusBegin(mgr, parent, child, c);
    ++g_FocusNest; void* r = Orig_FSetChild(mgr, parent, child, cause); --g_FocusNest;
    char a[32]; snprintf(a, sizeof(a), "cause %d", (int)cause);
    FocusEnd(mgr, c, "SetFocusToChild", a, ret);
    return r;
}
static void* Hook_FReset(unsigned char* mgr, void* a1, void* a2, void* a3)
{
    void* ret = __builtin_return_address(0);
    FocusCall c; FocusBegin(mgr, nullptr, nullptr, c);   // (step B: runs in whoever's context is live - player 1's outside the mod's own calls)
    ++g_FocusNest; void* r = Orig_FReset(mgr, a1, a2, a3); --g_FocusNest;
    FocusEnd(mgr, c, "ResetAll", "", ret);
    return r;
}
